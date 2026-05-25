// Copyright Epic Games, Inc. All Rights Reserved.

#include "Ue5MMDTools.h"
#include "Ue5MMDToolsCommands.h"
#include "Ue5MMDToolsStyle.h"

#include "LevelEditor.h"
#include "MMDImportSetting.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

static const FName Ue5MMDToolsTabName("Ue5MMDTools");

#define LOCTEXT_NAMESPACE "FUe5MMDToolsModule"

void FUe5MMDToolsModule::StartupModule()
{
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
