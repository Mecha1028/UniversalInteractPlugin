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
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "K2Node_Event.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

static const FName UniversalInteractionSystemTabName("UniversalInteractionSystem");

#define LOCTEXT_NAMESPACE "FUniversalInteractionSystemModule"

// Helper function to create a Content Browser folder picker widget
TSharedRef<SWidget> CreateContentBrowserFolderPicker(TSharedRef<FString> OutputFolderPath)
{
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

    FPathPickerConfig PathPickerConfig;
    PathPickerConfig.DefaultPath = *OutputFolderPath;
    PathPickerConfig.OnPathSelected = FOnPathSelected::CreateLambda([OutputFolderPath](const FString& FolderPath)
        {
            *OutputFolderPath = FolderPath;
            UE_LOG(LogTemp, Log, TEXT("Selected Content Folder: %s"), *FolderPath);
        });

    return ContentBrowser.CreatePathPicker(PathPickerConfig);
}

// Helper: Generate the final Blueprint
void GenerateInteractableBlueprint(
    const FString& AssetName,
    const FString& PackagePath,
    UStaticMesh* SelectedMesh,
    bool bPreInteract,
    bool bReceiveInteract,
    bool bPostInteract,
    const FString& InParentClassPath)
{
    // Helper lambda for notifications
    auto ShowNotification = [](const FString& Message, bool bSuccess = true)
        {
            FNotificationInfo Info(FText::FromString(Message));
            Info.ExpireDuration = 5.0f;
            Info.bUseSuccessFailIcons = true;
            TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
            NotificationItem->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
        };

    UE_LOG(LogTemp, Log, TEXT("=== Starting Blueprint Generation ==="));
    UE_LOG(LogTemp, Log, TEXT("AssetName: %s"), *AssetName);
    UE_LOG(LogTemp, Log, TEXT("PackagePath: %s"), *PackagePath);
    UE_LOG(LogTemp, Log, TEXT("Mesh: %s"), SelectedMesh ? *SelectedMesh->GetName() : TEXT("None"));

    // ------------------------------------------------------------------------
    // 1. Load base Blueprint using the Asset Registry (most reliable method)
    // ------------------------------------------------------------------------
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FAssetData> AssetList;
    AssetRegistry.GetAssetsByPath(FName("/UniversalInteractionSystem"), AssetList, true);

    UBlueprint* BaseBP = nullptr;
    for (const FAssetData& Asset : AssetList)
    {
        if (Asset.AssetName == FName("UIS_BPP_InteractActor"))
        {
            BaseBP = Cast<UBlueprint>(Asset.GetAsset());
            break;
        }
    }

    if (!BaseBP)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Could not find UIS_BPP_InteractActor in plugin folder"));
        ShowNotification(TEXT("Base Blueprint 'UIS_BPP_InteractActor' not found in plugin content."), false);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Loaded base blueprint: %s"), *BaseBP->GetName());

    UClass* ParentClass = BaseBP->GeneratedClass;
    if (!ParentClass)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Base blueprint has no GeneratedClass"));
        ShowNotification(TEXT("Base blueprint has no GeneratedClass"), false);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Parent class: %s"), *ParentClass->GetName());

    // ------------------------------------------------------------------------
    // 2. Validate package path
    // ------------------------------------------------------------------------
    if (PackagePath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Output folder path is empty"));
        ShowNotification(TEXT("Output folder path is empty"), false);
        return;
    }

    // ------------------------------------------------------------------------
    // 3. Create package
    // ------------------------------------------------------------------------
    FString FullPackagePath = PackagePath + TEXT("/") + AssetName;
    UE_LOG(LogTemp, Log, TEXT("Creating package at: %s"), *FullPackagePath);
    UPackage* Package = CreatePackage(*FullPackagePath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Could not create package"));
        ShowNotification(TEXT("Failed to create package"), false);
        return;
    }

    // ------------------------------------------------------------------------
    // 4. Create the Blueprint asset using BlueprintFactory
    // ------------------------------------------------------------------------
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    Factory->ParentClass = ParentClass;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UObject* NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
    UBlueprint* NewBP = Cast<UBlueprint>(NewAsset);

    if (!NewBP)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: CreateAsset returned null or not a Blueprint"));
        ShowNotification(TEXT("Failed to create Blueprint asset"), false);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Blueprint asset created successfully"));

    // ------------------------------------------------------------------------
    // 5. Add Static Mesh Component to SimpleConstructionScript
    // ------------------------------------------------------------------------
    if (SelectedMesh)
    {
        UE_LOG(LogTemp, Log, TEXT("Adding StaticMeshComponent with mesh: %s"), *SelectedMesh->GetName());
        USimpleConstructionScript* SCS = NewBP->SimpleConstructionScript;
        if (!SCS)
        {
            UE_LOG(LogTemp, Warning, TEXT("SimpleConstructionScript is null, creating new one"));
            NewBP->SimpleConstructionScript = NewObject<USimpleConstructionScript>(NewBP);
            SCS = NewBP->SimpleConstructionScript;
        }
        USCS_Node* NewNode = SCS->CreateNode(UStaticMeshComponent::StaticClass(), FName(TEXT("StaticMeshComponent")));
        if (NewNode)
        {
            SCS->AddNode(NewNode);
            if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(NewNode->ComponentTemplate))
            {
                SMC->SetStaticMesh(SelectedMesh);
                UE_LOG(LogTemp, Log, TEXT("StaticMeshComponent added and mesh set"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to create SCS node for StaticMeshComponent"));
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("No mesh selected, skipping StaticMeshComponent"));
    }

    // ------------------------------------------------------------------------
    // 6. Load interface class using Asset Registry and implement it
    // ------------------------------------------------------------------------
    UClass* InterfaceClass = nullptr;
    for (const FAssetData& Asset : AssetList)
    {
        if (Asset.AssetName == FName("UIS_BPI_InteractInterface"))
        {
            FString ClassPath = Asset.GetObjectPathString() + TEXT("_C");
            InterfaceClass = LoadObject<UClass>(nullptr, *ClassPath);
            break;
        }
    }

    if (InterfaceClass)
    {
        UE_LOG(LogTemp, Log, TEXT("Loaded interface: %s"), *InterfaceClass->GetName());
        if (!NewBP->GeneratedClass->ImplementsInterface(InterfaceClass))
        {
            FBlueprintEditorUtils::ImplementNewInterface(NewBP, InterfaceClass->GetFName());
            UE_LOG(LogTemp, Log, TEXT("Interface implemented"));
        }

        // 7. Add event nodes to the event graph
        if (bPreInteract || bReceiveInteract || bPostInteract)
        {
            UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(NewBP);
            if (EventGraph)
            {
                UE_LOG(LogTemp, Log, TEXT("Found event graph, adding event nodes..."));
                int32 NodePosY = 200;
                auto AddEventNode = [&](const FName& FunctionName)
                    {
                        UFunction* Func = InterfaceClass->FindFunctionByName(FunctionName);
                        if (!Func) return;
                        UK2Node_Event* EventNode = FKismetEditorUtilities::AddDefaultEventNode(
                            NewBP, EventGraph, FunctionName, InterfaceClass, NodePosY);
                        if (EventNode)
                        {
                            EventNode->NodePosX = 200;
                            NodePosY += 150;
                            UE_LOG(LogTemp, Log, TEXT("  Added event node: %s"), *FunctionName.ToString());
                        }
                    };
                if (bPreInteract) AddEventNode(TEXT("PreInteract"));
                if (bReceiveInteract) AddEventNode(TEXT("ReceiveInteract"));
                if (bPostInteract) AddEventNode(TEXT("PostInteract"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("No event graph found"));
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Could not find interface UIS_BPI_InteractInterface"));
        ShowNotification(TEXT("Failed to load interface - event nodes not added"), false);
    }

    // ------------------------------------------------------------------------
    // 8. Compile and save the Blueprint
    // ------------------------------------------------------------------------
    UE_LOG(LogTemp, Log, TEXT("Compiling blueprint..."));
    FKismetEditorUtilities::CompileBlueprint(NewBP);

    FAssetRegistryModule::AssetCreated(NewBP);
    NewBP->MarkPackageDirty();

    FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    bool bSaved = UPackage::SavePackage(Package, NewBP, *PackageFilename, SaveArgs);
    if (bSaved)
    {
        UE_LOG(LogTemp, Log, TEXT("SUCCESS: Blueprint saved to %s"), *FullPackagePath);
        ShowNotification(FString::Printf(TEXT("Blueprint created: %s"), *AssetName), true);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Could not save package"));
        ShowNotification(TEXT("Failed to save Blueprint"), false);
    }
}

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
    // ----- Template Definitions -----
    struct FInteractableTemplate
    {
        FString DisplayName;
        FString ParentBlueprintName;
        bool bDefaultPre;
        bool bDefaultReceive;
        bool bDefaultPost;
    };

    TSharedRef<TArray<TSharedPtr<FInteractableTemplate>>> TemplateOptions = MakeShared<TArray<TSharedPtr<FInteractableTemplate>>>();
    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{ TEXT("Custom"), TEXT("UIS_BPP_InteractActor"), false, true, false }));
    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{ TEXT("Door"), TEXT("UIS_BPC_Door"), true, true, false }));
    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{ TEXT("NPC Dialogue"), TEXT("UIS_BPC_Dialogue"), false, true, true }));

    // ----- Persistent State Storage -----
    TSharedRef<TSharedPtr<FInteractableTemplate>> SelectedTemplate = MakeShared<TSharedPtr<FInteractableTemplate>>((*TemplateOptions)[0]);
    TSharedRef<FString> ParentClassPath = MakeShared<FString>();
    *ParentClassPath = FString::Printf(TEXT("/UniversalInteractionSystem/%s.%s_C"), *(*SelectedTemplate)->ParentBlueprintName, *(*SelectedTemplate)->ParentBlueprintName);

    TSharedRef<bool> bPreInteract = MakeShared<bool>((*SelectedTemplate)->bDefaultPre);
    TSharedRef<bool> bReceiveInteract = MakeShared<bool>((*SelectedTemplate)->bDefaultReceive);
    TSharedRef<bool> bPostInteract = MakeShared<bool>((*SelectedTemplate)->bDefaultPost);

    TSharedRef<FAssetData> SelectedMeshAsset = MakeShared<FAssetData>();
    TSharedRef<FString> OutputFolderPath = MakeShared<FString>(TEXT("/Game"));
    TSharedRef<FString> AssetName = MakeShared<FString>(TEXT("NewInteractable"));

    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SVerticalBox)

                // ---- Template Selector ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Template:")))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(SComboBox<TSharedPtr<FInteractableTemplate>>)
                        .OptionsSource(&TemplateOptions.Get())
                        .InitiallySelectedItem(*SelectedTemplate)
                        .OnGenerateWidget_Lambda([](TSharedPtr<FInteractableTemplate> Item)
                            {
                                return SNew(STextBlock).Text(FText::FromString(Item->DisplayName));
                            })
                        .OnSelectionChanged_Lambda([SelectedTemplate, bPreInteract, bReceiveInteract, bPostInteract, ParentClassPath](TSharedPtr<FInteractableTemplate> NewSelection, ESelectInfo::Type)
                            {
                                if (NewSelection.IsValid())
                                {
                                    *SelectedTemplate = NewSelection;
                                    *bPreInteract = NewSelection->bDefaultPre;
                                    *bReceiveInteract = NewSelection->bDefaultReceive;
                                    *bPostInteract = NewSelection->bDefaultPost;
                                    *ParentClassPath = FString::Printf(TEXT("/UniversalInteractionSystem/%s.%s_C"), *NewSelection->ParentBlueprintName, *NewSelection->ParentBlueprintName);
                                }
                            })
                        [
                            SNew(STextBlock)
                                .Text_Lambda([SelectedTemplate]() -> FText
                                    {
                                        return (*SelectedTemplate).IsValid() ? FText::FromString((*SelectedTemplate)->DisplayName) : FText::GetEmpty();
                                    })
                        ]
                ]

            // ---- Static Mesh Picker (with proper display) ----
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
                        .ObjectPath_Lambda([SelectedMeshAsset]() -> FString
                            {
                                return SelectedMeshAsset->IsValid() ? SelectedMeshAsset->GetObjectPathString() : FString();
                            })
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

            // ---- Output Folder Picker (Content Browser Style) ----
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
                    CreateContentBrowserFolderPicker(OutputFolderPath)
                ]

                // ---- Generate Button (with reset after success) ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(10)
                .HAlign(HAlign_Center)
                [
                    SNew(SButton)
                        .Text(FText::FromString(TEXT("Generate Blueprint")))
                        .OnClicked_Lambda([SelectedMeshAsset, AssetName, bPreInteract, bReceiveInteract, bPostInteract, OutputFolderPath, ParentClassPath, SelectedTemplate, TemplateOptions]() -> FReply
                            {
                                FString NameStr = *AssetName;
                                FString FolderStr = *OutputFolderPath;
                                bool bPre = *bPreInteract;
                                bool bReceive = *bReceiveInteract;
                                bool bPost = *bPostInteract;

                                UE_LOG(LogTemp, Log, TEXT("=== Generate Blueprint Clicked ==="));
                                UE_LOG(LogTemp, Log, TEXT("Template: %s"), *(*SelectedTemplate)->DisplayName);
                                UE_LOG(LogTemp, Log, TEXT("ParentClassPath: %s"), *(*ParentClassPath));
                                UE_LOG(LogTemp, Log, TEXT("Name: %s, Folder: %s"), *NameStr, *FolderStr);
                                UE_LOG(LogTemp, Log, TEXT("Events - Pre: %d, Receive: %d, Post: %d"), bPre, bReceive, bPost);

                                // Validate required fields
                                if (!SelectedMeshAsset->IsValid() || NameStr.IsEmpty() || FolderStr.IsEmpty())
                                {
                                    UE_LOG(LogTemp, Warning, TEXT("Missing required fields: Mesh, Name, or Folder."));
                                    FNotificationInfo Info(FText::FromString(TEXT("Please fill in all required fields (Mesh, Name, Folder).")));
                                    Info.ExpireDuration = 3.0f;
                                    Info.bUseSuccessFailIcons = true;
                                    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
                                    NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
                                    return FReply::Handled();
                                }

                                UStaticMesh* Mesh = Cast<UStaticMesh>(SelectedMeshAsset->GetAsset());
                                GenerateInteractableBlueprint(NameStr, FolderStr, Mesh, bPre, bReceive, bPost, *ParentClassPath);

                                // Reset form to defaults (but keep selected template? We'll reset to Custom)
                                *SelectedTemplate = (*TemplateOptions)[0]; // Custom
                                *ParentClassPath = FString::Printf(TEXT("/UniversalInteractionSystem/%s.%s_C"), *(*SelectedTemplate)->ParentBlueprintName, *(*SelectedTemplate)->ParentBlueprintName);
                                *SelectedMeshAsset = FAssetData();
                                *AssetName = TEXT("NewInteractable");
                                *bPreInteract = (*SelectedTemplate)->bDefaultPre;
                                *bReceiveInteract = (*SelectedTemplate)->bDefaultReceive;
                                *bPostInteract = (*SelectedTemplate)->bDefaultPost;

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