// Copyright Epic Games, Inc. All Rights Reserved.

#include "UniversalInteractionSystem.h"
#include "UniversalInteractionSystemStyle.h"
#include "UniversalInteractionSystemCommands.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SDirectoryPicker.h"


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
    // ----- Persistent State Storage -----
    TSharedRef<bool> bPreInteract = MakeShared<bool>(false);
    TSharedRef<bool> bReceiveInteract = MakeShared<bool>(true);
    TSharedRef<bool> bPostInteract = MakeShared<bool>(false);

    TSharedRef<FAssetData> SelectedMeshAsset = MakeShared<FAssetData>();
    TSharedRef<FString> OutputFolderPath = MakeShared<FString>();
    TSharedRef<FString> AssetName = MakeShared<FString>(TEXT("NewInteractable"));

    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SVerticalBox)

                // ---- Static Mesh Picker ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Select Static Mesh:")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(SObjectPropertyEntryBox)
                        .AllowedClass(UStaticMesh::StaticClass())
                        .OnObjectChanged_Lambda([SelectedMeshAsset](const FAssetData& AssetData)
                            {
                                *SelectedMeshAsset = AssetData;
                            })
                ]

            // ---- Asset Name Input ----
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Blueprint Name:")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(SEditableTextBox)
                        .Text_Lambda([AssetName]() -> FText
                            {
                                return FText::FromString(*AssetName);
                            })
                        .OnTextChanged_Lambda([AssetName](const FText& NewText)
                            {
                                *AssetName = NewText.ToString();
                            })
                ]

            // ---- Interface Events Label ----
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Include Interface Events:")))
                ]

                // ---- PreInteract Checkbox ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5, 0, 5, 0)
                [
                    SNew(SCheckBox)
                        .IsChecked_Lambda([bPreInteract]() -> ECheckBoxState
                            {
                                return *bPreInteract ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                            })
                        .OnCheckStateChanged_Lambda([bPreInteract](ECheckBoxState NewState)
                            {
                                *bPreInteract = (NewState == ECheckBoxState::Checked);
                            })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("PreInteract")))
                        ]
                ]

            // ---- ReceiveInteract Checkbox ----
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5, 0, 5, 0)
                [
                    SNew(SCheckBox)
                        .IsChecked_Lambda([bReceiveInteract]() -> ECheckBoxState
                            {
                                return *bReceiveInteract ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                            })
                        .OnCheckStateChanged_Lambda([bReceiveInteract](ECheckBoxState NewState)
                            {
                                *bReceiveInteract = (NewState == ECheckBoxState::Checked);
                            })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("ReceiveInteract")))
                        ]
                ]

            // ---- PostInteract Checkbox ----
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5, 0, 5, 0)
                [
                    SNew(SCheckBox)
                        .IsChecked_Lambda([bPostInteract]() -> ECheckBoxState
                            {
                                return *bPostInteract ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                            })
                        .OnCheckStateChanged_Lambda([bPostInteract](ECheckBoxState NewState)
                            {
                                *bPostInteract = (NewState == ECheckBoxState::Checked);
                            })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("PostInteract")))
                        ]
                ]

            // ---- Output Folder Picker (Unreal's SDirectoryPicker) ----
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Output Folder:")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(SDirectoryPicker)
                        .Folder(OutputFolderPath.Get())
                        .OnDirectoryChanged_Lambda([OutputFolderPath](const FString& NewPath)
                            {
                                *OutputFolderPath = NewPath;
                            })
                ]

            // ---- Generate Button ----
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(10)
                .HAlign(HAlign_Center)
                [
                    SNew(SButton)
                        .Text(FText::FromString(TEXT("Generate Blueprint")))
                        .OnClicked_Lambda([SelectedMeshAsset, AssetName, bPreInteract, bReceiveInteract, bPostInteract, OutputFolderPath]() -> FReply
                            {
                                FString MeshName = SelectedMeshAsset->AssetName.ToString();
                                FString NameStr = *AssetName;
                                FString FolderStr = *OutputFolderPath;
                                bool bPre = *bPreInteract;
                                bool bReceive = *bReceiveInteract;
                                bool bPost = *bPostInteract;

                                UE_LOG(LogTemp, Log, TEXT("=== Generate Blueprint ==="));
                                UE_LOG(LogTemp, Log, TEXT("Mesh: %s"), *MeshName);
                                UE_LOG(LogTemp, Log, TEXT("Name: %s"), *NameStr);
                                UE_LOG(LogTemp, Log, TEXT("Folder: %s"), *FolderStr);
                                UE_LOG(LogTemp, Log, TEXT("PreInteract: %d"), bPre);
                                UE_LOG(LogTemp, Log, TEXT("ReceiveInteract: %d"), bReceive);
                                UE_LOG(LogTemp, Log, TEXT("PostInteract: %d"), bPost);
                                UE_LOG(LogTemp, Log, TEXT("Parent Class: BP_InteractableActor (hardcoded)"));

                                // TODO: Call Blueprint generation function

                                return FReply::Handled();
                            })
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