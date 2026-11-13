// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ue5MMDToolsEditor.h"
#include "Ue5MMDToolsCommands.h"
#include "Ue5MMDToolsStyle.h"

#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "Misc/CoreDelegates.h"
#include "MMDImportSetting.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"

#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "ToolMenus.h"
#include "Editor/FMMDActorImportService.h"
#include "Import/TMMDMeshBuilder.h"
#include "UObject/CoreRedirects.h"
#include "Widgets/Docking/SDockTab.h"

#if WITH_EDITOR
#include "EditorModeRegistry.h"
#include "Editor/MMDMaterialPickerMode.h"
#endif

static const FName Ue5MMDToolsTabName("Ue5MMDTools");

// 控制台命令：MMD.OpenPanel —— 打开 MMD 工具面板（供脚本/MCP 验证用）
static FAutoConsoleCommand CmdMMDOpenPanel(
	TEXT("MMD.OpenPanel"),
	TEXT("打开 MMD 工具面板（Nomad Tab）"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(Ue5MMDToolsTabName);
	}));


#define LOCTEXT_NAMESPACE "FUe5MMDToolsEditorModule"


void FUe5MMDToolsEditorModule::StartupModule()
{
	// Default 阶段 CoreRedirects 已初始化，先迁移旧编辑器类引用，再加载编辑器资产。
	TArray<FCoreRedirect> Redirects;
	for (const TCHAR* ClassName : { TEXT("AnimGraphNode_MMDSkeletalControl"), TEXT("MMDToolPanelWidget"), TEXT("MMDToolPreviewRenderer"), TEXT("MMDMaterialEnsure"), TEXT("MMDMigrateShadowCommandlet"), TEXT("MMDValidateImportCommandlet"), TEXT("MMDValidateMaterialsCommandlet") })
	{
		Redirects.Emplace(ECoreRedirectFlags::Type_Class,
			FString(TEXT("/Script/Ue5MMDTools.")) + ClassName,
			FString(TEXT("/Script/Ue5MMDToolsEditor.")) + ClassName);
	}
	FCoreRedirects::AddRedirectList(Redirects, TEXT("Ue5MMDTools module split"));
	TMMDMeshBuilder::InitializeAsyncImports();
	ActorImportService = MakeUnique<FMMDActorImportService>();
	IMMDActorImportService::Set(ActorImportService.Get());
	// ---------- UI ----------
	// Defer until the engine (GEngine, Slate, ToolMenus, tab manager) is ready.
	OnPostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FUe5MMDToolsEditorModule::OnPostEngineInit);
}

void FUe5MMDToolsEditorModule::OnPostEngineInit()
{
	// 命令let 只需加载类型和 shader 路径，不注册交互式编辑器界面。
	if (IsRunningCommandlet())
	{
		return;
	}
#if WITH_EDITOR
	// 材质拾取模式（选中 AMMDActor 自动进入，点击部位高亮材质）
	FEditorModeRegistry::Get().RegisterMode(
		FMMDMaterialPickerMode::EM_MaterialPicker,
		MakeShareable(new FMMDMaterialPickerMode::FFactory));
#endif

	FUe5MMDToolsStyle::Initialize();
	FUe5MMDToolsStyle::ReloadTextures();
	FUe5MMDToolsCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FUe5MMDToolsCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FUe5MMDToolsEditorModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUe5MMDToolsEditorModule::RegisterMenus));

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(Ue5MMDToolsTabName,
			FOnSpawnTab::CreateRaw(this, &FUe5MMDToolsEditorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FUe5MMDToolsTabTitle", "Ue5MMDTools"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
	bEditorUIInitialized = true;
}

void FUe5MMDToolsEditorModule::ShutdownModule()
{
	if (OnPostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(OnPostEngineInitHandle);
		OnPostEngineInitHandle.Reset();
	}

	TMMDMeshBuilder::ShutdownAsyncImports();
	IMMDActorImportService::Set(nullptr);
	ActorImportService.Reset();
	if (!bEditorUIInitialized)
	{
		return;
	}
	bEditorUIInitialized = false;

#if WITH_EDITOR
	FEditorModeRegistry::Get().UnregisterMode(FMMDMaterialPickerMode::EM_MaterialPicker);
#endif

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FUe5MMDToolsStyle::Shutdown();
	FUe5MMDToolsCommands::Unregister();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Ue5MMDToolsTabName);
}

TSharedRef<SDockTab> FUe5MMDToolsEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<MMDImportSetting> ImportSetting = SNew(MMDImportSetting);
	MMDImportSetting::RegisterInstance(ImportSetting);
	return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ImportSetting];
}

void FUe5MMDToolsEditorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(Ue5MMDToolsTabName);
}

void FUe5MMDToolsEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
		Section.AddMenuEntryWithCommandList(
			FUe5MMDToolsCommands::Get().OpenPluginWindow, PluginCommands);
	}
	{
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		FToolMenuSection& Section = Toolbar->FindOrAddSection("Settings");
		FToolMenuEntry& Entry = Section.AddEntry(
			FToolMenuEntry::InitToolBarButton(FUe5MMDToolsCommands::Get().OpenPluginWindow));
		Entry.SetCommandList(PluginCommands);
	}
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FUe5MMDToolsEditorModule, Ue5MMDToolsEditor)
