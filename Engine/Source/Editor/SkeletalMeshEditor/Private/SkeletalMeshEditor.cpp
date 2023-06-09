// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshEditor.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorReimportHandler.h"
#include "AssetData.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor/EditorEngine.h"
#include "EngineGlobals.h"
#include "ISkeletalMeshEditorModule.h"
#include "IPersonaToolkit.h"
#include "PersonaModule.h"
#include "SkeletalMeshEditorMode.h"
#include "IPersonaPreviewScene.h"
#include "SkeletalMeshEditorCommands.h"
#include "IDetailsView.h"
#include "ISkeletonTree.h"
#include "ISkeletonEditorModule.h"
#include "IAssetFamily.h"
#include "PersonaCommonCommands.h"
#include "EngineUtils.h"
#include "Rendering/SkeletalMeshModel.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "ClothingAsset.h"
#include "SCreateClothingSettingsPanel.h"
#include "ClothingSystemEditorInterfaceModule.h"
#include "Preferences/PersonaOptions.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "SkeletalMeshToolMenuContext.h"
#include "EditorViewportClient.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Algo/Transform.h"
#include "ISkeletonTreeItem.h"
#include "FbxMeshUtils.h"
#include "LODUtilities.h"

#include "ScopedTransaction.h"
#include "ComponentReregisterContext.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"

#include "Misc/MessageDialog.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/CoreMisc.h"

const FName SkeletalMeshEditorAppIdentifier = FName(TEXT("SkeletalMeshEditorApp"));

const FName SkeletalMeshEditorModes::SkeletalMeshEditorMode(TEXT("SkeletalMeshEditorMode"));

const FName SkeletalMeshEditorTabs::DetailsTab(TEXT("DetailsTab"));
const FName SkeletalMeshEditorTabs::SkeletonTreeTab(TEXT("SkeletonTreeView"));
const FName SkeletalMeshEditorTabs::AssetDetailsTab(TEXT("AnimAssetPropertiesTab"));
const FName SkeletalMeshEditorTabs::ViewportTab(TEXT("Viewport"));
const FName SkeletalMeshEditorTabs::AdvancedPreviewTab(TEXT("AdvancedPreviewTab"));
const FName SkeletalMeshEditorTabs::MorphTargetsTab("MorphTargetsTab");
const FName SkeletalMeshEditorTabs::AnimationMappingTab("AnimationMappingWindow");

DEFINE_LOG_CATEGORY(LogSkeletalMeshEditor);

#define LOCTEXT_NAMESPACE "SkeletalMeshEditor"

FSkeletalMeshEditor::FSkeletalMeshEditor()
{
	UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
	if (Editor != nullptr)
	{
		Editor->RegisterForUndo(this);
	}
}

FSkeletalMeshEditor::~FSkeletalMeshEditor()
{
	UEditorEngine* Editor = Cast<UEditorEngine>(GEngine);
	if (Editor != nullptr)
	{
		Editor->UnregisterForUndo(this);
	}
}

bool IsReductionParentBaseLODUseSkeletalMeshBuildWorkflow(USkeletalMesh* SkeletalMesh, int32 TestLODIndex)
{
	FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(TestLODIndex);
	if (LODInfo == nullptr || !SkeletalMesh->GetImportedModel() || !SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(TestLODIndex))
	{
		return false;
	}
	if (SkeletalMesh->IsLODImportedDataBuildAvailable(TestLODIndex))
	{
		return true;
	}
	if (LODInfo->bHasBeenSimplified || SkeletalMesh->IsReductionActive(TestLODIndex))
	{
		int32 ReduceBaseLOD = LODInfo->ReductionSettings.BaseLOD;
		if (ReduceBaseLOD < TestLODIndex)
		{
			return IsReductionParentBaseLODUseSkeletalMeshBuildWorkflow(SkeletalMesh, ReduceBaseLOD);
		}
	}
	return false;
}

bool FSkeletalMeshEditor::OnRequestClose()
{
	bool bAllowClose = true;

	if (PersonaToolkit.IsValid() && SkeletalMesh)
	{
		bool bHaveModifiedLOD = false;
		for (int32 LODIndex = 0; LODIndex < SkeletalMesh->GetLODNum(); ++LODIndex)
		{
			FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LODIndex);
			if (LODInfo == nullptr || !SkeletalMesh->GetImportedModel() || !SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex))
			{
				continue;
			}
			
			//Do not prevent exiting if we are not using the skeletal mesh build workflow
			if (!SkeletalMesh->IsLODImportedDataBuildAvailable(LODIndex))
			{
				if (!LODInfo->bHasBeenSimplified && !SkeletalMesh->IsReductionActive(LODIndex))
				{
					continue;
				}
				//Do not prevent exit if the generated LOD is base on a LODModel not using the skeletalmesh build workflow
				int32 ReduceBaseLOD = LODInfo->ReductionSettings.BaseLOD;
				if(!IsReductionParentBaseLODUseSkeletalMeshBuildWorkflow(SkeletalMesh, ReduceBaseLOD))
				{
					continue;
				}
				
			}

			bool bValidLODSettings = false;
			if (SkeletalMesh->GetLODSettings() != nullptr)
			{
				const int32 NumSettings = FMath::Min(SkeletalMesh->GetLODSettings()->GetNumberOfSettings(), SkeletalMesh->GetLODNum());
				if (LODIndex < NumSettings)
				{
					bValidLODSettings = true;
				}
			}
			const FSkeletalMeshLODGroupSettings* SkeletalMeshLODGroupSettings = bValidLODSettings ? &SkeletalMesh->GetLODSettings()->GetSettingsForLODLevel(LODIndex) : nullptr;

			FGuid BuildGUID = LODInfo->ComputeDeriveDataCacheKey(SkeletalMeshLODGroupSettings);
			if (LODInfo->BuildGUID != BuildGUID)
			{
				bHaveModifiedLOD = true;
				break;
			}
			FString BuildStringID = SkeletalMesh->GetImportedModel()->LODModels[LODIndex].GetLODModelDeriveDataKey();
			if (SkeletalMesh->GetImportedModel()->LODModels[LODIndex].BuildStringID != BuildStringID)
			{
				bHaveModifiedLOD = true;
				break;
			}
		}

		if (bHaveModifiedLOD)
		{
			// find out the user wants to do with this dirty material
			EAppReturnType::Type OkCancelReply = FMessageDialog::Open(
				EAppMsgType::OkCancel,
				FText::Format(LOCTEXT("SkeletalMeshEditorShouldApplyLODChanges", "We have to apply level of detail changes to {0} before exiting the skeletal mesh editor."), FText::FromString(PersonaToolkit->GetMesh()->GetName()))
			);

			switch (OkCancelReply)
			{
			case EAppReturnType::Ok:
				{
					SkeletalMesh->MarkPackageDirty();
					SkeletalMesh->PostEditChange();
					bAllowClose = true;
				}
				break;
			case EAppReturnType::Cancel:
				// Don't exit.
				bAllowClose = false;
				break;
			}
		}
	}
	return bAllowClose;
}

void FSkeletalMeshEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_SkeletalMeshEditor", "Skeletal Mesh Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

void FSkeletalMeshEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

void FSkeletalMeshEditor::InitSkeletalMeshEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USkeletalMesh* InSkeletalMesh)
{
	SkeletalMesh = InSkeletalMesh;

	FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
	PersonaToolkit = PersonaModule.CreatePersonaToolkit(InSkeletalMesh);

	PersonaToolkit->GetPreviewScene()->SetDefaultAnimationMode(EPreviewSceneDefaultAnimationMode::ReferencePose);

	TSharedRef<IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(InSkeletalMesh);
	AssetFamily->RecordAssetOpened(FAssetData(InSkeletalMesh));

	TSharedPtr<IPersonaPreviewScene> PreviewScene = PersonaToolkit->GetPreviewScene();

	FSkeletonTreeArgs SkeletonTreeArgs;
	SkeletonTreeArgs.OnSelectionChanged = FOnSkeletonTreeSelectionChanged::CreateSP(this, &FSkeletalMeshEditor::HandleSelectionChanged);
	SkeletonTreeArgs.PreviewScene = PreviewScene;
	SkeletonTreeArgs.ContextName = GetToolkitFName();

	ISkeletonEditorModule& SkeletonEditorModule = FModuleManager::GetModuleChecked<ISkeletonEditorModule>("SkeletonEditor");
	SkeletonTree = SkeletonEditorModule.CreateSkeletonTree(PersonaToolkit->GetSkeleton(), SkeletonTreeArgs);

	BindCommands();
	RegisterToolbar();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, SkeletalMeshEditorAppIdentifier, FTabManager::FLayout::NullLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InSkeletalMesh);

	AddApplicationMode(
		SkeletalMeshEditorModes::SkeletalMeshEditorMode,
		MakeShareable(new FSkeletalMeshEditorMode(SharedThis(this), SkeletonTree.ToSharedRef())));

	SetCurrentMode(SkeletalMeshEditorModes::SkeletalMeshEditorMode);

	ExtendMenu();
	ExtendToolbar();
	RegenerateMenusAndToolbars();

	// Set up mesh click selection
	PreviewScene->RegisterOnMeshClick(FOnMeshClick::CreateSP(this, &FSkeletalMeshEditor::HandleMeshClick));
	PreviewScene->SetAllowMeshHitProxies(GetDefault<UPersonaOptions>()->bAllowMeshSectionSelection);
}

FName FSkeletalMeshEditor::GetToolkitFName() const
{
	return FName("SkeletalMeshEditor");
}

FText FSkeletalMeshEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "SkeletalMeshEditor");
}

FString FSkeletalMeshEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "SkeletalMeshEditor ").ToString();
}

FLinearColor FSkeletalMeshEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

void FSkeletalMeshEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SkeletalMesh);
}

void FSkeletalMeshEditor::BindCommands()
{
	FSkeletalMeshEditorCommands::Register();

	ToolkitCommands->MapAction(FSkeletalMeshEditorCommands::Get().ReimportMesh,
		FExecuteAction::CreateSP(this, &FSkeletalMeshEditor::HandleReimportMesh, (int32)INDEX_NONE));

	ToolkitCommands->MapAction(FSkeletalMeshEditorCommands::Get().ReimportAllMesh,
		FExecuteAction::CreateSP(this, &FSkeletalMeshEditor::HandleReimportAllMesh, (int32)INDEX_NONE));

	ToolkitCommands->MapAction(FSkeletalMeshEditorCommands::Get().MeshSectionSelection,
		FExecuteAction::CreateSP(this, &FSkeletalMeshEditor::ToggleMeshSectionSelection),
		FCanExecuteAction(), 
		FIsActionChecked::CreateSP(this, &FSkeletalMeshEditor::IsMeshSectionSelectionChecked));

	ToolkitCommands->MapAction(FPersonaCommonCommands::Get().TogglePlay,
		FExecuteAction::CreateRaw(&GetPersonaToolkit()->GetPreviewScene().Get(), &IPersonaPreviewScene::TogglePlayback));
}

TSharedPtr<FSkeletalMeshEditor> FSkeletalMeshEditor::GetSkeletalMeshEditor(const FToolMenuContext& InMenuContext)
{
	if (USkeletalMeshToolMenuContext* Context = InMenuContext.FindContext<USkeletalMeshToolMenuContext>())
	{
		if (Context->SkeletalMeshEditor.IsValid())
		{
			return StaticCastSharedPtr<FSkeletalMeshEditor>(Context->SkeletalMeshEditor.Pin());
		}
	}

	return TSharedPtr<FSkeletalMeshEditor>();
}

void FSkeletalMeshEditor::RegisterReimportContextMenu(const FName InBaseMenuName)
{
	static auto ReimportMeshWithNewFileAction = [](const FToolMenuContext& InMenuContext, int32 SourceFileIndex)
	{
		TSharedPtr<FSkeletalMeshEditor> SkeletalMeshEditor = GetSkeletalMeshEditor(InMenuContext);
		if (SkeletalMeshEditor.IsValid())
		{
			if (USkeletalMesh* FoundSkeletalMesh = SkeletalMeshEditor->SkeletalMesh)
			{
				UFbxSkeletalMeshImportData* SkeletalMeshImportData = Cast<UFbxSkeletalMeshImportData>(FoundSkeletalMesh->GetAssetImportData());
				if (SkeletalMeshImportData)
				{
					SkeletalMeshImportData->ImportContentType = SourceFileIndex == 0 ? EFBXImportContentType::FBXICT_All : SourceFileIndex == 1 ? EFBXImportContentType::FBXICT_Geometry : EFBXImportContentType::FBXICT_SkinningWeights;
					SkeletalMeshEditor->HandleReimportMeshWithNewFile(SourceFileIndex);
				}
			}
		}
	};

	static auto CreateMultiContentSubMenu = [](UToolMenu* InMenu)
	{
		FToolMenuSection& Section = InMenu->AddSection("Reimport");

		TSharedPtr<FSkeletalMeshEditor> SkeletalMeshEditor = GetSkeletalMeshEditor(InMenu->Context);
		if (SkeletalMeshEditor.IsValid())
		{
			Section.AddMenuEntry(
				"ReimportGeometryContentLabel",
				LOCTEXT("ReimportGeometryContentLabel", "Geometry"),
				LOCTEXT("ReimportGeometryContentLabelTooltipTooltip", "Reimport Geometry Only"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
				FToolMenuExecuteAction::CreateLambda(ReimportMeshWithNewFileAction, 1)
			);

			Section.AddMenuEntry(
				"ReimportSkinningAndWeightsContentLabel",
				LOCTEXT("ReimportSkinningAndWeightsContentLabel", "Skinning And Weights"),
				LOCTEXT("ReimportSkinningAndWeightsContentLabelTooltipTooltip", "Reimport Skinning And Weights Only"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
				FToolMenuExecuteAction::CreateLambda(ReimportMeshWithNewFileAction, 2)
			);
		}
	};

	static auto ReimportAction = [](const FToolMenuContext& InMenuContext, const int32 SourceFileIndex, const bool bReimportAll, const bool bWithNewFile)
	{
		TSharedPtr<FSkeletalMeshEditor> SkeletalMeshEditor = GetSkeletalMeshEditor(InMenuContext);
		if (SkeletalMeshEditor.IsValid())
		{
			if (USkeletalMesh* FoundSkeletalMesh = SkeletalMeshEditor->SkeletalMesh)
			{
				UFbxSkeletalMeshImportData* SkeletalMeshImportData = FoundSkeletalMesh ? Cast<UFbxSkeletalMeshImportData>(FoundSkeletalMesh->GetAssetImportData()) : nullptr;
				if (SkeletalMeshImportData)
				{
					SkeletalMeshImportData->ImportContentType = SourceFileIndex == 0 ? EFBXImportContentType::FBXICT_All : SourceFileIndex == 1 ? EFBXImportContentType::FBXICT_Geometry : EFBXImportContentType::FBXICT_SkinningWeights;
					if (bReimportAll)
					{
						if (bWithNewFile)
						{
							SkeletalMeshEditor->HandleReimportAllMeshWithNewFile(SourceFileIndex);
						}
						else
						{
							SkeletalMeshEditor->HandleReimportAllMesh(SourceFileIndex);
						}
					}
					else
					{
						if (bWithNewFile)
						{
							SkeletalMeshEditor->HandleReimportMeshWithNewFile(SourceFileIndex);
						}
						else
						{
							SkeletalMeshEditor->HandleReimportMesh(SourceFileIndex);
						}
					}
				}
			}
		}
	};

	static auto CreateReimportSubMenu = [](UToolMenu* InMenu, bool bReimportAll, bool bWithNewFile)
	{
		TSharedPtr<FSkeletalMeshEditor> SkeletalMeshEditor = GetSkeletalMeshEditor(InMenu->Context);
		if (SkeletalMeshEditor.IsValid())
		{
			USkeletalMesh* InSkeletalMesh = SkeletalMeshEditor->SkeletalMesh;
			if (InSkeletalMesh && InSkeletalMesh->GetAssetImportData())
			{
				//Get the data
				TArray<FString> SourceFilePaths;
				InSkeletalMesh->GetAssetImportData()->ExtractFilenames(SourceFilePaths);
				TArray<FString> SourceFileLabels;
				InSkeletalMesh->GetAssetImportData()->ExtractDisplayLabels(SourceFileLabels);

				if (SourceFileLabels.Num() > 0 && SourceFileLabels.Num() == SourceFilePaths.Num())
				{
					FToolMenuSection& Section = InMenu->AddSection("Reimport");
					for (int32 SourceFileIndex = 0; SourceFileIndex < SourceFileLabels.Num(); ++SourceFileIndex)
					{
						FText ReimportLabel = FText::Format(LOCTEXT("ReimportNoLabel", "SourceFile {0}"), SourceFileIndex);
						FText ReimportLabelTooltip = FText::Format(LOCTEXT("ReimportNoLabelTooltip", "Reimport File: {0}"), FText::FromString(SourceFilePaths[SourceFileIndex]));
						if (SourceFileLabels[SourceFileIndex].Len() > 0)
						{
							ReimportLabel = FText::Format(LOCTEXT("ReimportLabel", "{0}"), FText::FromString(SourceFileLabels[SourceFileIndex]));
							ReimportLabelTooltip = FText::Format(LOCTEXT("ReimportLabelTooltip", "Reimport {0} File: {1}"), FText::FromString(SourceFileLabels[SourceFileIndex]), FText::FromString(SourceFilePaths[SourceFileIndex]));
						}

						Section.AddMenuEntry(
							NAME_None,
							ReimportLabel,
							ReimportLabelTooltip,
							FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.ReimportAsset"),
							FToolMenuExecuteAction::CreateLambda(ReimportAction, SourceFileIndex, bReimportAll, bWithNewFile)
						);
					}
				}
			}
		}
	};

	if (!UToolMenus::Get()->IsMenuRegistered(UToolMenus::JoinMenuPaths(InBaseMenuName, "ReimportContextMenu")))
	{
		UToolMenu* ToolMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "ReimportContextMenu"));
		ToolMenu->AddDynamicSection("Section", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			TSharedPtr<FSkeletalMeshEditor> SkeletalMeshEditor = GetSkeletalMeshEditor(InMenu->Context);
			if (SkeletalMeshEditor.IsValid())
			{
				USkeletalMesh* InSkeletalMesh = SkeletalMeshEditor->SkeletalMesh;
				bool bShowSubMenu = InSkeletalMesh != nullptr && InSkeletalMesh->GetAssetImportData() != nullptr && InSkeletalMesh->GetAssetImportData()->GetSourceFileCount() > 1;

				FToolMenuSection& Section = InMenu->AddSection("Section");
				if (!bShowSubMenu)
				{
					//Reimport
					Section.AddMenuEntry(
						FSkeletalMeshEditorCommands::Get().ReimportMesh->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportMesh->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportMesh->GetDescription(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateSP(SkeletalMeshEditor.ToSharedRef(), &FSkeletalMeshEditor::HandleReimportMesh, 0)));

					Section.AddMenuEntry(
						FSkeletalMeshEditorCommands::Get().ReimportMeshWithNewFile->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportMeshWithNewFile->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportMeshWithNewFile->GetDescription(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateSP(SkeletalMeshEditor.ToSharedRef(), &FSkeletalMeshEditor::HandleReimportMeshWithNewFile, 0)));

					//Reimport ALL
					Section.AddMenuEntry(
						FSkeletalMeshEditorCommands::Get().ReimportAllMesh->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMesh->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMesh->GetDescription(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateSP(SkeletalMeshEditor.ToSharedRef(), &FSkeletalMeshEditor::HandleReimportAllMesh, 0)));

					Section.AddMenuEntry(
						FSkeletalMeshEditorCommands::Get().ReimportAllMeshWithNewFile->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMeshWithNewFile->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMeshWithNewFile->GetDescription(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateSP(SkeletalMeshEditor.ToSharedRef(), &FSkeletalMeshEditor::HandleReimportAllMeshWithNewFile, 0)));

					Section.AddSubMenu(
						"ReimportMultiSources",
						LOCTEXT("ReimportMultiSources", "Reimport Content"),
						LOCTEXT("ReimportMultiSourcesTooltip", "Reimport Geometry or Skinning Weights content, this will create multi import source file."),
						FNewToolMenuDelegate::CreateLambda(CreateMultiContentSubMenu));
				}
				else
				{
					//Create 4 submenu: Reimport, ReimportWithNewFile, ReimportAll and ReimportAllWithNewFile
					Section.AddSubMenu(
						FSkeletalMeshEditorCommands::Get().ReimportMesh->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportMesh->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportMesh->GetDescription(),
						FNewToolMenuDelegate::CreateLambda(CreateReimportSubMenu, false, false));
			
					Section.AddSubMenu(
						FSkeletalMeshEditorCommands::Get().ReimportMeshWithNewFile->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportMeshWithNewFile->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportMeshWithNewFile->GetDescription(),
						FNewToolMenuDelegate::CreateLambda(CreateReimportSubMenu, false, true));

					Section.AddSubMenu(
						FSkeletalMeshEditorCommands::Get().ReimportAllMesh->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMesh->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMesh->GetDescription(),
						FNewToolMenuDelegate::CreateLambda(CreateReimportSubMenu, true, false));

					Section.AddSubMenu(
						FSkeletalMeshEditorCommands::Get().ReimportAllMeshWithNewFile->GetCommandName(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMeshWithNewFile->GetLabel(),
						FSkeletalMeshEditorCommands::Get().ReimportAllMeshWithNewFile->GetDescription(),
						FNewToolMenuDelegate::CreateLambda(CreateReimportSubMenu, true, true));
				}
			}
		}));
	}
}

void FSkeletalMeshEditor::ExtendToolbar()
{
	// If the ToolbarExtender is valid, remove it before rebuilding it
	if (ToolbarExtender.IsValid())
	{
		RemoveToolbarExtender(ToolbarExtender);
		ToolbarExtender.Reset();
	}

	ToolbarExtender = MakeShareable(new FExtender);

	AddToolbarExtender(ToolbarExtender);

	ISkeletalMeshEditorModule& SkeletalMeshEditorModule = FModuleManager::GetModuleChecked<ISkeletalMeshEditorModule>("SkeletalMeshEditor");
	AddToolbarExtender(SkeletalMeshEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	TArray<ISkeletalMeshEditorModule::FSkeletalMeshEditorToolbarExtender> ToolbarExtenderDelegates = SkeletalMeshEditorModule.GetAllSkeletalMeshEditorToolbarExtenders();

	for (auto& ToolbarExtenderDelegate : ToolbarExtenderDelegates)
	{
		if (ToolbarExtenderDelegate.IsBound())
		{
			AddToolbarExtender(ToolbarExtenderDelegate.Execute(GetToolkitCommands(), SharedThis(this)));
		}
	}

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ParentToolbarBuilder)
	{
		// Second toolbar on right side
		FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
		TSharedRef<class IAssetFamily> AssetFamily = PersonaModule.CreatePersonaAssetFamily(SkeletalMesh);
		AddToolbarWidget(PersonaModule.CreateAssetFamilyShortcutWidget(SharedThis(this), AssetFamily));
	}
	));
}

void FSkeletalMeshEditor::RegisterToolbar()
{
	static const FName MenuName = "AssetEditor.SkeletalMeshEditor.ToolBar";
	RegisterReimportContextMenu(MenuName);

	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* ToolMenu = UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);

		const FToolMenuInsert SectionInsertLocation("Asset", EToolMenuInsertType::After);

		{
			ToolMenu->AddDynamicSection("Persona", FNewToolBarDelegateLegacy::CreateLambda([](FToolBarBuilder& ToolbarBuilder, UToolMenu* InMenu)
			{
				TSharedPtr<FSkeletalMeshEditor> SkeletalMeshEditor = GetSkeletalMeshEditor(InMenu->Context);
				if (SkeletalMeshEditor.IsValid() && SkeletalMeshEditor->PersonaToolkit.IsValid())
				{
					FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");
					FPersonaModule::FCommonToolbarExtensionArgs Args;
					Args.bPreviewMesh = false;
					PersonaModule.AddCommonToolbarExtensions(ToolbarBuilder, SkeletalMeshEditor->PersonaToolkit.ToSharedRef(), Args);
				}
			}), SectionInsertLocation);
		}

		{
			FToolMenuSection& Section = ToolMenu->AddSection("Mesh", FText(), SectionInsertLocation);
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(FSkeletalMeshEditorCommands::Get().ReimportMesh));
			Section.AddEntry(FToolMenuEntry::InitComboButton("ReimportContextMenu", FUIAction(), FNewToolMenuDelegate()));
		}

		{
			FToolMenuSection& Section = ToolMenu->AddSection("SkeletalMesh", FText(), FToolMenuInsert("Mesh", EToolMenuInsertType::After));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(FSkeletalMeshEditorCommands::Get().MeshSectionSelection));
		}
	}
}

void FSkeletalMeshEditor::InitToolMenuContext(FToolMenuContext& MenuContext)
{
	FAssetEditorToolkit::InitToolMenuContext(MenuContext);

	USkeletalMeshToolMenuContext* Context = NewObject<USkeletalMeshToolMenuContext>();
	Context->SkeletalMeshEditor = SharedThis(this);
	MenuContext.AddObject(Context);
}

void FSkeletalMeshEditor::FillMeshClickMenu(FMenuBuilder& MenuBuilder, HActor* HitProxy, const FViewportClick& Click)
{
	UDebugSkelMeshComponent* MeshComp = GetPersonaToolkit()->GetPreviewMeshComponent();

	// Must have hit something, but if the preview is invalid, bail
	if(!MeshComp)
	{
		return;
	}

	const int32 LodIndex = MeshComp->GetPredictedLODLevel();
	const int32 SectionIndex = HitProxy->SectionIndex;

	TSharedRef<SWidget> InfoWidget = SNew(SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(2.5f, 5.0f, 2.5f, 0.0f))
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			//.Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.LabelFont"))
				.Text(FText::Format(LOCTEXT("MeshClickMenu_SectionInfo", "LOD{0} - Section {1}"), LodIndex, SectionIndex))
				]
			]
		];


	MenuBuilder.AddWidget(InfoWidget, FText::GetEmpty(), true, false);

	MenuBuilder.BeginSection(TEXT("MeshClickMenu_Asset"), LOCTEXT("MeshClickMenu_Section_Asset", "Asset"));
	{
		FUIAction Action;
		Action.CanExecuteAction = FCanExecuteAction::CreateSP(this, &FSkeletalMeshEditor::CanApplyClothing, LodIndex, SectionIndex);

		MenuBuilder.AddSubMenu(
			LOCTEXT("MeshClickMenu_AssetApplyMenu", "Apply Clothing Data..."),
			LOCTEXT("MeshClickMenu_AssetApplyMenu_ToolTip", "Select clothing data to apply to the selected section."),
			FNewMenuDelegate::CreateSP(this, &FSkeletalMeshEditor::FillApplyClothingAssetMenu, LodIndex, SectionIndex),
			Action,
			TEXT(""),
			EUserInterfaceActionType::Button
			);

		Action.ExecuteAction = FExecuteAction::CreateSP(this, &FSkeletalMeshEditor::OnRemoveClothingAssetMenuItemClicked, LodIndex, SectionIndex);
		Action.CanExecuteAction = FCanExecuteAction::CreateSP(this, &FSkeletalMeshEditor::CanRemoveClothing, LodIndex, SectionIndex);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("MeshClickMenu_RemoveClothing", "Remove Clothing Data"),
			LOCTEXT("MeshClickMenu_RemoveClothing_ToolTip", "Remove the currently assigned clothing data."),
			FSlateIcon(),
			Action
			);
			
		Action.ExecuteAction = FExecuteAction();
		Action.CanExecuteAction = FCanExecuteAction::CreateSP(this, &FSkeletalMeshEditor::CanCreateClothing, LodIndex, SectionIndex);

		MenuBuilder.AddSubMenu(
			LOCTEXT("MeshClickMenu_CreateClothing_Label", "Create Clothing Data from Section"),
			LOCTEXT("MeshClickMenu_CreateClothing_ToolTip", "Create a new clothing data using the selected section as a simulation mesh"),
			FNewMenuDelegate::CreateSP(this, &FSkeletalMeshEditor::FillCreateClothingMenu, LodIndex, SectionIndex),
			Action,
			TEXT(""),
			EUserInterfaceActionType::Button
			);

		Action.CanExecuteAction = FCanExecuteAction::CreateSP(this, &FSkeletalMeshEditor::CanCreateClothingLod, LodIndex, SectionIndex);

		MenuBuilder.AddSubMenu(
			LOCTEXT("MeshClickMenu_CreateClothingNewLod_Label", "Create Clothing LOD from Section"),
			LOCTEXT("MeshClickMenu_CreateClothingNewLod_ToolTip", "Create a clothing simulation mesh from the selected section and add it as a LOD to existing clothing data."),
			FNewMenuDelegate::CreateSP(this, &FSkeletalMeshEditor::FillCreateClothingLodMenu, LodIndex, SectionIndex),
			Action,
			TEXT(""),
			EUserInterfaceActionType::Button
		);


		if (SkeletalMesh != nullptr && SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LodIndex))
		{
			const FSkeletalMeshLODInfo* SkeletalMeshLODInfo = SkeletalMesh->GetLODInfo(LodIndex);
			if (SkeletalMeshLODInfo != nullptr)
			{
				FUIAction ActionRemoveSection;
				ActionRemoveSection.ExecuteAction = FExecuteAction::CreateSP(this, &FSkeletalMeshEditor::OnRemoveSectionFromLodAndBelowMenuItemClicked, LodIndex, SectionIndex);

				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("MeshClickMenu_RemoveSectionFromLodAndBelow", "Generate section {1} up to LOD {0}"), LodIndex, SectionIndex),
					FText::Format(LOCTEXT("MeshClickMenu_RemoveSectionFromLodAndBelow_Tooltip", "Generated LODs will use section {1} up to LOD {0}, and ignore it for lower quality LODs"), LodIndex, SectionIndex),
					FSlateIcon(),
					ActionRemoveSection
				);
			}
		}
	}
	MenuBuilder.EndSection();
}

void FSkeletalMeshEditor::OnRemoveSectionFromLodAndBelowMenuItemClicked(int32 LodIndex, int32 SectionIndex)
{
	if (SkeletalMesh == nullptr || !SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LodIndex) || !SkeletalMesh->GetImportedModel()->LODModels[LodIndex].Sections.IsValidIndex(SectionIndex))
	{
		return;
	}
	const FSkeletalMeshLODInfo* SkeletalMeshLODInfo = SkeletalMesh->GetLODInfo(LodIndex);
	if (SkeletalMeshLODInfo == nullptr)
	{
		return;
	}
	FScopedTransaction Transaction(LOCTEXT("ChangeGenerateUpTo", "Set Generate Up To"));
	SkeletalMesh->Modify();

	SkeletalMesh->GetImportedModel()->LODModels[LodIndex].Sections[SectionIndex].GenerateUpToLodIndex = LodIndex;
	FSkeletalMeshUpdateContext UpdateContext;
	UpdateContext.SkeletalMesh = SkeletalMesh;
	UpdateContext.AssociatedComponents.Push(GetPersonaToolkit()->GetPreviewMeshComponent());
	//Generate only the LODs that can be affected by the changes
	TArray<int32> BaseLodIndexes;
	BaseLodIndexes.Add(LodIndex);
	for (int32 GenerateLodIndex = LodIndex + 1; GenerateLodIndex < SkeletalMesh->GetImportedModel()->LODModels.Num(); ++GenerateLodIndex)
	{
		const FSkeletalMeshLODInfo* CurrentSkeletalMeshLODInfo = SkeletalMesh->GetLODInfo(GenerateLodIndex);
		if (CurrentSkeletalMeshLODInfo != nullptr && CurrentSkeletalMeshLODInfo->bHasBeenSimplified && BaseLodIndexes.Contains(CurrentSkeletalMeshLODInfo->ReductionSettings.BaseLOD))
		{
			FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, GenerateLodIndex, GetTargetPlatformManagerRef().GetRunningTargetPlatform(), true);
			BaseLodIndexes.Add(GenerateLodIndex);
		}
	}
	SkeletalMesh->PostEditChange();
	GetPersonaToolkit()->GetPreviewScene()->InvalidateViews();
}

void FSkeletalMeshEditor::FillApplyClothingAssetMenu(FMenuBuilder& MenuBuilder, int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	// Nothing to fill
	if(!Mesh)
	{
		return;
	}

	MenuBuilder.BeginSection(TEXT("ApplyClothingMenu"), LOCTEXT("ApplyClothingMenuHeader", "Available Assets"));
	{
		for(UClothingAssetBase* BaseAsset : Mesh->GetMeshClothingAssets())
		{
			UClothingAssetCommon* ClothAsset = CastChecked<UClothingAssetCommon>(BaseAsset);

			FUIAction Action;
			Action.CanExecuteAction = FCanExecuteAction::CreateSP(this, &FSkeletalMeshEditor::CanApplyClothing, InLodIndex, InSectionIndex);

			const int32 NumClothLods = ClothAsset->GetNumLods();
			for(int32 ClothLodIndex = 0; ClothLodIndex < NumClothLods; ++ClothLodIndex)
			{
				Action.ExecuteAction = FExecuteAction::CreateSP(this, &FSkeletalMeshEditor::OnApplyClothingAssetClicked, BaseAsset, InLodIndex, InSectionIndex, ClothLodIndex);

				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("ApplyClothingMenuItem", "{0} - LOD{1}"), FText::FromString(ClothAsset->GetName()), FText::AsNumber(ClothLodIndex)),
					LOCTEXT("ApplyClothingMenuItem_ToolTip", "Apply this clothing asset to the selected mesh LOD and section"),
					FSlateIcon(),
					Action
					);
			}
		}
	}
	MenuBuilder.EndSection();
}

void FSkeletalMeshEditor::FillCreateClothingMenu(FMenuBuilder& MenuBuilder, int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	if(!Mesh)
	{
		return;
	}

	TSharedRef<SWidget> Widget = SNew(SCreateClothingSettingsPanel)
		.Mesh(Mesh)
		.MeshName(Mesh->GetName())
		.LodIndex(InLodIndex)
		.SectionIndex(InSectionIndex)
		.OnCreateRequested(this, &FSkeletalMeshEditor::OnCreateClothingAssetMenuItemClicked)
		.bIsSubImport(false);

	MenuBuilder.AddWidget(Widget, FText::GetEmpty(), true, false);
}

void FSkeletalMeshEditor::FillCreateClothingLodMenu(FMenuBuilder& MenuBuilder, int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	if(!Mesh)
	{
		return;
	}

	TSharedRef<SWidget> Widget = SNew(SCreateClothingSettingsPanel)
		.Mesh(Mesh)
		.MeshName(Mesh->GetName())
		.LodIndex(InLodIndex)
		.SectionIndex(InSectionIndex)
		.OnCreateRequested(this, &FSkeletalMeshEditor::OnCreateClothingAssetMenuItemClicked)
		.bIsSubImport(true);

		MenuBuilder.AddWidget(Widget, FText::GetEmpty(), true, false);
}

void FSkeletalMeshEditor::OnRemoveClothingAssetMenuItemClicked(int32 InLodIndex, int32 InSectionIndex)
{
	RemoveClothing(InLodIndex, InSectionIndex);
}

void FSkeletalMeshEditor::OnCreateClothingAssetMenuItemClicked(FSkeletalMeshClothBuildParams& Params)
{
	// Close the menu we created
	FSlateApplication::Get().DismissAllMenus();

	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	if(Mesh)
	{
		Mesh->Modify();

		FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(Mesh);

		if (Params.bRemoveFromMesh)  // Remove section prior to importing, otherwise the UsedBoneIndices won't be reflecting the loss of the section in the sub LOD
		{
			// Force the rebuilding of the render data at the end of this scope to update the used bone array
			FScopedSkeletalMeshPostEditChange ScopedSkeletalMeshPostEditChange(Mesh);
	
			// User doesn't want the section anymore as a renderable, get rid of it
			Mesh->RemoveMeshSection(Params.LodIndex, Params.SourceSection);
		}

		// Update the skeletal mesh at the end of the scope, this time with the new clothing changes
		FScopedSkeletalMeshPostEditChange ScopedSkeletalMeshPostEditChange(Mesh);

		// Handle the creation through the clothing asset factory
		FClothingSystemEditorInterfaceModule& ClothingEditorModule = FModuleManager::LoadModuleChecked<FClothingSystemEditorInterfaceModule>("ClothingSystemEditorInterface");
		UClothingAssetFactoryBase* AssetFactory = ClothingEditorModule.GetClothingAssetFactory();

		// See if we're importing a LOD or new asset
		if(Params.TargetAsset.IsValid())
		{
			UClothingAssetBase* TargetAssetPtr = Params.TargetAsset.Get();
			int32 SectionIndex = -1, AssetLodIndex = -1;
			if (Params.bRemapParameters)
			{
				if (TargetAssetPtr)
				{
					//Cache the section and asset LOD this asset was bound at before unbinding
					FSkeletalMeshLODModel& SkelLod = Mesh->GetImportedModel()->LODModels[Params.TargetLod];
					for (int32 i = 0; i < SkelLod.Sections.Num(); ++i)
					{
						if (SkelLod.Sections[i].ClothingData.AssetGuid == TargetAssetPtr->GetAssetGuid())
						{
							SectionIndex = i;
							AssetLodIndex = SkelLod.Sections[i].ClothingData.AssetLodIndex;
							RemoveClothing(Params.TargetLod, SectionIndex);
							break;
						}
					}
				}
			}

			AssetFactory->ImportLodToClothing(Mesh, Params);

			if (Params.bRemapParameters)
			{
				//If it was bound previously, rebind at same section with same LOD
				if (TargetAssetPtr && SectionIndex > -1)
				{
					ApplyClothing(TargetAssetPtr, Params.TargetLod, SectionIndex, AssetLodIndex);
				}
			}
		}
		else
		{
			UClothingAssetBase* NewClothingAsset = AssetFactory->CreateFromSkeletalMesh(Mesh, Params);

			if(NewClothingAsset)
			{
				Mesh->AddClothingAsset(NewClothingAsset);
			}
		}

		//Make sure no section is isolated or highlighted
		UDebugSkelMeshComponent * MeshComponent = GetPersonaToolkit()->GetPreviewScene()->GetPreviewMeshComponent();
		if(MeshComponent)
		{
			MeshComponent->SetSelectedEditorSection(INDEX_NONE);
			MeshComponent->SetSelectedEditorMaterial(INDEX_NONE);
			MeshComponent->SetMaterialPreview(INDEX_NONE);
			MeshComponent->SetSectionPreview(INDEX_NONE);
		}
	}
}

void FSkeletalMeshEditor::OnApplyClothingAssetClicked(UClothingAssetBase* InAssetToApply, int32 InMeshLodIndex, int32 InMeshSectionIndex, int32 InClothLodIndex)
{
	ApplyClothing(InAssetToApply, InMeshLodIndex, InMeshSectionIndex, InClothLodIndex);
}

bool FSkeletalMeshEditor::CanApplyClothing(int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	if(Mesh->GetMeshClothingAssets().Num() > 0)
	{
	FSkeletalMeshModel* MeshResource = Mesh->GetImportedModel();

	if(MeshResource->LODModels.IsValidIndex(InLodIndex))
	{
		FSkeletalMeshLODModel& LodModel = MeshResource->LODModels[InLodIndex];

		if(LodModel.Sections.IsValidIndex(InSectionIndex))
		{
			return !LodModel.Sections[InSectionIndex].HasClothingData();
		}
	}
	}

	return false;
}

bool FSkeletalMeshEditor::CanRemoveClothing(int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	FSkeletalMeshModel* MeshResource = Mesh->GetImportedModel();

	if(MeshResource->LODModels.IsValidIndex(InLodIndex))
	{
		FSkeletalMeshLODModel& LodModel = MeshResource->LODModels[InLodIndex];

		if(LodModel.Sections.IsValidIndex(InSectionIndex))
		{
			return LodModel.Sections[InSectionIndex].HasClothingData();
		}
	}

	return false;
}

bool FSkeletalMeshEditor::CanCreateClothing(int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	FSkeletalMeshModel* MeshResource = Mesh->GetImportedModel();

	if(MeshResource->LODModels.IsValidIndex(InLodIndex))
	{
		FSkeletalMeshLODModel& LodModel = MeshResource->LODModels[InLodIndex];

		if(LodModel.Sections.IsValidIndex(InSectionIndex))
		{
			FSkelMeshSection& Section = LodModel.Sections[InSectionIndex];

			return !Section.HasClothingData();
		}
	}

	return false;
}

bool FSkeletalMeshEditor::CanCreateClothingLod(int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	return Mesh && Mesh->GetMeshClothingAssets().Num() > 0 && CanApplyClothing(InLodIndex, InSectionIndex);
}

void FSkeletalMeshEditor::ApplyClothing(UClothingAssetBase* InAsset, int32 InLodIndex, int32 InSectionIndex, int32 InClothingLod)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	if (Mesh == nullptr || Mesh->GetImportedModel() == nullptr || !Mesh->GetImportedModel()->LODModels.IsValidIndex(InLodIndex))
	{
		return;
	}

	FSkeletalMeshLODModel& LODModel = Mesh->GetImportedModel()->LODModels[InLodIndex];
	const FSkelMeshSection& Section = LODModel.Sections[InSectionIndex];
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(Mesh);
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(Mesh);
		FScopedTransaction Transaction(LOCTEXT("SkeletalMeshEditorApplyClothingTransaction", "Persona editor: Apply Section Cloth"));
		Mesh->Modify();

		FSkelMeshSourceSectionUserData& OriginalSectionData = LODModel.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
		auto ClearOriginalSectionUserData = [&OriginalSectionData]()
		{
			OriginalSectionData.CorrespondClothAssetIndex = INDEX_NONE;
			OriginalSectionData.ClothingData.AssetGuid = FGuid();
			OriginalSectionData.ClothingData.AssetLodIndex = INDEX_NONE;
		};
		if (UClothingAssetCommon* ClothingAsset = Cast<UClothingAssetCommon>(InAsset))
		{
			ClothingAsset->Modify();

			// Look for a currently bound asset an unbind it if necessary first
			if (UClothingAssetBase* CurrentAsset = Mesh->GetSectionClothingAsset(InLodIndex, InSectionIndex))
			{
				CurrentAsset->Modify();
				CurrentAsset->UnbindFromSkeletalMesh(Mesh, InLodIndex);
				ClearOriginalSectionUserData();
			}

			if (ClothingAsset->BindToSkeletalMesh(Mesh, InLodIndex, InSectionIndex, InClothingLod))
			{
				//Successful bind so set the SectionUserData
				int32 AssetIndex = INDEX_NONE;
				check(Mesh->GetMeshClothingAssets().Find(ClothingAsset, AssetIndex));
				OriginalSectionData.CorrespondClothAssetIndex = AssetIndex;
				OriginalSectionData.ClothingData.AssetGuid = ClothingAsset->GetAssetGuid();
				OriginalSectionData.ClothingData.AssetLodIndex = InClothingLod;
			}
		}
		else if (Mesh)
		{
			//User set none, so unbind anything that is bind
			if (UClothingAssetBase* CurrentAsset = Mesh->GetSectionClothingAsset(InLodIndex, InSectionIndex))
			{
				CurrentAsset->Modify();
				CurrentAsset->UnbindFromSkeletalMesh(Mesh, InLodIndex);
				ClearOriginalSectionUserData();
			}
		}
	}
}

void FSkeletalMeshEditor::RemoveClothing(int32 InLodIndex, int32 InSectionIndex)
{
	USkeletalMesh* Mesh = GetPersonaToolkit()->GetPreviewMesh();

	if (Mesh->GetImportedModel() == nullptr || !Mesh->GetImportedModel()->LODModels.IsValidIndex(InLodIndex))
	{
		return;
	}

	FSkeletalMeshLODModel& LODModel = Mesh->GetImportedModel()->LODModels[InLodIndex];
	const FSkelMeshSection& Section = LODModel.Sections[InSectionIndex];
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(Mesh);
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(Mesh);
		FScopedTransaction Transaction(LOCTEXT("SkeletalMeshEditorRemoveClothingTransaction", "Persona editor: Remove Section Cloth"));
		Mesh->Modify();

		FSkelMeshSourceSectionUserData& OriginalSectionData = LODModel.UserSectionsData.FindOrAdd(Section.OriginalDataSectionIndex);
		auto ClearOriginalSectionUserData = [&OriginalSectionData]()
		{
			OriginalSectionData.CorrespondClothAssetIndex = INDEX_NONE;
			OriginalSectionData.ClothingData.AssetGuid = FGuid();
			OriginalSectionData.ClothingData.AssetLodIndex = INDEX_NONE;
		};
		// Look for a currently bound asset an unbind it if necessary first
		if (UClothingAssetBase* CurrentAsset = Mesh->GetSectionClothingAsset(InLodIndex, InSectionIndex))
		{
			CurrentAsset->Modify();
			CurrentAsset->UnbindFromSkeletalMesh(Mesh, InLodIndex);
			ClearOriginalSectionUserData();
		}
	}
}

void FSkeletalMeshEditor::ExtendMenu()
{
	MenuExtender = MakeShareable(new FExtender);

	AddMenuExtender(MenuExtender);

	ISkeletalMeshEditorModule& SkeletalMeshEditorModule = FModuleManager::GetModuleChecked<ISkeletalMeshEditorModule>("SkeletalMeshEditor");
	AddMenuExtender(SkeletalMeshEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

void FSkeletalMeshEditor::HandleObjectsSelected(const TArray<UObject*>& InObjects)
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObjects(InObjects);
	}
}

void FSkeletalMeshEditor::HandleObjectSelected(UObject* InObject)
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(InObject);
	}
}

void FSkeletalMeshEditor::HandleSelectionChanged(const TArrayView<TSharedPtr<ISkeletonTreeItem>>& InSelectedItems, ESelectInfo::Type InSelectInfo)
{
	if (DetailsView.IsValid())
	{
		TArray<UObject*> Objects;
		Algo::TransformIf(InSelectedItems, Objects, [](const TSharedPtr<ISkeletonTreeItem>& InItem) { return InItem->GetObject() != nullptr; }, [](const TSharedPtr<ISkeletonTreeItem>& InItem) { return InItem->GetObject(); });
		DetailsView->SetObjects(Objects);
	}
}

void FSkeletalMeshEditor::PostUndo(bool bSuccess)
{
	OnPostUndo.Broadcast();
}

void FSkeletalMeshEditor::PostRedo(bool bSuccess)
{
	OnPostUndo.Broadcast();
}

void FSkeletalMeshEditor::Tick(float DeltaTime)
{
	GetPersonaToolkit()->GetPreviewScene()->InvalidateViews();
}

TStatId FSkeletalMeshEditor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FSkeletalMeshEditor, STATGROUP_Tickables);
}

void FSkeletalMeshEditor::HandleDetailsCreated(const TSharedRef<IDetailsView>& InDetailsView)
{
	DetailsView = InDetailsView;
}

void FSkeletalMeshEditor::HandleMeshDetailsCreated(const TSharedRef<IDetailsView>& InDetailsView)
{
	FPersonaModule& PersonaModule = FModuleManager::GetModuleChecked<FPersonaModule>("Persona");
	PersonaModule.CustomizeMeshDetails(InDetailsView, GetPersonaToolkit());
}

UObject* FSkeletalMeshEditor::HandleGetAsset()
{
	return GetEditingObject();
}

bool FSkeletalMeshEditor::HandleReimportMeshInternal(int32 SourceFileIndex /*= INDEX_NONE*/, bool bWithNewFile /*= false*/)
{
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);
	bool bResult = false;
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);
		// Reimport the asset
		bResult = FReimportManager::Instance()->Reimport(SkeletalMesh, true, true, TEXT(""), nullptr, SourceFileIndex, bWithNewFile);
		// Refresh skeleton tree
		SkeletonTree->Refresh();
	}
	return bResult;
}

void FSkeletalMeshEditor::HandleReimportMesh(int32 SourceFileIndex /*= INDEX_NONE*/)
{
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);

	HandleReimportMeshInternal(SourceFileIndex, false);
}

void FSkeletalMeshEditor::HandleReimportMeshWithNewFile(int32 SourceFileIndex /*= INDEX_NONE*/)
{
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);

	HandleReimportMeshInternal(SourceFileIndex, true);
}

void ReimportAllCustomLODs(USkeletalMesh* SkeletalMesh, UDebugSkelMeshComponent* PreviewMeshComponent, bool bWithNewFile)
{
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

		//Find the dependencies of the generated LOD
		TArray<bool> Dependencies;
		Dependencies.AddZeroed(SkeletalMesh->GetLODNum());
		//Avoid making LOD 0 to true in the dependencies since everything that should be regenerate base on LOD 0 is already regenerate at this point.
		//But we need to regenerate every generated LOD base on any re-import custom LOD
		//Reimport all custom LODs
		for (int32 LodIndex = 1; LodIndex < SkeletalMesh->GetLODNum(); ++LodIndex)
		{
			//Do not reimport LOD that was re-import with the base mesh
			if (SkeletalMesh->GetLODInfo(LodIndex)->bImportWithBaseMesh)
			{
				continue;
			}
			if (SkeletalMesh->GetLODInfo(LodIndex)->bHasBeenSimplified == false)
			{
				FString SourceFilenameBackup = SkeletalMesh->GetLODInfo(LodIndex)->SourceImportFilename;
				if (bWithNewFile)
				{
					SkeletalMesh->GetLODInfo(LodIndex)->SourceImportFilename.Empty();
				}

				if (!FbxMeshUtils::ImportMeshLODDialog(SkeletalMesh, LodIndex, false))
				{
					if (bWithNewFile)
					{
						SkeletalMesh->GetLODInfo(LodIndex)->SourceImportFilename = SourceFilenameBackup;
					}
				}
				else
				{
					Dependencies[LodIndex] = true;
				}
			}
			else if (Dependencies[SkeletalMesh->GetLODInfo(LodIndex)->ReductionSettings.BaseLOD])
			{
				//Regenerate the LOD
				FSkeletalMeshUpdateContext UpdateContext;
				UpdateContext.SkeletalMesh = SkeletalMesh;
				UpdateContext.AssociatedComponents.Push(PreviewMeshComponent);
				FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LodIndex, GetTargetPlatformManagerRef().GetRunningTargetPlatform(), false);
				Dependencies[LodIndex] = true;
			}
		}
	}
}

void FSkeletalMeshEditor::HandleReimportAllMesh(int32 SourceFileIndex /*= INDEX_NONE*/)
{
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);
	// Reimport the asset
	if (SkeletalMesh)
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);

		//Reimport base LOD
		if (HandleReimportMeshInternal(SourceFileIndex, false))
		{
			//Reimport all custom LODs
			ReimportAllCustomLODs(SkeletalMesh, GetPersonaToolkit()->GetPreviewMeshComponent(), false);
		}
	}
}

void FSkeletalMeshEditor::HandleReimportAllMeshWithNewFile(int32 SourceFileIndex /*= INDEX_NONE*/)
{
	FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkeletalMesh);

	// Reimport the asset
	if (SkeletalMesh)
	{
		FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh);
		TArray<UObject*> ImportObjs;
		ImportObjs.Add(SkeletalMesh);
		if (HandleReimportMeshInternal(SourceFileIndex, true))
		{
			//Reimport all custom LODs
			ReimportAllCustomLODs(SkeletalMesh, GetPersonaToolkit()->GetPreviewMeshComponent(), true);
		}
	}
}


void FSkeletalMeshEditor::ToggleMeshSectionSelection()
{
	TSharedRef<IPersonaPreviewScene> PreviewScene = GetPersonaToolkit()->GetPreviewScene();
	PreviewScene->DeselectAll();
	bool bState = !PreviewScene->AllowMeshHitProxies();
	GetMutableDefault<UPersonaOptions>()->bAllowMeshSectionSelection = bState;
	PreviewScene->SetAllowMeshHitProxies(bState);
}

bool FSkeletalMeshEditor::IsMeshSectionSelectionChecked() const
{
	return GetPersonaToolkit()->GetPreviewScene()->AllowMeshHitProxies();
}

void FSkeletalMeshEditor::HandleMeshClick(HActor* HitProxy, const FViewportClick& Click)
{
	USkeletalMeshComponent* Component = GetPersonaToolkit()->GetPreviewMeshComponent();
	if (Component)
	{
		Component->SetSelectedEditorSection(HitProxy->SectionIndex);
		Component->PushSelectionToProxy();
	}

	if(Click.GetKey() == EKeys::RightMouseButton)
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		FillMeshClickMenu(MenuBuilder, HitProxy, Click);

		FSlateApplication::Get().PushMenu(
			FSlateApplication::Get().GetActiveTopLevelWindow().ToSharedRef(),
			FWidgetPath(),
			MenuBuilder.MakeWidget(),
			FSlateApplication::Get().GetCursorPos(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
			);
	}
}

#undef LOCTEXT_NAMESPACE
