#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "UI/MMDToolPanelStyle.h"
#include "UI/SMMDAnimatedButton.h"
#include "UI/MMDPreviewMotionPresets.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

TSharedRef<SWidget> MMDImportSetting::BuildMotionPresetPanel()
{
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
	const auto Presets = MMDGetPreviewMotionPresets();
	const TCHAR* Descriptions[] = { TEXT("双手身前"), TEXT("轻叹与呼吸"), TEXT("托腮与侧头"), TEXT("整理衣服") };
	for (int32 Row = 0; Row < 2; ++Row)
	{
		TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
		for (int32 Column = 0; Column < 2; ++Column)
		{
			const int32 Index = Row * 2 + Column;
			const FName Id = Presets[Index].Id;
			Buttons->AddSlot().FillWidth(1).Padding(2)
			[
				SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Secondary()).HAlign(HAlign_Fill).ContentPadding(FMargin(10, 8))
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewModel(); })
				.ToolTipText(FText::FromString(TEXT("替换右侧预览动作，保留暂停、倍速与定格状态。重复点击不会从头开始。")))
				.ButtonColorAndOpacity_Lambda([this, Id]() { return ViewPanel->GetBuiltinMotion() == Id ? MMDPanelStyle::AccentSoft : FLinearColor::White; })
				.OnClicked_Lambda([this, Id]()
				{
					FText Message;
					const bool bSuccess = ViewPanel->PlayBuiltinMotion(Id, Message);
					ShowImportProgress(Message.ToString(), bSuccess ? EMMDMessageType::Info : EMMDMessageType::Warning);
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)
						[SNew(STextBlock).Text(FText::FromString(Presets[Index].Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9)).ColorAndOpacity(MMDPanelStyle::Text)]
						+ SHorizontalBox::Slot().AutoWidth()
						[SNew(STextBlock).Text(FText::FromString(TEXT("当前"))).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Accent)
							.Visibility_Lambda([this, Id]() { return ViewPanel->GetBuiltinMotion() == Id ? EVisibility::Visible : EVisibility::Hidden; })]]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s · %.1fs"), Descriptions[Index], Presets[Index].Duration)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Muted)]
				]
			];
		}
		Content->AddSlot().AutoHeight()[Buttons];
	}
	Content->AddSlot().AutoHeight().Padding(2, 5)
	[SNew(STextBlock).Text(FText::FromString(TEXT("播放、定格与物理控制在预览区底部。"))).AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Muted)];
	Content->AddSlot().AutoHeight().Padding(2, 2)
	[SNew(STextBlock).Text(FText::FromString(TEXT("动作：MMDAgent / NITech · CC BY 3.0"))).ColorAndOpacity(MMDPanelStyle::Muted)
		.ToolTipText(FText::FromString(TEXT("Copyright (c) 2009-2015 Nagoya Institute of Technology, Department of Computer Science.\nMMDAgent Motion: Wait / Breath / Idle of Mei.\nhttps://creativecommons.org/licenses/by/3.0/")))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7)).AutoWrapText(true)];
	return SNew(SBorder).BorderImage(MMDPanelStyle::CardBrush()).Padding(6)[Content];
}

TSharedRef<SWidget> MMDImportSetting::BuildPreviewControls()
{
	return SNew(SBorder).Tag(TEXT("MMDPreviewControls")).BorderImage(MMDPanelStyle::HeaderBrush()).Padding(8)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).Tag(TEXT("MMDPreviewTogglePlay")).ButtonStyle(MMDPanelStyle::Primary()).HAlign(HAlign_Center).ContentPadding(FMargin(14, 6))
				.ToolTipText(FText::FromString(TEXT("暂停／继续动作和物理。视口内可按空格。")))
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.OnClicked_Lambda([this]() { ViewPanel->SetPreviewPlaying(!ViewPanel->IsPreviewPlaying()); return FReply::Handled(); })
				[SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(ViewPanel->IsPreviewPlaying() ? TEXT("暂停") : TEXT("继续")); }).ColorAndOpacity(MMDPanelStyle::PrimaryText)]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text).Text(FText::FromString(TEXT("重播")))
				.ToolTipText(FText::FromString(TEXT("回到起点并正常播放，退出定格。视口内可按 Home。")))
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.OnClicked_Lambda([this]() { ViewPanel->RestartPreviewAnimation(); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).Tag(TEXT("MMDPreviewHoldPose")).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text)
				.Text_Lambda([this]() { return FText::FromString(ViewPanel->IsPreviewPoseHeld() ? TEXT("解除定格") : TEXT("定格姿势")); })
				.ButtonColorAndOpacity_Lambda([this]() { return ViewPanel->IsPreviewPoseHeld() ? MMDPanelStyle::AccentSoft : FLinearColor::White; })
				.ToolTipText(FText::FromString(TEXT("固定当前动作时间，播放状态下头发、衣物仍实时运算。再次点击恢复动作。")))
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.OnClicked_Lambda([this]() { ViewPanel->SetPreviewPoseHeld(!ViewPanel->IsPreviewPoseHeld()); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth()
			[SNew(SComboButton).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text)
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.ToolTipText(FText::FromString(TEXT("动作播放速度；定格时保留所选速度，解除定格后生效。")))
				.ButtonContent()[SNew(STextBlock).ColorAndOpacity(MMDPanelStyle::Text)
					.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.1f×"), ViewPanel->GetPreviewPlaybackSpeed())); })]
				.OnGetMenuContent_Lambda([this]()
				{
					FMenuBuilder Menu(true, nullptr);
					for (float Speed : {0.5f, 1.0f, 1.5f})
						Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%.1f×"), Speed)), FText(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this, Speed]() { ViewPanel->SetPreviewPlaybackSpeed(Speed); })));
					return Menu.MakeWidget();
				})]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).HAlign(HAlign_Right).Padding(8, 0)
			[SNew(STextBlock).Text_Lambda([this]() { return ViewPanel->GetPreviewPlaybackStatus(); })
				.ToolTipText(FText::FromString(TEXT("本栏只控制右侧角色预览。"))).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Muted)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))
				.Tag(TEXT("MMDPreviewPreviousFrame")).ButtonStyle(MMDPanelStyle::Secondary()).ContentPadding(FMargin(6, 3))
				.Text(FText::FromString(TEXT("‹ 帧"))).ForegroundColor(MMDPanelStyle::Text)
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.ToolTipText(FText::FromString(TEXT("上一采样帧：暂停动作与物理，精确检查姿势。到达开头后保持首帧。")))
				.OnClicked_Lambda([this]() { ViewPanel->StepPreviewFrame(-1); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[SNew(SSlider).Tag(TEXT("MMDPreviewTimeline")).Style(MMDPanelStyle::Slider()).SliderBarColor(MMDPanelStyle::Line).SliderHandleColor(MMDPanelStyle::Accent)
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.ToolTipText(FText::FromString(TEXT("拖动定位动作，松开后恢复此前的播放状态。键盘选中时间轴后按 Enter 开始微调，方向键每次移动一采样帧，再按 Enter 结束。")))
				.Value_Lambda([this]() { const float Length = ViewPanel->GetPreviewDuration(); return Length > 0 ? ViewPanel->GetPreviewTime() / Length : 0; })
				.StepSize_Lambda([this]() { const float Length = ViewPanel->GetPreviewDuration(); return Length > 0 ? FMath::Clamp(ViewPanel->GetPreviewFrameInterval() / Length, 0.0f, 1.0f) : 1.0f; })
				.OnMouseCaptureBegin_Lambda([this]() { ViewPanel->BeginPreviewScrub(); })
				.OnMouseCaptureEnd_Lambda([this]() { ViewPanel->EndPreviewScrub(); })
				.OnControllerCaptureBegin_Lambda([this]() { ViewPanel->BeginPreviewScrub(); })
				.OnControllerCaptureEnd_Lambda([this]() { ViewPanel->EndPreviewScrub(); })
				.OnValueChanged_Lambda([this](float Value) { ViewPanel->SeekPreviewAnimation(Value * ViewPanel->GetPreviewDuration()); })]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0, 0, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))
				.Tag(TEXT("MMDPreviewNextFrame")).ButtonStyle(MMDPanelStyle::Secondary()).ContentPadding(FMargin(6, 3))
				.Text(FText::FromString(TEXT("帧 ›"))).ForegroundColor(MMDPanelStyle::Text)
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewAnimation(); })
				.ToolTipText(FText::FromString(TEXT("下一采样帧：暂停动作与物理，精确检查姿势。到达结尾后保持末帧，不循环回零。")))
				.OnClicked_Lambda([this]() { ViewPanel->StepPreviewFrame(1); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Muted)
				.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.2f / %.2f s"), ViewPanel->GetPreviewTime(), ViewPanel->GetPreviewDuration())); })]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[SNew(SCheckBox)
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewPhysics(); })
				.ToolTipText(FText::FromString(TEXT("只切换模型的 MMD 物理，不切换动作。无刚体模型仅播放骨骼动画。")))
				.IsChecked_Lambda([this]() { return ViewPanel->IsPreviewPhysicsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { ViewPanel->SetPreviewPhysicsEnabled(State == ECheckBoxState::Checked); })
				[SNew(STextBlock).Text(FText::FromString(TEXT("实时物理"))).ColorAndOpacity(MMDPanelStyle::Text).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))]]
			+ SHorizontalBox::Slot().AutoWidth().Padding(7, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Tab()).ForegroundColor(MMDPanelStyle::Muted).Text(FText::FromString(TEXT("重置物理")))
				.ToolTipText(FText::FromString(TEXT("重建头发／衣物物理，保留当前动作时间和机位。")))
				.IsEnabled_Lambda([this]() { return ViewPanel->HasPreviewPhysics() && ViewPanel->IsPreviewPhysicsEnabled(); })
				.OnClicked_Lambda([this]() { ViewPanel->ResetPreviewPhysics(); return FReply::Handled(); })]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).HAlign(HAlign_Right)
			[SNew(STextBlock).Text_Lambda([this]() { return ViewPanel->GetPreviewPhysicsStatus(); })
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Muted)]
		]
	];
}
