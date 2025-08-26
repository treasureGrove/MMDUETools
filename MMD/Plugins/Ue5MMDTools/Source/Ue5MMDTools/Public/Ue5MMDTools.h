// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleManager.h"
/**
 * FUe5MMDToolsModule类
 * 继承自IModuleInterface接口，是一个模块类，用于实现UE5的MMD工具插件功能
 */
class FUe5MMDToolsModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

private:
	void RegisterMenus();

	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

private:
	TSharedPtr<class FUICommandList> PluginCommands;
};
