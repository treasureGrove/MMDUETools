// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "Ue5MMDToolsStyle.h"

class FUe5MMDToolsCommands : public TCommands<FUe5MMDToolsCommands>
{
public:

	FUe5MMDToolsCommands()
		: TCommands<FUe5MMDToolsCommands>(TEXT("Ue5MMDTools"), NSLOCTEXT("Contexts", "Ue5MMDTools", "Ue5MMDTools Plugin"), NAME_None, FUe5MMDToolsStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};