// Copyright Epic Games, Inc. All Rights Reserved.

#include "UniversalInteractionSystemCommands.h"

#define LOCTEXT_NAMESPACE "FUniversalInteractionSystemModule"

void FUniversalInteractionSystemCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "UniversalInteractionSystem", "Bring up UniversalInteractionSystem window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
