// Copyright Epic Games, Inc. All Rights Reserved.
#include "Ue5MMDToolsCommands.h"
#include "Ue5MMDTools.h"

#define LOCTEXT_NAMESPACE "FUe5MMDToolsModule"

void FUe5MMDToolsCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "Ue5MMDTools", "Bring up Ue5MMDTools window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
