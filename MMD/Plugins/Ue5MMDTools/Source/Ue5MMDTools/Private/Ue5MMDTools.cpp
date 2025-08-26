// Copyright Epic Games, Inc. All Rights Reserved.
#include "Ue5MMDTools.h"
#include "Ue5MMDToolsStyle.h"
#include "Ue5MMDToolsCommands.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"
#include "MMDViewPanel.h"
#include "MMDImportSetting.h"

static const FName Ue5MMDToolsTabName("Ue5MMDTools");

#define LOCTEXT_NAMESPACE "FUe5MMDToolsModule"

void FUe5MMDToolsModule::StartupModule()
{
    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

    FUe5MMDToolsStyle::Initialize();
    FUe5MMDToolsStyle::ReloadTextures();

    FUe5MMDToolsCommands::Register();

    PluginCommands = MakeShareable(new FUICommandList);

    PluginCommands->MapAction(
        FUe5MMDToolsCommands::Get().OpenPluginWindow,
        FExecuteAction::CreateRaw(this, &FUe5MMDToolsModule::PluginButtonClicked),
        FCanExecuteAction());

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUe5MMDToolsModule::RegisterMenus));

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Ue5MMDToolsTabName, FOnSpawnTab::CreateRaw(this, &FUe5MMDToolsModule::OnSpawnPluginTab)).SetDisplayName(LOCTEXT("FUe5MMDToolsTabTitle", "Ue5MMDTools")).SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUe5MMDToolsModule::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.

    UToolMenus::UnRegisterStartupCallback(this);

    UToolMenus::UnregisterOwner(this);

    FUe5MMDToolsStyle::Shutdown();

    FUe5MMDToolsCommands::Unregister();

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Ue5MMDToolsTabName);
}

TSharedRef<SDockTab> FUe5MMDToolsModule::OnSpawnPluginTab(const FSpawnTabArgs &SpawnTabArgs)
{
    FText WidgetText = FText::Format(
        LOCTEXT("WindowWidgetText", "Add code to {0} in {1} to override this window's contents"),
        FText::FromString(TEXT("FUe5MMDToolsModule::OnSpawnPluginTab")),
        FText::FromString(TEXT("Ue5MMDTools.cpp")));

    // 创建ViewPanel引用，以便传递给设置区
    TSharedRef<MMDViewPanel> ViewPanelWidget = SNew(MMDViewPanel);

    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
            [SNew(SSplitter)
                 .Orientation(Orient_Vertical)
             // 上方主内容区（左右可拖动）
             + SSplitter::Slot()
                   .Value(0.8f)
                       [SNew(SSplitter)
                            .Orientation(Orient_Horizontal) +
                        SSplitter::Slot()
                            .Value(0.25f)
                                [SNew(SBorder)
                                     .Padding(5)
                                         [SNew(STextBlock).Text(FText::FromString(TEXT("时间轴")))]] +
                        SSplitter::Slot()
                            .Value(0.75f)
                                [SNew(SBorder)
                                     .Padding(5)
                                         [ViewPanelWidget]]]
             // 下方设置区
             + SSplitter::Slot()
                   .Value(0.2f)
                       [SNew(SBorder)
                            .Padding(5)
                                [SNew(MMDImportSetting)
                                     .ViewPanel(ViewPanelWidget)]]];
    // ...existing code...
}

void FUe5MMDToolsModule::PluginButtonClicked()
{
    FGlobalTabmanager::Get()->TryInvokeTab(Ue5MMDToolsTabName);
}

void FUe5MMDToolsModule::RegisterMenus()
{
    // Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
    FToolMenuOwnerScoped OwnerScoped(this);

    {
        UToolMenu *Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
        {
            FToolMenuSection &Section = Menu->FindOrAddSection("WindowLayout");
            Section.AddMenuEntryWithCommandList(FUe5MMDToolsCommands::Get().OpenPluginWindow, PluginCommands);
        }
    }

    {
        UToolMenu *ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
        {
            FToolMenuSection &Section = ToolbarMenu->FindOrAddSection("Settings");
            {
                FToolMenuEntry &Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUe5MMDToolsCommands::Get().OpenPluginWindow));
                Entry.SetCommandList(PluginCommands);
            }
        }
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUe5MMDToolsModule, Ue5MMDTools)