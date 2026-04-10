// Copyright Epic Games, Inc. All Rights Reserved.

#include "UniversalInteractionSystem.h"
#include "UniversalInteractionSystemStyle.h"
#include "UniversalInteractionSystemCommands.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"

static const FName UniversalInteractionSystemTabName("UniversalInteractionSystem");

#define LOCTEXT_NAMESPACE "FUniversalInteractionSystemModule"

void FUniversalInteractionSystemModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FUniversalInteractionSystemStyle::Initialize();
	FUniversalInteractionSystemStyle::ReloadTextures();

	FUniversalInteractionSystemCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FUniversalInteractionSystemCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FUniversalInteractionSystemModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUniversalInteractionSystemModule::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(UniversalInteractionSystemTabName, FOnSpawnTab::CreateRaw(this, &FUniversalInteractionSystemModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FUniversalInteractionSystemTabTitle", "UniversalInteractionSystem"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUniversalInteractionSystemModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FUniversalInteractionSystemStyle::Shutdown();

	FUniversalInteractionSystemCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UniversalInteractionSystemTabName);
}

TSharedRef<SDockTab> FUniversalInteractionSystemModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	FText WidgetText = FText::Format(
		LOCTEXT("WindowWidgetText", "Add code to {0} in {1} to override this window's contents"),
		FText::FromString(TEXT("FUniversalInteractionSystemModule::OnSpawnPluginTab")),
		FText::FromString(TEXT("UniversalInteractionSystem.cpp"))
		);

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			// Put your tab content here!
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(WidgetText)
			]
		];
}

void FUniversalInteractionSystemModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UniversalInteractionSystemTabName);
}

void FUniversalInteractionSystemModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FUniversalInteractionSystemCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUniversalInteractionSystemCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUniversalInteractionSystemModule, UniversalInteractionSystem)