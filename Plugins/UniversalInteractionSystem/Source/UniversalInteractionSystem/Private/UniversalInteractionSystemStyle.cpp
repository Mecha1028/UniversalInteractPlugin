// Copyright Epic Games, Inc. All Rights Reserved.

#include "UniversalInteractionSystemStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FUniversalInteractionSystemStyle::StyleInstance = nullptr;

void FUniversalInteractionSystemStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FUniversalInteractionSystemStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FUniversalInteractionSystemStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("UniversalInteractionSystemStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FUniversalInteractionSystemStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("UniversalInteractionSystemStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("UniversalInteractionSystem")->GetBaseDir() / TEXT("Resources"));

	return Style;
}

void FUniversalInteractionSystemStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FUniversalInteractionSystemStyle::Get()
{
	return *StyleInstance;
}
