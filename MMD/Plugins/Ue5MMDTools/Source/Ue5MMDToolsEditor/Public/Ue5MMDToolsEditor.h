// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleManager.h"

class FUe5MMDToolsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override { return false; }

	void PluginButtonClicked();

private:
	void OnPostEngineInit();
	void RegisterMenus();
	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

	TSharedPtr<class FUICommandList> PluginCommands;
	TUniquePtr<class FMMDActorImportService> ActorImportService;
	FDelegateHandle OnPostEngineInitHandle;
	bool bEditorUIInitialized = false;
};
