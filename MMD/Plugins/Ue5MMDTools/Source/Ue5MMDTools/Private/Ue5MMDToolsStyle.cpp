// Copyright Epic Games, Inc. All Rights Reserved.
#include "Ue5MMDToolsStyle.h"
#include "Ue5MMDTools.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FUe5MMDToolsStyle::StyleInstance = nullptr;

void FUe5MMDToolsStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FUe5MMDToolsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FUe5MMDToolsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("Ue5MMDToolsStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FUe5MMDToolsStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("Ue5MMDToolsStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("Ue5MMDTools")->GetBaseDir() / TEXT("Resources"));

	Style->Set("Ue5MMDTools.OpenPluginWindow", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));

	return Style;
}

void FUe5MMDToolsStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FUe5MMDToolsStyle::Get()
{
	return *StyleInstance;
}
