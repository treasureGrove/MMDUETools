#pragma once

#include "CoreMinimal.h"
#include "MMDToolPanelStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Input/SButton.h"

// 复用原生按钮的输入、无障碍与参数，仅在交互期间绘制轻微反馈。
class SMMDAnimatedButton : public SButton
{
public:
	using FArguments = SButton::FArguments;

	void Construct(const FArguments& InArgs, TAttribute<bool> InMotionEnabled = true)
	{
		MotionEnabled = MoveTemp(InMotionEnabled);
		SButton::Construct(InArgs);
		SetCanTick(false);
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SButton::OnMouseEnter(MyGeometry, MouseEvent);
		AnimateToInteractionState();
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SButton::OnMouseLeave(MouseEvent);
		AnimateToInteractionState();
	}

	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override
	{
		bKeyboardFocus = InFocusEvent.GetCause() != EFocusCause::Mouse;
		const FReply Reply = SButton::OnFocusReceived(MyGeometry, InFocusEvent);
		AnimateToInteractionState();
		return Reply;
	}

	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override
	{
		bKeyboardFocus = false;
		SButton::OnFocusLost(InFocusEvent);
		AnimateToInteractionState();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (!MotionEnabled.Get(true) || !ShouldBeEnabled(bParentEnabled))
		{
			return SButton::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
				LayerId, InWidgetStyle, bParentEnabled);
		}

		// 只变换绘制几何；按钮的布局尺寸、命中区域和外部 RenderTransform 保持稳定。
		const FGeometry PaintGeometry = AllottedGeometry.MakeChild(
			FSlateRenderTransform(FScale2D(CurrentState.Y), FVector2f(0.0f, CurrentState.X)));
		const int32 LastLayer = SButton::OnPaint(Args, PaintGeometry, MyCullingRect,
			OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		if (CurrentState.Z <= UE_SMALL_NUMBER)
		{
			return LastLayer;
		}

		FLinearColor Outline = MMDPanelStyle::Accent * InWidgetStyle.GetColorAndOpacityTint();
		Outline.A *= CurrentState.Z * 0.65f;
		FSlateRoundedBoxBrush FocusBrush(FLinearColor::Transparent, 6.0f, Outline, 1.5f);
		FocusBrush.OutlineSettings.bUseBrushTransparency = false;
		// MakeBox 的 Tint 是填充色；边框单独衰减，避免白色填充盖住文字。
		FSlateDrawElement::MakeBox(OutDrawElements, LastLayer + 1, PaintGeometry.ToPaintGeometry(),
			&FocusBrush, ESlateDrawEffect::None, FLinearColor::Transparent);
		return LastLayer + 1;
	}

protected:
	virtual void Press() override
	{
		SButton::Press();
		AnimateToInteractionState();
	}

	virtual void Release() override
	{
		SButton::Release();
		AnimateToInteractionState();
	}

private:
	void AnimateToInteractionState()
	{
		if (!MotionEnabled.Get(true) || !IsEnabled())
		{
			CurrentState = FVector3f(0.0f, 1.0f, 0.0f);
			TargetState = CurrentState;
			if (const TSharedPtr<FActiveTimerHandle> Timer = MotionTimer.Pin())
			{
				UnRegisterActiveTimer(Timer.ToSharedRef());
				MotionTimer.Reset();
			}
			Invalidate(EInvalidateWidgetReason::Paint);
			return;
		}

		const bool bAttention = IsHovered() || bKeyboardFocus;
		const bool bPressed = IsPressed();
		const FVector3f NewTarget(bPressed ? 0.5f : (bAttention ? -1.5f : 0.0f),
			bPressed ? 0.985f : 1.0f, bAttention || bPressed ? 1.0f : 0.0f);
		if (NewTarget.Equals(TargetState))
		{
			return;
		}

		StartState = CurrentState;
		TargetState = NewTarget;
		Elapsed = 0.0f;
		Duration = bPressed ? 0.12f : 0.16f;
		if (!MotionTimer.IsValid())
		{
			MotionTimer = RegisterActiveTimer(0.0f,
				FWidgetActiveTimerDelegate::CreateSP(this, &SMMDAnimatedButton::AdvanceMotion));
		}
	}

	EActiveTimerReturnType AdvanceMotion(double CurrentTime, float DeltaTime)
	{
		if (!MotionEnabled.Get(true) || !IsEnabled())
		{
			CurrentState = FVector3f(0.0f, 1.0f, 0.0f);
			TargetState = CurrentState;
			MotionTimer.Reset();
			Invalidate(EInvalidateWidgetReason::Paint);
			return EActiveTimerReturnType::Stop;
		}

		Elapsed += DeltaTime;
		const float Alpha = FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f);
		const float Ease = 1.0f - FMath::Pow(1.0f - Alpha, 3.0f);
		CurrentState = FMath::Lerp(StartState, TargetState, Ease);
		Invalidate(EInvalidateWidgetReason::Paint);
		if (Alpha >= 1.0f)
		{
			MotionTimer.Reset();
			return EActiveTimerReturnType::Stop;
		}
		return EActiveTimerReturnType::Continue;
	}

	TAttribute<bool> MotionEnabled;
	TWeakPtr<FActiveTimerHandle> MotionTimer;
	// X 为垂直偏移，Y 为缩放，Z 为边框反馈强度。
	FVector3f CurrentState = FVector3f(0.0f, 1.0f, 0.0f);
	FVector3f StartState = FVector3f(0.0f, 1.0f, 0.0f);
	FVector3f TargetState = FVector3f(0.0f, 1.0f, 0.0f);
	float Elapsed = 0.0f;
	float Duration = 0.16f;
	bool bKeyboardFocus = false;
};
