// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ue5MMDTools.h"
#include "Ue5MMDToolsCommands.h"
#include "Ue5MMDToolsStyle.h"

#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "Misc/CoreDelegates.h"
#include "MMDImportSetting.h"
#include "Misc/Paths.h"
#include "Rendering/FMMDAnimeLightViewExtension.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#if WITH_EDITOR
#include "EditorModeRegistry.h"
#include "Editor/MMDMaterialPickerMode.h"
#endif

static const FName Ue5MMDToolsTabName("Ue5MMDTools");

#define LOCTEXT_NAMESPACE "FUe5MMDToolsModule"


void FUe5MMDToolsModule::StartupModule()
{
	// ---------- shader directory mapping ----------
	// Must happen early (module loads at PostConfigInit, before the engine's shader
	// serialization history is initialized, so global shaders register correctly).
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools")))
	{
		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/Ue5MMDTools"), ShaderDir);
		UE_LOG(LogTemp, Log, TEXT("Ue5MMDTools shader dir mapped: /Plugin/Ue5MMDTools"));
	}

	// ---------- UI ----------
	// Defer until the engine (GEngine, Slate, ToolMenus, tab manager) is ready.
	OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FUe5MMDToolsModule::OnPostEngineInit);
}

void FUe5MMDToolsModule::OnPostEngineInit()
{
	// ---------- light data collection view extension ----------
	// Collects lights in SetupViewFamily so the light data RT is same-frame synced.
	LightViewExtension = FSceneViewExtensions::NewExtension<FMMDAnimeLightViewExtension>();

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
		FExecuteAction::CreateRaw(this, &FUe5MMDToolsModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUe5MMDToolsModule::RegisterMenus));

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(Ue5MMDToolsTabName,
			FOnSpawnTab::CreateRaw(this, &FUe5MMDToolsModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FUe5MMDToolsTabTitle", "Ue5MMDTools"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUe5MMDToolsModule::ShutdownModule()
{
	if (OnPostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitHandle);
		OnPostEngineInitHandle.Reset();
	}

	LightViewExtension.Reset();

#if WITH_EDITOR
	FEditorModeRegistry::Get().UnregisterMode(FMMDMaterialPickerMode::EM_MaterialPicker);
#endif

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FUe5MMDToolsStyle::Shutdown();
	FUe5MMDToolsCommands::Unregister();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Ue5MMDToolsTabName);
}

TSharedRef<SDockTab> FUe5MMDToolsModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<MMDImportSetting> ImportSetting = SNew(MMDImportSetting);
	MMDImportSetting::RegisterInstance(ImportSetting);
	return SNew(SDockTab).TabRole(ETabRole::NomadTab)[ImportSetting];
}

void FUe5MMDToolsModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(Ue5MMDToolsTabName);
}

void FUe5MMDToolsModule::RegisterMenus()
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
IMPLEMENT_MODULE(FUe5MMDToolsModule, Ue5MMDTools)
