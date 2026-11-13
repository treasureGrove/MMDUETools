#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"

// 二次元角色菜单主题，只作用于本工具，不注册编辑器全局样式。
namespace MMDPanelStyle
{
	inline FLinearColor RGB(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }
	inline const FLinearColor Canvas = RGB(234, 238, 250);
	inline const FLinearColor Panel = RGB(246, 248, 255);
	inline const FLinearColor Card = RGB(253, 254, 255);
	inline const FLinearColor Line = RGB(200, 208, 231);
	inline const FLinearColor Text = RGB(38, 48, 81);
	inline const FLinearColor Muted = RGB(96, 110, 142);
	inline const FLinearColor Accent = RGB(97, 104, 222);
	inline const FLinearColor AccentSoft = RGB(230, 232, 255);
	inline const FLinearColor PrimaryText = FLinearColor::White;
	inline const FLinearColor Success = RGB(34, 116, 111);
	inline const FLinearColor Warning = RGB(153, 88, 31);
	inline const FLinearColor Error = RGB(179, 54, 82);

	inline const FSlateBrush* CardBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Card, 8.0f, Line, 1.0f);
		return &Brush;
	}

	inline const FSlateBrush* SelectedCardBrush()
	{
		static const FSlateRoundedBoxBrush Brush(AccentSoft, 8.0f, Accent, 1.4f);
		return &Brush;
	}

	inline const FSlateBrush* HeaderBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Panel, 8.0f, Line, 1.0f);
		return &Brush;
	}

	inline const FSlateBrush* BadgeBrush()
	{
		static const FSlateRoundedBoxBrush Brush(AccentSoft, 5.0f);
		return &Brush;
	}

	inline FButtonStyle MakeButton(const FLinearColor& Normal, const FLinearColor& Hover, const FLinearColor& Pressed,
		const FLinearColor& Outline, const FLinearColor& Foreground)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(Normal, 6.0f, Outline, 1.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(Hover, 6.0f, Accent, 1.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(Pressed, 6.0f, Accent, 1.0f));
		Style.SetDisabled(FSlateRoundedBoxBrush(RGB(224, 228, 239), 6.0f, Line, 1.0f));
		Style.SetNormalForeground(Foreground).SetHoveredForeground(Foreground).SetPressedForeground(Foreground);
		Style.SetDisabledForeground(Muted);
		Style.SetNormalPadding(FMargin(12, 8));
		Style.SetPressedPadding(FMargin(12, 9, 12, 7));
		return Style;
	}

	inline const FButtonStyle* Primary()
	{
		static const FButtonStyle Style = MakeButton(Accent, RGB(119, 125, 242), RGB(77, 84, 196), Accent, PrimaryText);
		return &Style;
	}

	inline const FButtonStyle* Secondary()
	{
		static const FButtonStyle Style = MakeButton(Card, RGB(239, 241, 255), AccentSoft, Line, Text);
		return &Style;
	}

	inline const FButtonStyle* Toggle()
	{
		static const FButtonStyle Style = MakeButton(FLinearColor::White, RGB(245, 246, 255), RGB(231, 234, 253), Line, Text);
		return &Style;
	}

	inline const FButtonStyle* Tab()
	{
		static const FButtonStyle Style = MakeButton(FLinearColor::Transparent, AccentSoft, AccentSoft, FLinearColor::Transparent, Text);
		return &Style;
	}

	inline const FExpandableAreaStyle* Expandable()
	{
		static const FExpandableAreaStyle Style = []()
		{
			FExpandableAreaStyle Result = FAppStyle::Get().GetWidgetStyle<FExpandableAreaStyle>("ExpandableArea");
			Result.CollapsedImage.TintColor = Text;
			Result.ExpandedImage.TintColor = Text;
			return Result;
		}();
		return &Style;
	}

	inline const FSplitterStyle* Splitter()
	{
		static const FSplitterStyle Style = FSplitterStyle()
			.SetHandleNormalBrush(FSlateRoundedBoxBrush(Line, 0.0f))
			.SetHandleHighlightBrush(FSlateRoundedBoxBrush(Accent, 0.0f));
		return &Style;
	}

	inline const FScrollBarStyle* ScrollBar()
	{
		static const FScrollBarStyle Style = FScrollBarStyle()
			.SetHorizontalBackgroundImage(FSlateNoResource()).SetVerticalBackgroundImage(FSlateNoResource())
			.SetVerticalTopSlotImage(FSlateNoResource()).SetVerticalBottomSlotImage(FSlateNoResource())
			.SetHorizontalTopSlotImage(FSlateNoResource()).SetHorizontalBottomSlotImage(FSlateNoResource())
			.SetNormalThumbImage(FSlateRoundedBoxBrush(Line, 3.0f))
			.SetHoveredThumbImage(FSlateRoundedBoxBrush(Muted, 3.0f))
			.SetDraggedThumbImage(FSlateRoundedBoxBrush(Accent, 3.0f)).SetThickness(4.0f);
		return &Style;
	}

	inline const FScrollBoxStyle* ScrollBox()
	{
		static const FScrollBoxStyle Style = FScrollBoxStyle()
			.SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource())
			.SetLeftShadowBrush(FSlateNoResource()).SetRightShadowBrush(FSlateNoResource());
		return &Style;
	}

	inline const FSpinBoxStyle* SpinBox()
	{
		static const FSpinBoxStyle Style = []()
		{
			FSpinBoxStyle Result = FAppStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");
			Result.SetBackgroundBrush(*CardBrush()).SetActiveBackgroundBrush(*SelectedCardBrush()).SetHoveredBackgroundBrush(*HeaderBrush());
			Result.SetInactiveFillBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f));
			Result.SetHoveredFillBrush(FSlateRoundedBoxBrush(AccentSoft, 4.0f)).SetActiveFillBrush(FSlateRoundedBoxBrush(AccentSoft, 4.0f));
			Result.SetForegroundColor(Text);
			Result.ArrowsImage.TintColor = Accent;
			return Result;
		}();
		return &Style;
	}

	inline const FSliderStyle* Slider()
	{
		static const FSliderStyle Style = FSliderStyle()
			.SetNormalBarImage(FSlateRoundedBoxBrush(FLinearColor::White, 2.0f))
			.SetHoveredBarImage(FSlateRoundedBoxBrush(FLinearColor::White, 2.0f))
			.SetDisabledBarImage(FSlateRoundedBoxBrush(FLinearColor::White, 2.0f))
			.SetNormalThumbImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f, FVector2D(8, 14)))
			.SetHoveredThumbImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f, FVector2D(8, 14)))
			.SetDisabledThumbImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f, FVector2D(8, 14)))
			.SetBarThickness(3.0f);
		return &Style;
	}
}
