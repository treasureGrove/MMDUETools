#include "UI/MMDImportSetting.h"

#include "UI/MMDViewPanel.h"
#include "UI/MMDToolPanelStyle.h"
#include "UI/SMMDAnimatedButton.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Stage/MMDStageMaterialState.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelUtils.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "RenderUtils.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/ObjectSaveContext.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor MutedText = MMDPanelStyle::Muted;
	const FName ToolStageTag(TEXT("MMDToolPanelStage"));

	TSharedRef<SWidget> Hint(const TCHAR* Text)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true)
			.ColorAndOpacity(MutedText).Font(FAppStyle::GetFontStyle("SmallFont"));
	}

	TSharedRef<SWidget> SectionTitle(const TCHAR* Text)
	{
		return SNew(STextBlock).Text(FText::FromString(Text))
			.ColorAndOpacity(MMDPanelStyle::Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 11));
	}

	struct FStyleEntry
	{
		const TCHAR* Name;
		const TCHAR* Description;
		const TCHAR* Asset;
		const TCHAR* Mark = nullptr;
	};

	const FStyleEntry MainStyles[] = {
		{TEXT("终末地 V2"), TEXT("清晰明暗 · 细描边 · 工业动画质感"), TEXT("EndfieldV2/DA_Render_EndfieldV2"), TEXT("V2")},
		{TEXT("无限暖暖"), TEXT("柔和肤色 · 发丝光泽 · 细腻布料"), TEXT("DA_Render_InfinityNikki"), TEXT("N")}
	};
	const FStyleEntry ReferenceStyles[] = {
		{TEXT("明快卡通"), TEXT("鲜明色彩与卡通明暗"), TEXT("DA_Render_OpenWorldToon")},
		{TEXT("银白柔光"), TEXT("低对比与清透色彩"), TEXT("DA_Render_SilverCity")},
		{TEXT("电影冷暖"), TEXT("冷暖对比与柔和高光"), TEXT("DA_Render_CinematicAnime")},
		{TEXT("工业动画"), TEXT("偏冷色调与硬朗明暗"), TEXT("DA_Render_IndustrialAnime")},
		{TEXT("霓虹演唱会"), TEXT("适合霓虹舞台的角色配色"), TEXT("DA_Render_NeonConcert")},
		{TEXT("月光镜庭"), TEXT("适合月夜舞台的角色配色"), TEXT("DA_Render_MoonMirror")},
		{TEXT("樱庭水镜"), TEXT("适合庭院舞台的角色配色"), TEXT("DA_Render_SakuraTheater")},
		{TEXT("终末地 · 旧版"), TEXT("保留旧工程的兼容方案"), TEXT("Endfield/DA_Render_Endfield")}
	};

	FString StylePath(const TCHAR* Asset)
	{
		return FString(TEXT("/Ue5MMDTools/Stages/Presets/")) + Asset;
	}
}

TSharedRef<SWidget> MMDImportSetting::BuildRenderPresetPanel()
{
	struct FStyleSearchState
	{
		FString Query;
		TSet<FString> MatchingAssets;
		int32 ReferenceMatches = UE_ARRAY_COUNT(ReferenceStyles);
		bool bExpandedBeforeSearch = false;
		TWeakPtr<SSearchBox> SearchBox;
		TWeakPtr<SExpandableArea> ReferenceArea;
	};
	const TSharedRef<FStyleSearchState> SearchState = MakeShared<FStyleSearchState>();
	auto UpdateSearch = [SearchState](const FText& Text)
	{
		const bool bWasSearching = !SearchState->Query.IsEmpty();
		SearchState->Query = Text.ToString().TrimStartAndEnd();
		TArray<FString> Terms;
		SearchState->Query.ParseIntoArrayWS(Terms);
		SearchState->MatchingAssets.Reset();
		SearchState->ReferenceMatches = 0;
		auto AddMatch = [SearchState, &Terms](const FStyleEntry& Entry)
		{
			const FString Searchable = FString(Entry.Name) + TEXT(" ") + Entry.Description + TEXT(" ") + StylePath(Entry.Asset);
			for (const FString& Term : Terms)
			{
				if (!Searchable.Contains(Term, ESearchCase::IgnoreCase)) return false;
			}
			SearchState->MatchingAssets.Add(Entry.Asset);
			return true;
		};
		for (const FStyleEntry& Entry : MainStyles) AddMatch(Entry);
		for (const FStyleEntry& Entry : ReferenceStyles)
		{
			if (AddMatch(Entry)) ++SearchState->ReferenceMatches;
		}
		if (const TSharedPtr<SExpandableArea> Area = SearchState->ReferenceArea.Pin())
		{
			if (!bWasSearching && !SearchState->Query.IsEmpty()) SearchState->bExpandedBeforeSearch = Area->IsExpanded();
			if (!SearchState->Query.IsEmpty()) Area->SetExpanded(SearchState->ReferenceMatches > 0);
			else if (bWasSearching) Area->SetExpanded(SearchState->bExpandedBeforeSearch);
		}
	};
	UpdateSearch(FText::GetEmpty());
	static const FSearchBoxStyle SearchStyle = []()
	{
		FSearchBoxStyle Style = FAppStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox");
		Style.TextBoxStyle.SetBackgroundImageNormal(*MMDPanelStyle::CardBrush())
			.SetBackgroundImageHovered(*MMDPanelStyle::HeaderBrush())
			.SetBackgroundImageFocused(*MMDPanelStyle::SelectedCardBrush())
			.SetForegroundColor(MMDPanelStyle::Text).SetFocusedForegroundColor(MMDPanelStyle::Text)
			.SetPadding(FMargin(10, 7));
		Style.TextBoxStyle.TextStyle.SetColorAndOpacity(MMDPanelStyle::Text);
		Style.GlassImage.TintColor = MMDPanelStyle::Muted;
		Style.ClearImage.TintColor = MMDPanelStyle::Accent;
		return Style;
	}();
	PreSaveWorldHandle = FEditorDelegates::PreSaveWorldWithContext.AddLambda(
		[this](UWorld* World, FObjectPreSaveContext) { SuspendLevelPresetPreviews(World); });
	PostSaveWorldHandle = FEditorDelegates::PostSaveWorldWithContext.AddLambda(
		[this](UWorld* World, FObjectPostSaveContext) { ResumeLevelPresetPreviews(World); });
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	Panel->AddSlot().AutoHeight().Padding(0, 2, 0, 12)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SectionTitle(TEXT("应用目标"))]
		+ SHorizontalBox::Slot().AutoWidth()
		[SNew(SBorder).BorderImage(MMDPanelStyle::BadgeBrush()).Padding(FMargin(8, 4))
			[SNew(STextBlock).Text(FText::FromString(TEXT("临时预览"))).ColorAndOpacity(MMDPanelStyle::Accent).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))]]
	];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
		[
			SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Toggle()).HAlign(HAlign_Center)
			.ButtonColorAndOpacity_Lambda([this]() { return !bApplyPresetToLevelSelection ? MMDPanelStyle::Accent : FLinearColor::White; })
			.OnClicked_Lambda([this]() { bApplyPresetToLevelSelection = false; return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(TEXT("预览角色"))).ColorAndOpacity_Lambda([this]() { return !bApplyPresetToLevelSelection ? MMDPanelStyle::PrimaryText : MMDPanelStyle::Text; })]
		]
		+ SHorizontalBox::Slot().FillWidth(1)
		[
			SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Toggle()).HAlign(HAlign_Center)
			.ButtonColorAndOpacity_Lambda([this]() { return bApplyPresetToLevelSelection ? MMDPanelStyle::Accent : FLinearColor::White; })
			.OnClicked_Lambda([this]() { bApplyPresetToLevelSelection = true; return FReply::Handled(); })
			[SNew(STextBlock).Text(FText::FromString(TEXT("关卡所选"))).ColorAndOpacity_Lambda([this]() { return bApplyPresetToLevelSelection ? MMDPanelStyle::PrimaryText : MMDPanelStyle::Text; })]
		]
	];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[SNew(STextBlock).Text_Lambda([this]() { return GetRenderPresetTargetText(); }).ToolTipText_Lambda([this]() { return GetRenderPresetTargetText(); })
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity(MutedText)];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
		[Hint(TEXT("保留角色底色与贴图。临时预览，关闭面板或重载后恢复；保存关卡保留原材质。"))];

	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
	[
		SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text).HAlign(HAlign_Center).ContentPadding(FMargin(10, 6))
		.Text(FText::FromString(TEXT("恢复原材质")))
		.IsEnabled_Lambda([this]() { return !GetRenderPresetTargets().IsEmpty(); })
		.OnClicked_Raw(this, &MMDImportSetting::RestoreRenderPreset)
	];
	TSharedPtr<SSearchBox> SearchBox;
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
	[
		SAssignNew(SearchBox, SSearchBox).Style(&SearchStyle).Tag(TEXT("MMD.StyleSearch"))
		.HintText(FText::FromString(TEXT("搜索风格，如：暖暖 / 霓虹 / Nikki")))
		.ToolTipText(FText::FromString(TEXT("按名称、效果描述或英文名称筛选；多个关键词用空格分隔。")))
		.DelayChangeNotificationsWhileTyping(false)
		.OnTextChanged_Lambda(UpdateSearch)
	];
	SearchState->SearchBox = SearchBox;
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
		[SNew(STextBlock).Tag(TEXT("MMD.StyleSearchCount")).ColorAndOpacity(MutedText)
			.Text_Lambda([SearchState]()
			{
				const int32 Total = UE_ARRAY_COUNT(MainStyles) + UE_ARRAY_COUNT(ReferenceStyles);
				return FText::FromString(SearchState->Query.IsEmpty()
					? FString::Printf(TEXT("%d 种风格 · 点击卡片按钮即可预览"), Total)
					: FString::Printf(TEXT("找到 %d / %d 种风格"), SearchState->MatchingAssets.Num(), Total));
			})]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
		[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).Tag(TEXT("MMD.StyleSearchClear")).ButtonStyle(MMDPanelStyle::Tab()).ForegroundColor(MMDPanelStyle::Accent).ContentPadding(FMargin(6, 3))
			.Text(FText::FromString(TEXT("清空搜索")))
			.Visibility_Lambda([SearchState]() { return SearchState->Query.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			.OnClicked_Lambda([SearchState]()
			{
				if (const TSharedPtr<SSearchBox> Input = SearchState->SearchBox.Pin())
				{
					Input->SetText(FText::GetEmpty());
					return FReply::Handled().SetUserFocus(Input.ToSharedRef(), EFocusCause::SetDirectly);
				}
				return FReply::Handled();
			})]
	];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(SBorder).Tag(TEXT("MMD.StyleSearchEmpty")).BorderImage(MMDPanelStyle::CardBrush()).Padding(14)
		.Visibility_Lambda([SearchState]() { return SearchState->MatchingAssets.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
		[SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SectionTitle(TEXT("暂时没有匹配的风格"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Hint(TEXT("换个关键词，或清空搜索查看全部风格。"))]]
	];
	auto MakeStyleCard = [this, SearchState](const FStyleEntry& Entry) -> TSharedRef<SWidget>
	{
		const FString Path = StylePath(Entry.Asset);
		const FString Asset = Entry.Asset;
		const bool bAssetExists = FPackageName::DoesPackageExist(Path);
		const TCHAR* Mark = Entry.Mark;
		return SNew(SBorder).BorderImage_Lambda([this, Path]() { return IsRenderPresetApplied(Path) ? MMDPanelStyle::SelectedCardBrush() : MMDPanelStyle::CardBrush(); }).Padding(14)
			.Tag(FName(*Path)).Visibility_Lambda([SearchState, Asset]() { return SearchState->MatchingAssets.Contains(Asset) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, Mark ? 10 : 0, 0)
				[SNew(SBox).WidthOverride(Mark ? 38 : 0).HeightOverride(Mark ? 38 : 0)
					[SNew(SBorder).Visibility(Mark ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
						.BorderImage(MMDPanelStyle::BadgeBrush()).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[SNew(STextBlock).Text(FText::FromString(Mark ? Mark : TEXT(""))).ColorAndOpacity(MMDPanelStyle::Accent).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))]]]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SectionTitle(Entry.Name)]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[SNew(STextBlock).Text_Lambda([this, Path]() { return FText::FromString(IsRenderPresetApplied(Path) ? TEXT("✓ 已应用") : TEXT("")); })
					.ColorAndOpacity(MMDPanelStyle::Accent).Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[Hint(Entry.Description)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Toggle())
				.ButtonColorAndOpacity_Lambda([this, Path]() { return IsRenderPresetApplied(Path) ? MMDPanelStyle::Accent : FLinearColor::White; })
				.ForegroundColor_Lambda([this, Path]() { return IsRenderPresetApplied(Path) ? MMDPanelStyle::PrimaryText : MMDPanelStyle::Accent; })
				.HAlign(HAlign_Center).ContentPadding(FMargin(12, 7))
				.Text_Lambda([this, Path]() { return FText::FromString(IsRenderPresetApplied(Path) ? TEXT("✓ 已应用") : bApplyPresetToLevelSelection ? TEXT("预览到关卡所选 →") : TEXT("预览风格 →")); })
				.IsEnabled_Lambda([this, bAssetExists]() { return bAssetExists && !GetRenderPresetTargets().IsEmpty(); })
				.ToolTipText(FText::FromString(TEXT("应用到上方指定的角色。重复选择保留当前预览；按住 Shift 点击可重新应用修改后的配方。")))
				.OnClicked_Lambda([this, Path]() { return ApplyRenderPreset(Path, FSlateApplication::Get().GetModifierKeys().IsShiftDown()); })
			]
		];
	};
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
	[
		SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(MutedText)
		.Text_Lambda([]()
		{
			const auto& Status = MMDEndfieldNativeBackend::GetStatus();
			return FText::FromString(Substrate::IsToonProfileEnabled() && Status.bActive && Status.bProfileMarkerAllowed && Status.Mode == 2
				? TEXT("原生风格已就绪") : TEXT("当前使用兼容效果；完整风格需要启用 Substrate 与插件原生后端。"));
		})
	];
	for (const FStyleEntry& Entry : MainStyles)
	{
		Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[MakeStyleCard(Entry)];
	}
	TSharedRef<SVerticalBox> MoreStyles = SNew(SVerticalBox);
	for (const FStyleEntry& Entry : ReferenceStyles)
	{
		MoreStyles->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[MakeStyleCard(Entry)];
	}
	TSharedPtr<SExpandableArea> ReferenceArea;
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
	[
		SAssignNew(ReferenceArea, SExpandableArea).Tag(TEXT("MMD.StyleSearchReference")).InitiallyCollapsed(true).Style(MMDPanelStyle::Expandable())
		.Visibility_Lambda([SearchState]() { return SearchState->ReferenceMatches > 0 ? EVisibility::Visible : EVisibility::Collapsed; })
		.BorderImage(MMDPanelStyle::CardBrush()).HeaderPadding(FMargin(10, 10)).Padding(FMargin(6))
		.HeaderContent()
		[SNew(STextBlock).ColorAndOpacity(MMDPanelStyle::Text).Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.Text_Lambda([SearchState]()
			{
				return FText::FromString(FString::Printf(TEXT("更多风格 · 兼容预设（%d）"), SearchState->ReferenceMatches));
			})]
		.BodyContent()[MoreStyles]
	];
	SearchState->ReferenceArea = ReferenceArea;
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[SectionTitle(TEXT("使用自定义预设"))];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Hint(TEXT("选择预设资产后，立即应用到上方目标。"))];
	Panel->AddSlot().AutoHeight()
	[
		SNew(SObjectPropertyEntryBox).AllowedClass(UMMDStageRenderPreset::StaticClass())
		.ObjectPath_Lambda([this]() { return CustomRenderPresetPath; })
		.OnObjectChanged_Lambda([this](const FAssetData& Asset)
		{
			CustomRenderPresetPath = Asset.GetObjectPathString();
			if (Asset.IsValid()) ApplyRenderPreset(CustomRenderPresetPath);
		})
	];
	return Panel;
}

TArray<USkeletalMeshComponent*> MMDImportSetting::GetRenderPresetTargets() const
{
	TArray<USkeletalMeshComponent*> Result;
	auto AddActor = [&Result](AActor* Actor)
	{
		if (!IsValid(Actor)) return;
		TInlineComponentArray<USkeletalMeshComponent*> Components(Actor);
		for (USkeletalMeshComponent* Component : Components)
		{
			if (IsValid(Component) && Component->GetSkeletalMeshAsset() && !Component->IsVisualizationComponent()) Result.AddUnique(Component);
		}
	};
	if (bApplyPresetToLevelSelection)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World && !GEditor->PlayWorld && GEditor->GetSelectedActors())
		{
			for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			{
				AActor* Actor = Cast<AActor>(*It);
				if (IsValid(Actor) && Actor->GetWorld() == World) AddActor(Actor);
			}
		}
	}
	else if (ViewPanel.IsValid()) AddActor(ViewPanel->GetPreviewActor());
	return Result;
}

bool MMDImportSetting::IsRenderPresetApplied(const FString& PresetPath) const
{
	const auto Targets = GetRenderPresetTargets();
	if (Targets.IsEmpty()) return false;
	for (USkeletalMeshComponent* Component : Targets)
	{
		const auto* State = Component->GetAssetUserData<UMMDStageMaterialState>();
		if (!State || !State->AppliedPreset || State->AppliedPreset->GetOutermost()->GetName() != PresetPath) return false;
	}
	return true;
}

FText MMDImportSetting::GetRenderPresetTargetText() const
{
	const TArray<USkeletalMeshComponent*> Targets = GetRenderPresetTargets();
	if (Targets.IsEmpty()) return FText::FromString(bApplyPresetToLevelSelection
		? TEXT("请在关卡中选中角色，支持多选。") : TEXT("请先在「角色」中导入或加载角色。"));
	return FText::FromString(FString::Printf(TEXT("目标：%s%s · %d 个网格"),
		*Targets[0]->GetOwner()->GetActorLabel(), Targets.Num() > 1 ? TEXT(" 等") : TEXT(""), Targets.Num()));
}

FReply MMDImportSetting::ApplyRenderPreset(const FString& PresetPath, bool bForceRefresh)
{
	if (!bForceRefresh && IsRenderPresetApplied(PresetPath))
	{
		ShowImportProgress(TEXT("当前目标已使用这个风格。修改预设配方后，可按住 Shift 点击重新应用。"));
		return FReply::Handled();
	}
	UMMDStageRenderPreset* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *PresetPath);
	if (!Preset)
	{
		ShowImportProgress(TEXT("预设资产不可用，请检查插件内容。"), EMMDMessageType::Error);
		return FReply::Handled();
	}
	const TArray<USkeletalMeshComponent*> Targets = GetRenderPresetTargets();
	int32 Applied = 0;
	for (USkeletalMeshComponent* Component : Targets)
	{
		if (UMMDStageLibrary::ApplyCharacterRenderPreset(Component, Preset))
		{
			++Applied;
			if (bApplyPresetToLevelSelection) LevelPresetPreviews.Add(Component, TStrongObjectPtr<UMMDStageRenderPreset>(Preset));
		}
	}
	if (GEditor) GEditor->RedrawLevelEditingViewports();
	const FString Name = Preset->DisplayName.IsEmpty() ? Preset->GetName() : Preset->DisplayName.ToString();
	ShowImportProgress(Applied > 0 ? FString::Printf(TEXT("已应用 %s：%d/%d 个网格，可恢复原材质。"), *Name, Applied, Targets.Num())
		: TEXT("未应用预设，请先选择有效角色与目标范围。"),
		Applied == Targets.Num() && Applied > 0 ? EMMDMessageType::Success : EMMDMessageType::Warning);
	return FReply::Handled();
}

FReply MMDImportSetting::RestoreRenderPreset()
{
	int32 Restored = 0;
	for (USkeletalMeshComponent* Component : GetRenderPresetTargets())
	{
		if (UMMDStageLibrary::RestoreCharacterMaterials(Component)) ++Restored;
		LevelPresetPreviews.Remove(Component);
		SuspendedLevelPresetPreviews.Remove(Component);
	}
	if (GEditor) GEditor->RedrawLevelEditingViewports();
	ShowImportProgress(Restored > 0 ? FString::Printf(TEXT("已恢复 %d 个网格的原材质。"), Restored) : TEXT("当前目标没有可恢复的预设。"),
		Restored > 0 ? EMMDMessageType::Success : EMMDMessageType::Info);
	return FReply::Handled();
}

MMDImportSetting::~MMDImportSetting()
{
	FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveWorldHandle);
	FEditorDelegates::PostSaveWorldWithContext.Remove(PostSaveWorldHandle);
	for (const auto& Entry : LevelPresetPreviews)
	{
		USkeletalMeshComponent* Component = Entry.Key.Get();
		const UMMDStageMaterialState* State = Component ? Component->GetAssetUserData<UMMDStageMaterialState>() : nullptr;
		if (State && State->AppliedPreset == Entry.Value.Get()) UMMDStageLibrary::RestoreCharacterMaterials(Component);
	}
}

void MMDImportSetting::SuspendLevelPresetPreviews(UWorld* World)
{
	// 保存原始覆盖，避免临时 MID 和临时恢复记录进入关卡。
	for (const auto& Entry : LevelPresetPreviews)
	{
		USkeletalMeshComponent* Component = Entry.Key.Get();
		if (!Component || Component->GetWorld() != World) continue;
		const UMMDStageMaterialState* State = Component->GetAssetUserData<UMMDStageMaterialState>();
		if (State && State->AppliedPreset == Entry.Value.Get() && UMMDStageLibrary::RestoreCharacterMaterials(Component))
			SuspendedLevelPresetPreviews.Add(Component);
	}
}

void MMDImportSetting::ResumeLevelPresetPreviews(UWorld* World)
{
	for (auto It = SuspendedLevelPresetPreviews.CreateIterator(); It; ++It)
	{
		USkeletalMeshComponent* Component = It->Get();
		if (Component && Component->GetWorld() != World) continue;
		const TStrongObjectPtr<UMMDStageRenderPreset>* Preset = LevelPresetPreviews.Find(*It);
		if (Component && Preset) UMMDStageLibrary::ApplyCharacterRenderPreset(Component, Preset->Get());
		It.RemoveCurrent();
	}
}

TSharedRef<SWidget> MMDImportSetting::BuildScenePresetPanel()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	Panel->AddSlot().AutoHeight().Padding(0, 2, 0, 6)[SectionTitle(TEXT("角色展示场景"))];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[Hint(TEXT("一键切换布景与配套光照，保留当前动作和角色风格。"))];
	auto MakeDisplayCard = [this](EMMDStudioDisplayStyle Style) -> TSharedRef<SWidget>
	{
		const bool bDefault = Style == EMMDStudioDisplayStyle::Studio;
		return SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))
			.Tag(FName(*(TEXT("MMDDisplayScene.") + FMMDStudioDisplayScene::GetId(Style))))
			.ButtonStyle(MMDPanelStyle::Secondary()).HAlign(HAlign_Fill).ContentPadding(FMargin(10, 8))
			.ButtonColorAndOpacity_Lambda([this, Style]() { return ViewPanel->GetDisplaySceneStyle() == Style ? MMDPanelStyle::AccentSoft : FLinearColor::White; })
			.ToolTipText(FText::FromString(TEXT("切换右侧展示场景及配套光照。当前关卡、角色材质与动作保持不变。")))
			.OnClicked_Lambda([this, Style]()
			{
				const bool bReady = ViewPanel->PreviewDisplayScene(Style);
				ShowImportProgress(bReady ? TEXT("已切换展示场景：") + FMMDStudioDisplayScene::GetDisplayName(Style)
					+ TEXT(" · 配套光照已同步。") : TEXT("展示场景暂不可用。"), bReady ? EMMDMessageType::Success : EMMDMessageType::Error);
				return FReply::Handled();
			})
			[SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[SNew(STextBlock).Text(FText::FromString(FMMDStudioDisplayScene::GetDisplayName(Style) + (bDefault ? TEXT(" · 默认推荐") : TEXT(""))))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10)).ColorAndOpacity(MMDPanelStyle::Text)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[SNew(STextBlock).Text(FText::FromString(FMMDStudioDisplayScene::GetDescription(Style))).AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).ColorAndOpacity(MMDPanelStyle::Muted)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[SNew(STextBlock).Text_Lambda([this, Style]() { return FText::FromString(ViewPanel->GetDisplaySceneStyle() == Style ? TEXT("✓ 当前展示") : TEXT("切换展示 →")); })
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8)).ColorAndOpacity(MMDPanelStyle::Accent)]];
	};
	TArray<EMMDStudioDisplayStyle> MoreDisplays, EarlyDisplays;
	for (const auto Style : FMMDStudioDisplayScene::GetStyles())
	{
		if (Style == EMMDStudioDisplayStyle::Studio)
			Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[MakeDisplayCard(Style)];
		else if (Style == EMMDStudioDisplayStyle::SkyGlass || Style == EMMDStudioDisplayStyle::VioletStage || Style == EMMDStudioDisplayStyle::WarmAtelier)
			EarlyDisplays.Add(Style);
		else MoreDisplays.Add(Style);
	}
	auto MakeDisplayGrid = [&MakeDisplayCard](const TArray<EMMDStudioDisplayStyle>& Styles) -> TSharedRef<SWidget>
	{
		TSharedRef<SVerticalBox> Grid = SNew(SVerticalBox);
		for (int32 RowStart = 0; RowStart < Styles.Num(); RowStart += 2)
		{
			TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			for (int32 Column = 0; Column < 2; ++Column)
			{
				const int32 Index = RowStart + Column;
				TSharedRef<SWidget> Card = Styles.IsValidIndex(Index) ? MakeDisplayCard(Styles[Index]) : SNullWidget::NullWidget;
				Row->AddSlot().FillWidth(1).Padding(Column == 0 ? 0 : 3, 0, Column == 0 ? 3 : 0, 0)[Card];
			}
			Grid->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Row];
		}
		return Grid;
	};
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[SectionTitle(TEXT("更多展示预设"))];
	Panel->AddSlot().AutoHeight()[MakeDisplayGrid(MoreDisplays)];
	Panel->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
	[
		SNew(SExpandableArea).Tag(TEXT("MMDLegacyDisplayScenes")).InitiallyCollapsed(true).Style(MMDPanelStyle::Expandable())
		.BorderImage(MMDPanelStyle::CardBrush()).HeaderPadding(FMargin(10, 8)).Padding(FMargin(8))
		.HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("早期云空场景"))).ColorAndOpacity(MMDPanelStyle::Muted)]
		.BodyContent()[MakeDisplayGrid(EarlyDisplays)]
	];
	Panel->AddSlot().AutoHeight().Padding(0, 10, 0, 6)[SectionTitle(TEXT("完整舞台"))];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[Hint(TEXT("预览使用舞台原配灯光和后处理；只有「放入关卡」会修改当前关卡。"))];
	const FStyleEntry Stages[] = {
		{TEXT("霓虹演唱会"), TEXT("动态灯带 · 演出舞台"), TEXT("NeonConcert")},
		{TEXT("月光镜庭"), TEXT("月夜镜面 · 环形舞台"), TEXT("MoonMirror")},
		{TEXT("樱庭水镜"), TEXT("樱花庭院 · 水面涟漪"), TEXT("SakuraTheater")}
	};
	for (const FStyleEntry& Stage : Stages)
	{
		const FString Name(Stage.Asset);
		Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SBorder).BorderImage_Lambda([this, Name]()
			{
				const UMMDStageRenderPreset* Current = ViewPanel->GetStagePreviewPreset();
				return Current && Current->GetName() == TEXT("DA_Render_") + Name ? MMDPanelStyle::SelectedCardBrush() : MMDPanelStyle::CardBrush();
			}).Padding(12)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SectionTitle(Stage.Name)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[Hint(Stage.Description)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 5, 0)
					[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Primary()).ForegroundColor(MMDPanelStyle::PrimaryText).HAlign(HAlign_Center)
						.Text_Lambda([this, Name]()
						{
							const UMMDStageRenderPreset* Current = ViewPanel->GetStagePreviewPreset();
							return FText::FromString(Current && Current->GetName() == TEXT("DA_Render_") + Name ? TEXT("正在预览 ✓") : TEXT("预览舞台"));
						})
						.OnClicked_Lambda([this, Name]()
						{
							UMMDStageRenderPreset* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *StylePath(*(TEXT("DA_Render_") + Name)));
							const bool Ready = ViewPanel->PreviewStageEnvironment(Preset);
							ShowImportProgress(Ready ? TEXT("已切换舞台预览及配套光照，当前关卡不变。") : TEXT("舞台预览不可用。"), Ready ? EMMDMessageType::Success : EMMDMessageType::Error);
							return FReply::Handled();
						})]
					+ SHorizontalBox::Slot().FillWidth(1)
					[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text).HAlign(HAlign_Center).Text(FText::FromString(TEXT("放入关卡")))
						.IsEnabled_Lambda([]() { return GEditor && !GEditor->PlayWorld; })
						.OnClicked_Lambda([this, Name]() { return PlaceStagePreset(Name); })]
				]
			]
		];
	}
	Panel->AddSlot().AutoHeight().Padding(0, 12, 0, 6)[SectionTitle(TEXT("LOOKDEV · 光照环境"))];
	Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[Hint(TEXT("选择后回到中性检查台。风格材质保持不变，灯光、天空与下方状态同步。"))];
	const TCHAR* EnvironmentNames[] = { TEXT("三点布光"), TEXT("户外日光"), TEXT("阴天柔光"), TEXT("室内暖光"), TEXT("赛博霓虹"), TEXT("轮廓剪影"), TEXT("黄金时刻"), TEXT("恐怖绿光") };
	for (int32 Row = 0; Row < 4; ++Row)
	{
		TSharedRef<SHorizontalBox> RowWidget = SNew(SHorizontalBox);
		for (int32 Column = 0; Column < 2; ++Column)
		{
			const int32 Index = Row * 2 + Column;
			const EMMDLightingEnvironment Env = static_cast<EMMDLightingEnvironment>(Index + 1);
			RowWidget->AddSlot().FillWidth(1).Padding(0, 0, Column == 0 ? 5 : 0, 0)
			[
				SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Toggle()).HAlign(HAlign_Center)
				.ButtonColorAndOpacity_Lambda([this, Env]() { return ViewPanel->GetLightingEnvironment() == Env ? MMDPanelStyle::Accent : FLinearColor::White; })
				.OnClicked_Lambda([this, Env]()
				{
					const int32 Count = ViewPanel->ApplyLightingEnvironment(Env);
					ShowImportProgress(Count > 0 ? TEXT("已切换 LookDev：") + UMMDLightingEnvironmentLibrary::GetEnvironmentDisplayName(Env) : TEXT("光照切换失败。"), Count > 0 ? EMMDMessageType::Success : EMMDMessageType::Error);
					return FReply::Handled();
				})
				[SNew(STextBlock).Text(FText::FromString(EnvironmentNames[Index])).ColorAndOpacity_Lambda([this, Env]() { return ViewPanel->GetLightingEnvironment() == Env ? MMDPanelStyle::PrimaryText : MMDPanelStyle::Text; })]
			];
		}
		Panel->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[RowWidget];
	}
	Panel->AddSlot().AutoHeight().Padding(0, 10)
	[
		SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled)).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text).HAlign(HAlign_Center)
		.Tag(TEXT("MMDOpenPreviewMap"))
		.IsEnabled_Lambda([this]() { const auto Style = ViewPanel->GetDisplaySceneStyle(); return GEditor && !GEditor->PlayWorld && (!Style.IsSet() || Style == EMMDStudioDisplayStyle::Studio); })
		.Text_Lambda([this]()
		{
			const auto Style = ViewPanel->GetDisplaySceneStyle();
			return FText::FromString(Style.IsSet() ? (Style == EMMDStudioDisplayStyle::Studio ? TEXT("打开影棚光照参考关卡…") : TEXT("展示预览 · 暂无独立关卡")) : TEXT("打开当前预览对应关卡…"));
		})
		.ToolTipText_Lambda([this]()
		{
			const auto Style = ViewPanel->GetDisplaySceneStyle();
			return FText::FromString(Style.IsSet() ? TEXT("展示布景仅用于工具预览；影棚参考关卡只保存对应光照。新展示场景尚未保存为独立关卡。") : TEXT("打开当前舞台或 LookDev 对应的已保存关卡。"));
		})
		.OnClicked_Lambda([this]()
		{
			if (const auto Style = ViewPanel->GetDisplaySceneStyle(); Style.IsSet() && Style != EMMDStudioDisplayStyle::Studio) return FReply::Handled();
			if (const UMMDStageRenderPreset* Preset = ViewPanel->GetStagePreviewPreset())
				return OpenPresetMap(TEXT("/Ue5MMDTools/Stages/Maps/MMDStage_") + Preset->GetName().RightChop(FString(TEXT("DA_Render_")).Len()));
			const EMMDLightingEnvironment Env = ViewPanel->GetLightingEnvironment();
			if (!GEditor || GEditor->PlayWorld || !FEditorFileUtils::SaveDirtyPackages(true, true, false)) return FReply::Handled();
			const bool Opened = UMMDLightingEnvironmentLibrary::OpenEnvironmentLevel(Env);
			ShowImportProgress(Opened ? TEXT("已打开对应光照关卡。") : TEXT("打开光照关卡失败。"), Opened ? EMMDMessageType::Success : EMMDMessageType::Error);
			return FReply::Handled();
		})
	];
	Panel->AddSlot().AutoHeight().Padding(0, 4, 0, 0)
	[
		SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))
		.Tag(TEXT("MMDOpenFabLibrary")).ButtonStyle(MMDPanelStyle::Tab()).ForegroundColor(MMDPanelStyle::Accent)
		.ContentPadding(FMargin(8, 5)).Text(FText::FromString(TEXT("浏览 Fab 场景素材")))
		.ToolTipText(FText::FromString(TEXT("在浏览器中打开 Fab 的 Unreal Engine 素材页。资源加入库后，可从 UE 的「窗口 > Fab」添加到项目。")))
		.OnClicked_Lambda([this]()
		{
			FString Error;
			FPlatformProcess::LaunchURL(TEXT("https://www.fab.com/channels/unreal-engine"), nullptr, &Error);
			ShowImportProgress(Error.IsEmpty() ? TEXT("已打开 Fab；资源加入库后，可从 UE 的窗口 > Fab 添加到项目。")
				: TEXT("打开 Fab 失败：") + Error, Error.IsEmpty() ? EMMDMessageType::Info : EMMDMessageType::Error);
			return FReply::Handled();
		})
	];
	return Panel;
}

FReply MMDImportSetting::PlaceStagePreset(const FString& PresetName)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UMMDStageRenderPreset* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *StylePath(*(TEXT("DA_Render_") + PresetName)));
	if (!World || !Preset || GEditor->PlayWorld || !World->GetCurrentLevel() || FLevelUtils::IsLevelLocked(World->GetCurrentLevel()))
	{
		ShowImportProgress(TEXT("无法放置舞台，请检查预设资产、当前层级锁定状态并退出运行模式。"), EMMDMessageType::Warning);
		return FReply::Handled();
	}
	AMMDStageActor* Stage = nullptr;
	for (TActorIterator<AMMDStageActor> It(World); It; ++It)
	{
		if (It->GetLevel() == World->GetCurrentLevel() && It->ActorHasTag(ToolStageTag)) { Stage = *It; break; }
	}
	const FScopedTransaction Transaction(FText::FromString(TEXT("切换 MMD 预设舞台")));
	World->Modify();
	if (!Stage)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transactional;
		Params.OverrideLevel = World->GetCurrentLevel();
		Params.bDeferConstruction = true;
		Stage = World->SpawnActor<AMMDStageActor>(AMMDStageActor::StaticClass(), FTransform::Identity, Params);
		if (Stage)
		{
			Stage->Tags.AddUnique(ToolStageTag);
			Stage->RenderPreset = Preset;
			Stage->FinishSpawning(FTransform::Identity);
		}
	}
	else
	{
		Stage->Modify();
		Stage->ApplyStageRenderPreset(Preset);
	}
	if (Stage)
	{
		Stage->SetActorLabel(TEXT("MMD 舞台 · ") + Preset->DisplayName.ToString());
		Stage->MarkPackageDirty();
		if (ViewPanel.IsValid()) ViewPanel->PreviewStageEnvironment(Preset);
		GEditor->RedrawLevelEditingViewports();
	}
	ShowImportProgress(Stage ? TEXT("已切换舞台：") + Preset->DisplayName.ToString() + TEXT("。保存关卡即可保留。") : TEXT("舞台创建失败。"),
		Stage ? EMMDMessageType::Success : EMMDMessageType::Error);
	return FReply::Handled();
}

FReply MMDImportSetting::OpenPresetMap(const FString& PackagePath)
{
	if (!GEditor || GEditor->PlayWorld || !FPackageName::DoesPackageExist(PackagePath))
	{
		ShowImportProgress(TEXT("场景不可用，请检查资产并退出运行模式。"), EMMDMessageType::Warning);
		return FReply::Handled();
	}
	if (!FEditorFileUtils::SaveDirtyPackages(true, true, false)) return FReply::Handled();
	const bool Opened = FEditorFileUtils::LoadMap(PackagePath, false, true);
	if (Opened && !UMMDLightingEnvironmentLibrary::SyncToStageCamera()) UMMDLightingEnvironmentLibrary::ResetLevelViewportCamera();
	ShowImportProgress(Opened ? TEXT("已打开预设场景。") : TEXT("打开预设场景失败。"), Opened ? EMMDMessageType::Success : EMMDMessageType::Error);
	return FReply::Handled();
}
