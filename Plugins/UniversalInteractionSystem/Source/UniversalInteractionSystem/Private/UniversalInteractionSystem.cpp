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

// Forward declaration
void GenerateInteractableBlueprint(
    const FString& AssetName,
    const FString& PackagePath,
    UStaticMesh* SelectedMesh,
    TSharedRef<TMap<FName, TSharedRef<bool>>> FunctionCheckStates,
    const FString& ParentBlueprintName);

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
    TSharedRef<TMap<FName, TSharedRef<bool>>> FunctionCheckStates,
    const FString& ParentBlueprintName)
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
    UE_LOG(LogTemp, Log, TEXT("ParentBlueprintName: %s"), *ParentBlueprintName);

    // 1. Load the parent Blueprint using Asset Registry
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
    TArray<FAssetData> AssetList;
    AssetRegistry.GetAssetsByPath(FName("/UniversalInteractionSystem"), AssetList, true);

    UBlueprint* BaseBP = nullptr;
    for (const FAssetData& Asset : AssetList)
    {
        if (Asset.AssetName == FName(*ParentBlueprintName))
        {
            BaseBP = Cast<UBlueprint>(Asset.GetAsset());
            break;
        }
    }

    if (!BaseBP)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Could not find parent blueprint '%s' in plugin content"), *ParentBlueprintName);
        ShowNotification(FString::Printf(TEXT("Parent blueprint '%s' not found"), *ParentBlueprintName), false);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Loaded parent blueprint: %s"), *BaseBP->GetName());

    UClass* ParentClass = BaseBP->GeneratedClass;
    if (!ParentClass)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Parent blueprint has no GeneratedClass"));
        ShowNotification(TEXT("Parent blueprint has no GeneratedClass"), false);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Parent class: %s"), *ParentClass->GetName());

    // 2. Validate package path
    if (PackagePath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Output folder path is empty"));
        ShowNotification(TEXT("Output folder path is empty"), false);
        return;
    }

    // 3. Create package
    FString FullPackagePath = PackagePath + TEXT("/") + AssetName;
    UE_LOG(LogTemp, Log, TEXT("Creating package at: %s"), *FullPackagePath);
    UPackage* Package = CreatePackage(*FullPackagePath);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("FAILED: Could not create package"));
        ShowNotification(TEXT("Failed to create package"), false);
        return;
    }

    // 4. Create the Blueprint asset using BlueprintFactory
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

    // 5. Add Static Mesh Component to SimpleConstructionScript
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

    // 6. Load interface class using Asset Registry and implement it
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
            FBlueprintEditorUtils::ImplementNewInterface(NewBP, FTopLevelAssetPath(InterfaceClass));
            UE_LOG(LogTemp, Log, TEXT("Interface implemented"));
        }

        // 7. Add event nodes for each checked function
        bool bAnyChecked = false;
        for (const auto& Pair : *FunctionCheckStates)
        {
            if (*Pair.Value)
            {
                bAnyChecked = true;
                break;
            }
        }

        if (bAnyChecked)
        {
            UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(NewBP);
            if (EventGraph)
            {
                UE_LOG(LogTemp, Log, TEXT("Found event graph, adding event nodes..."));
                int32 NodePosY = 200;
                for (const auto& Pair : *FunctionCheckStates)
                {
                    if (*Pair.Value)
                    {
                        FName FunctionName = Pair.Key;
                        UFunction* Func = InterfaceClass->FindFunctionByName(FunctionName);
                        if (!Func) continue;

                        UK2Node_Event* EventNode = FKismetEditorUtilities::AddDefaultEventNode(
                            NewBP, EventGraph, FunctionName, InterfaceClass, NodePosY);
                        if (EventNode)
                        {
                            EventNode->NodePosX = 200;
                            NodePosY += 150;
                            UE_LOG(LogTemp, Log, TEXT("  Added event node: %s"), *FunctionName.ToString());
                        }
                    }
                }
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

    // 8. Compile and save
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
        // Legacy default bools (still used for quick lookup in ApplyTemplateDefaults)
        bool bDefaultPre;
        bool bDefaultReceive;
        bool bDefaultPost;
    };

    TSharedRef<TArray<TSharedPtr<FInteractableTemplate>>> TemplateOptions = MakeShared<TArray<TSharedPtr<FInteractableTemplate>>>();
    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{ TEXT("Custom"), TEXT("UIS_BPP_InteractActor"), false, false, false }));
    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{ TEXT("Door"), TEXT("UIS_BPC_Door"), true, true, false }));
    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{ TEXT("Dialogue"), TEXT("UIS_BPC_Dialogue"), false, true, true }));

    // ----- Persistent State Storage -----
    TSharedRef<TSharedPtr<FInteractableTemplate>> SelectedTemplate = MakeShared<TSharedPtr<FInteractableTemplate>>((*TemplateOptions)[0]);

    // Map from function name to its checked state
    TSharedRef<TMap<FName, TSharedRef<bool>>> FunctionCheckStates = MakeShared<TMap<FName, TSharedRef<bool>>>();

    TSharedRef<FAssetData> SelectedMeshAsset = MakeShared<FAssetData>();
    TSharedRef<FString> OutputFolderPath = MakeShared<FString>(TEXT("/Game"));
    TSharedRef<FString> AssetName = MakeShared<FString>(TEXT("NewInteractable"));

    // ----- Load interface and discover functions -----
    UClass* InterfaceClass = nullptr;
    TArray<FName> InterfaceFunctionNames;

    {
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
        TArray<FAssetData> AssetList;
        AssetRegistry.GetAssetsByPath(FName("/UniversalInteractionSystem"), AssetList, true);
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
            for (TFieldIterator<UFunction> FuncIt(InterfaceClass); FuncIt; ++FuncIt)
            {
                UFunction* Func = *FuncIt;
                FName FuncName = Func->GetFName();
                // Skip the internal ExecuteUbergraph function
                if (FuncName.ToString().Contains(TEXT("ExecuteUbergraph")))
                {
                    continue;
                }
                if (Func->HasAnyFunctionFlags(FUNC_BlueprintEvent) && !Func->HasAnyFunctionFlags(FUNC_Delegate))
                {
                    InterfaceFunctionNames.Add(FuncName);
                }
            }
        }
    }

    // Initialize check states (all false initially)
    for (const FName& FuncName : InterfaceFunctionNames)
    {
        FunctionCheckStates->Add(FuncName, MakeShared<bool>(false));
    }

    // Helper to apply template defaults based on template display name
    auto ApplyTemplateDefaults = [FunctionCheckStates, InterfaceFunctionNames](TSharedPtr<FInteractableTemplate> Template)
        {
            // Reset all to false
            for (const FName& FuncName : InterfaceFunctionNames)
            {
                *(*FunctionCheckStates)[FuncName] = false;
            }

            if (!Template.IsValid())
            {
                return;
            }

            FString TemplateName = Template->DisplayName;
            if (TemplateName == TEXT("Custom"))
            {
                // All false (already done)
            }
            else if (TemplateName == TEXT("Door"))
            {
                if (auto* Ptr = FunctionCheckStates->Find(FName("PreInteract")))
                    *(*Ptr) = true;
                if (auto* Ptr = FunctionCheckStates->Find(FName("ReceiveInteract")))
                    *(*Ptr) = true;
            }
            else if (TemplateName == TEXT("Dialogue"))
            {
                if (auto* Ptr = FunctionCheckStates->Find(FName("ReceiveInteract")))
                    *(*Ptr) = true;
                if (auto* Ptr = FunctionCheckStates->Find(FName("PostInteract")))
                    *(*Ptr) = true;
            }
        };

    // Apply initial template defaults
    ApplyTemplateDefaults(*SelectedTemplate);

    // Build the checkbox container widget dynamically
    TSharedRef<SVerticalBox> CheckboxContainer = SNew(SVerticalBox);
    for (const FName& FuncName : InterfaceFunctionNames)
    {
        TSharedRef<bool> CheckState = (*FunctionCheckStates)[FuncName];
        CheckboxContainer->AddSlot()
            .AutoHeight()
            .Padding(2)
            [
                SNew(SCheckBox)
                    .IsChecked_Lambda([CheckState]() -> ECheckBoxState
                        {
                            return *CheckState ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        })
                    .OnCheckStateChanged_Lambda([CheckState](ECheckBoxState NewState)
                        {
                            *CheckState = (NewState == ECheckBoxState::Checked);
                        })
                    [
                        SNew(STextBlock).Text(FText::FromName(FuncName))
                    ]
            ];
    }

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
                        .OnSelectionChanged_Lambda([SelectedTemplate, ApplyTemplateDefaults](TSharedPtr<FInteractableTemplate> NewSelection, ESelectInfo::Type)
                            {
                                if (NewSelection.IsValid())
                                {
                                    *SelectedTemplate = NewSelection;
                                    ApplyTemplateDefaults(NewSelection);
                                    UE_LOG(LogTemp, Log, TEXT("Template changed to: %s"), *NewSelection->DisplayName);
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

                // ---- Dynamic Checkboxes Container ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5, 0, 5, 0)
                [
                    CheckboxContainer
                ]

                // ---- Output Folder Picker (with scrolling) ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Output Folder:")))
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(5)
                [
                    CreateContentBrowserFolderPicker(OutputFolderPath)
                ]

                // ---- Generate Button ----
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(10)
                .HAlign(HAlign_Center)
                [
                    SNew(SButton)
                        .Text(FText::FromString(TEXT("Generate Blueprint")))
                        .OnClicked_Lambda([SelectedMeshAsset, AssetName, OutputFolderPath, FunctionCheckStates, SelectedTemplate, TemplateOptions]() -> FReply
                            {
                                FString NameStr = *AssetName;
                                FString FolderStr = *OutputFolderPath;

                                UE_LOG(LogTemp, Log, TEXT("=== Generate Blueprint Clicked ==="));
                                UE_LOG(LogTemp, Log, TEXT("Template: %s"), *(*SelectedTemplate)->DisplayName);
                                UE_LOG(LogTemp, Log, TEXT("ParentBlueprintName: %s"), *(*SelectedTemplate)->ParentBlueprintName);
                                UE_LOG(LogTemp, Log, TEXT("Name: %s, Folder: %s"), *NameStr, *FolderStr);

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
                                GenerateInteractableBlueprint(NameStr, FolderStr, Mesh, FunctionCheckStates, (*SelectedTemplate)->ParentBlueprintName);

                                // Reset to Custom template
                                *SelectedTemplate = (*TemplateOptions)[0];
                                *SelectedMeshAsset = FAssetData();
                                *AssetName = TEXT("NewInteractable");

                                // Reset checkboxes (we must re-apply the Custom defaults)
                                // Since we don't have ApplyTemplateDefaults captured here, we can just manually reset
                                // or we can capture a reset delegate. Simpler: reset all to false (Custom default).
                                for (auto& Pair : *FunctionCheckStates)
                                {
                                    *Pair.Value = false;
                                }

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