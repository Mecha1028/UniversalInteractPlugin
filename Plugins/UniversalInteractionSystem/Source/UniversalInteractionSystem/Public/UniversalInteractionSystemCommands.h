// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "UniversalInteractionSystemStyle.h"

class FUniversalInteractionSystemCommands : public TCommands<FUniversalInteractionSystemCommands>
{
public:

	FUniversalInteractionSystemCommands()
		: TCommands<FUniversalInteractionSystemCommands>(TEXT("UniversalInteractionSystem"), NSLOCTEXT("Contexts", "UniversalInteractionSystem", "UniversalInteractionSystem Plugin"), NAME_None, FUniversalInteractionSystemStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};