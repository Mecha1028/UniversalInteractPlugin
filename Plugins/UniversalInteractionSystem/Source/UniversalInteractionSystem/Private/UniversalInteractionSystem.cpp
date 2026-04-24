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

// Declaring function to help generate blueprints
void GenerateInteractableBlueprint(
    const FString& AssetName,
    const FString& PackagePath,
    UStaticMesh* SelectedMesh,
    TSharedRef<TMap<FName, TSharedRef<bool>>> FunctionCheckStates,
    const FString& ParentBlueprintName);

// Use content browser path picker to select output folder
TSharedRef<SWidget> CreateContentBrowserFolderPicker(TSharedRef<FString> OutputFolderPath)
{
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

    FPathPickerConfig PathPickerConfig;
    PathPickerConfig.DefaultPath = *OutputFolderPath;
    PathPickerConfig.OnPathSelected = FOnPathSelected::CreateLambda([OutputFolderPath](const FString& FolderPath)
        {
            *OutputFolderPath = FolderPath;
        });
    
    return ContentBrowser.CreatePathPicker(PathPickerConfig);
}

// Declaring function to generate child blueprints for custom and template interactables
void GenerateInteractableBlueprint(
    const FString& AssetName,
    const FString& PackagePath,
    UStaticMesh* SelectedMesh,
    TSharedRef<TMap<FName, TSharedRef<bool>>> FunctionCheckStates,
    const FString& ParentBlueprintName)
{
	// Display a notification in editor for success or failure of blueprint generation
    auto ShowNotification = [](const FString& Message, bool bSuccess = true)
        {
            FNotificationInfo Info(FText::FromString(Message));
            Info.ExpireDuration = 5.0f;
            Info.bUseSuccessFailIcons = true;
            TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
            NotificationItem->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
        };

    // Find parent blueprint class for blueprint generation
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
        ShowNotification(FString::Printf(TEXT("Parent blueprint '%s' not found"), *ParentBlueprintName), false);
        return;
    }

    UClass* ParentClass = BaseBP->GeneratedClass;
    if (!ParentClass)
    {
        ShowNotification(TEXT("Parent blueprint has no GeneratedClass"), false);
        return;
    }

    // Ensure output folder is valid and create package for the blueprint
    if (PackagePath.IsEmpty())
    {
        ShowNotification(TEXT("Output folder path is empty"), false);
        return;
    }

    FString FullPackagePath = PackagePath + TEXT("/") + AssetName;
    UPackage* Package = CreatePackage(*FullPackagePath);
    if (!Package)
    {
        ShowNotification(TEXT("Failed to create package"), false);
        return;
    }

	// Generate child blueprint using UblueprintFactory and AssetTools
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    Factory->ParentClass = ParentClass;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UObject* NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, PackagePath, UBlueprint::StaticClass(), Factory);
    UBlueprint* NewBP = Cast<UBlueprint>(NewAsset);

    if (!NewBP)
    {
        ShowNotification(TEXT("Failed to create Blueprint asset"), false);
        return;
    }

    // Add and set static mesh component to blueprint based on users selection
    if (SelectedMesh)
    {
        USimpleConstructionScript* SCS = NewBP->SimpleConstructionScript;
        if (!SCS)
        {
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
            }
        }
    }

    // Add UIS interface to blueprint for adding event nodes later
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
        if (!NewBP->GeneratedClass->ImplementsInterface(InterfaceClass))
        {
            FBlueprintEditorUtils::ImplementNewInterface(NewBP, FTopLevelAssetPath(InterfaceClass));
        }

        // Place event nodes in blueprint based on selected functions before generating blueprint
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
                        }
                    }
                }
            }
        }
    }
    else
    {
        ShowNotification(TEXT("Failed to load interface - event nodes not added"), false);
    }

    // Compile blueprint and save to selected folder
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
        ShowNotification(FString::Printf(TEXT("Blueprint created: %s"), *AssetName), true);
    }
    else
    {
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
        .SetDisplayName(LOCTEXT("FUniversalInteractionSystemTabTitle", "UIS Generator"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"))
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
    // Define templates and their default functions
    struct FInteractableTemplate
    {
        FString DisplayName;
        FString ParentBlueprintName;
        TMap<FName, bool> FunctionDefaults;
    };

    TSharedRef<TArray<TSharedPtr<FInteractableTemplate>>> TemplateOptions = MakeShared<TArray<TSharedPtr<FInteractableTemplate>>>();

    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{
        TEXT("Custom"),
        TEXT("UIS_BPP_InteractActor"),
        TMap<FName, bool> {}
	}));

    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{
        TEXT("Press Button"),
        TEXT("UIS_BPC_PressButton"),
        TMap<FName, bool> {
            {FName("PreInteract"), true},
            {FName("ReceiveInteract"), true},
            {FName("PostInteract"), true}
        }
    }));

    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{
        TEXT("Stand Button - Template Not Implemented"),
        TEXT("UIS_BPC_StandButton"),
        TMap<FName, bool> {}
    }));

    TemplateOptions->Add(MakeShared<FInteractableTemplate>(FInteractableTemplate{
        TEXT("Door"),
        TEXT("UIS_BPC_Door"),
        TMap<FName, bool> {
            {FName("ReceiveInteract"), true}
        }
    }));

    // Shared pointers to hold user selections
    TSharedRef<TSharedPtr<FInteractableTemplate>> SelectedTemplate = MakeShared<TSharedPtr<FInteractableTemplate>>((*TemplateOptions)[0]);

    TSharedRef<TMap<FName, TSharedRef<bool>>> FunctionCheckStates = MakeShared<TMap<FName, TSharedRef<bool>>>();

    TSharedRef<FAssetData> SelectedMeshAsset = MakeShared<FAssetData>();
    TSharedRef<FString> OutputFolderPath = MakeShared<FString>(TEXT("/Game"));
    TSharedRef<FString> AssetName = MakeShared<FString>(TEXT(""));

    // Load interface and get all functions and store their names for checkboxes later
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

    // Set all checkboxes to false by default
    for (const FName& FuncName : InterfaceFunctionNames)
    {
        FunctionCheckStates->Add(FuncName, MakeShared<bool>(false));
    }

    // Update checkboxes to template defaults when template is selected
    auto ApplyTemplateDefaults = [FunctionCheckStates](TSharedPtr<FInteractableTemplate> Template)
        {
            // Clear user selections before applying template defaults
            for (auto& Pair : *FunctionCheckStates)
            {
                *Pair.Value = false;
            }

            if (!Template.IsValid())
            {
                return;
            }

			// Set checkboxes based on template defaults
            for (const auto& FuncDefault : Template->FunctionDefaults)
            {
                if (auto* CheckStatePtr = FunctionCheckStates->Find(FuncDefault.Key))
                {
                    *(*CheckStatePtr) = FuncDefault.Value;
                }
            }
        };

    ApplyTemplateDefaults(*SelectedTemplate);

    // Generate checkbox per function in the interface if new functions are added later by user or developer
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

                // Template Selector
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

            // Static Mesh Picker
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

            // Asset Name Input
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

            // Interface Events Label 
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5)
                [
                    SNew(STextBlock)
                        .Text(FText::FromString(TEXT("Include Interface Events:")))
                ]

                // Dynamic Checkboxes Container 
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(5, 0, 5, 0)
                [
                    CheckboxContainer
                ]

                // Output Folder Picker 
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

                // Generate Button 
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

                                if (!SelectedMeshAsset->IsValid() || NameStr.IsEmpty() || FolderStr.IsEmpty())
                                {
                                    FNotificationInfo Info(FText::FromString(TEXT("Field(s) missing or invalid.")));
                                    Info.ExpireDuration = 3.0f;
                                    Info.bUseSuccessFailIcons = true;
                                    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
                                    NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
                                    return FReply::Handled();
                                }

                                UStaticMesh* Mesh = Cast<UStaticMesh>(SelectedMeshAsset->GetAsset());
                                GenerateInteractableBlueprint(NameStr, FolderStr, Mesh, FunctionCheckStates, (*SelectedTemplate)->ParentBlueprintName);

                                // Reset UI to default if user creates multiple blueprints at once
                                *SelectedTemplate = (*TemplateOptions)[0];
                                *SelectedMeshAsset = FAssetData();
                                *AssetName = TEXT("NewInteractable");

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

    // Add the plugin window to the Window menu
    {
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
        {
            FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
            Section.AddMenuEntryWithCommandList(FUniversalInteractionSystemCommands::Get().OpenPluginWindow, PluginCommands);
        }
    }

    // Add button to toolbar with differnt icon
    {
        UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
        {
            FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
            {
                FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
                    FUniversalInteractionSystemCommands::Get().OpenPluginWindow,
                    LOCTEXT("UniversalInteractionSystemLabel", "Universal Interaction System"),
                    LOCTEXT("UniversalInteractionSystemTooltip", "Generate UIS Blueprint"),
                    FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
                    NAME_None
                ));

                Entry.SetCommandList(PluginCommands);
            }
        }
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUniversalInteractionSystemModule, UniversalInteractionSystem)