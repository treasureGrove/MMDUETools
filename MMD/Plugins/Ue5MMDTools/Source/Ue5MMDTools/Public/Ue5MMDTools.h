// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleManager.h"

class FUe5MMDToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void PluginButtonClicked();

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedPtr<class FMMDAnimeViewExtension> AnimeViewExtension;
};
