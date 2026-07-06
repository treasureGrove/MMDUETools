// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ue5MMDTools.h"
#include "Ue5MMDToolsCommands.h"
#include "Ue5MMDToolsStyle.h"

#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "MMDImportSetting.h"
#include "Misc/Paths.h"
#if 0 // Disabled - needs UE 5.5 post-process API migration
#include "Rendering/MMDAnimeViewExtension.h"
#include "SceneViewExtension.h"
#endif
#include "ShaderCore.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

static const FName Ue5MMDToolsTabName("Ue5MMDTools");

#define LOCTEXT_NAMESPACE "FUe5MMDToolsModule"

void FUe5MMDToolsModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
	if (Plugin.IsValid())
	{
		const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/Ue5MMDTools"), ShaderDirectory);
		UE_LOG(LogTemp, Log, TEXT("Ue5MMDTools shader directory mapped: /Plugin/Ue5MMDTools -> %s"), *ShaderDirectory);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Ue5MMDTools plugin descriptor not found; shader directory mapping was skipped."));
	}

#if 0 // Disabled - needs UE 5.5 post-process API migration
	AnimeViewExtension = FSceneViewExtensions::NewExtension<FMMDAnimeViewExtension>();
	UE_LOG(LogTemp, Log, TEXT("MMD AnimeViewExtension registered."));
#endif

	FUe5MMDToolsStyle::Initialize();
	FUe5MMDToolsStyle::ReloadTextures();

	FUe5MMDToolsCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FUe5MMDToolsCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FUe5MMDToolsModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUe5MMDToolsModule::RegisterMenus));

	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(Ue5MMDToolsTabName, FOnSpawnTab::CreateRaw(this, &FUe5MMDToolsModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FUe5MMDToolsTabTitle", "Ue5MMDTools"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUe5MMDToolsModule::ShutdownModule()
{
#if 0 // Disabled - needs UE 5.5 post-process API migration
	// Release the SceneViewExtension so it is not leaked.
	AnimeViewExtension.Reset();
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

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			ImportSetting
		];
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
		Section.AddMenuEntryWithCommandList(FUe5MMDToolsCommands::Get().OpenPluginWindow, PluginCommands);
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
		FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUe5MMDToolsCommands::Get().OpenPluginWindow));
		Entry.SetCommandList(PluginCommands);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUe5MMDToolsModule, Ue5MMDTools)
