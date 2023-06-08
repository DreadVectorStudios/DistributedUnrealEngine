// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureEditorStyle.h"

#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"

FName FFractureEditorStyle::StyleName("FractureEditorStyle");

FFractureEditorStyle::FFractureEditorStyle()
	: FSlateStyleSet(StyleName)
{
	const FVector2D IconSize(20.0f, 20.0f);
	const FVector2D SmallIconSize(20.0f, 20.0f);
	const FVector2D LabelIconSize(16.0f, 16.0f);


	// const FVector2D Icon8x8(8.0f, 8.0f);
	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Experimental/ChaosEditor/Content"));
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	Set("FractureEditor.Slice", new FSlateImageBrush(RootToContentDir(TEXT("FractureSliceCut_40x.png")), IconSize));
	Set("FractureEditor.Slice.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSliceCut_20x.png")), SmallIconSize));
	Set("FractureEditor.Uniform", new FSlateImageBrush(RootToContentDir(TEXT("FractureUniformVoronoi_40x.png")), IconSize));
	Set("FractureEditor.Uniform.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureUniformVoronoi_20x.png")), SmallIconSize));
	Set("FractureEditor.Radial", new FSlateImageBrush(RootToContentDir(TEXT("FractureRadialVoronoi_40x.png")), IconSize));
	Set("FractureEditor.Radial.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureRadialVoronoi_20x.png")), SmallIconSize));
	Set("FractureEditor.Clustered", new FSlateImageBrush(RootToContentDir(TEXT("FractureClusterVoronoi_40x.png")), IconSize));
	Set("FractureEditor.Clustered.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureClusterVoronoi_20x.png")), SmallIconSize));
	Set("FractureEditor.Planar", new FSlateImageBrush(RootToContentDir(TEXT("FracturePlanarCut_40x.png")), IconSize));
	Set("FractureEditor.Planar.Small", new FSlateImageBrush(RootToContentDir(TEXT("FracturePlanarCut_20x.png")), SmallIconSize));
	Set("FractureEditor.Brick", new FSlateImageBrush(RootToContentDir(TEXT("FractureBrick_40x.png")), IconSize));
	Set("FractureEditor.Brick.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureBrick_20x.png")), SmallIconSize));
	Set("FractureEditor.Texture", new FSlateImageBrush(RootToContentDir(TEXT("FractureTexture_40x.png")), IconSize));
	Set("FractureEditor.Texture.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureTexture_20x.png")), SmallIconSize));
	Set("FractureEditor.Mesh", new FSlateImageBrush(RootToContentDir(TEXT("FractureMesh_40x.png")), IconSize));
	Set("FractureEditor.Mesh.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureMesh_20x.png")), SmallIconSize));

	// This is a bit of magic.  When you pass a command your Builder.AddToolBarButton, it will automatically try to find 
	// and Icon with the same name as the command and TCommand<> Context Name.  
	// format <Context>.<CommandName>[.Small]
	Set("FractureEditor.SelectAll", new FSlateImageBrush(RootToContentDir(TEXT("SelectAll_48x.png")), IconSize));
	Set("FractureEditor.SelectAll.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectAll_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectNone", new FSlateImageBrush(RootToContentDir(TEXT("DeselectAll_48x.png")), IconSize));
	Set("FractureEditor.SelectNone.Small", new FSlateImageBrush(RootToContentDir(TEXT("DeselectAll_48x.png")), SmallIconSize));
	Set("FractureEditor.SelectNeighbors", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectNeighbor_40x.png")), IconSize));
	Set("FractureEditor.SelectNeighbors.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectNeighbor_20x.png")), SmallIconSize));
	Set("FractureEditor.SelectParent", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectParent_40x.png")), IconSize));
	Set("FractureEditor.SelectParent.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectParent_20x.png")), SmallIconSize));
	Set("FractureEditor.SelectChildren", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectChildren_40x.png")), IconSize));
	Set("FractureEditor.SelectChildren.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectChildren_20x.png")), SmallIconSize));
	Set("FractureEditor.SelectSiblings", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectSiblings_40x.png")), IconSize));
	Set("FractureEditor.SelectSiblings.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectSiblings_20x.png")), SmallIconSize));
	Set("FractureEditor.SelectAllInLevel", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectLevel_40x.png")), IconSize));
	Set("FractureEditor.SelectAllInLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureSelectLevel_20x.png")), SmallIconSize));
	Set("FractureEditor.SelectInvert", new FSlateImageBrush(RootToContentDir(TEXT("SelectInvert_48x.png")), IconSize));
	Set("FractureEditor.SelectInvert.Small", new FSlateImageBrush(RootToContentDir(TEXT("SelectInvert_48x.png")), SmallIconSize));

	Set("FractureEditor.AutoCluster", new FSlateImageBrush(RootToContentDir(TEXT("FractureAutoCluster_40x.png")), IconSize));
	Set("FractureEditor.AutoCluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureAutoCluster_20x.png")), SmallIconSize));
	Set("FractureEditor.ClusterMagnet", new FSlateImageBrush(RootToContentDir(TEXT("FractureMagnet_40x.png")), IconSize));
	Set("FractureEditor.ClusterMagnet.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureMagnet_20x.png")), SmallIconSize));
	Set("FractureEditor.Cluster", new FSlateImageBrush(RootToContentDir(TEXT("FractureCluster_40x.png")), IconSize));
	Set("FractureEditor.Cluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureCluster_20x.png")), SmallIconSize));
	Set("FractureEditor.Uncluster", new FSlateImageBrush(RootToContentDir(TEXT("FractureUncluster_40x.png")), IconSize));
	Set("FractureEditor.Uncluster.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureUncluster_20x.png")), SmallIconSize));
	Set("FractureEditor.FlattenToLevel", new FSlateImageBrush(RootToContentDir(TEXT("FractureFlattenToLevel_40x.png")), IconSize));
	Set("FractureEditor.FlattenToLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureFlattenToLevel_20x.png")), SmallIconSize));
	Set("FractureEditor.Flatten", new FSlateImageBrush(RootToContentDir(TEXT("FractureFlatten_40x.png")), IconSize));
	Set("FractureEditor.Flatten.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureFlatten_20x.png")), SmallIconSize));
	Set("FractureEditor.Merge", new FSlateImageBrush(RootToContentDir(TEXT("FractureMerge_40x.png")), IconSize));
	Set("FractureEditor.Merge.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureMerge_20x.png")), SmallIconSize));
	Set("FractureEditor.MoveUp", new FSlateImageBrush(RootToContentDir(TEXT("FractureMoveUpRow_40x.png")), IconSize));
	Set("FractureEditor.MoveUp.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureMoveUpRow_20x.png")), SmallIconSize));

	Set("FractureEditor.AddEmbeddedGeometry", new FSlateImageBrush(RootToContentDir(TEXT("FractureEmbed_40x.png")), IconSize));
	Set("FractureEditor.AddEmbeddedGeometry.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureEmbed_20x.png")), SmallIconSize));
	Set("FractureEditor.AutoEmbedGeometry", new FSlateImageBrush(RootToContentDir(TEXT("FractureAutoEmbed_40x.png")), IconSize));
	Set("FractureEditor.AutoEmbedGeometry.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureAutoEmbed_20x.png")), SmallIconSize));
	Set("FractureEditor.FlushEmbeddedGeometry", new FSlateImageBrush(RootToContentDir(TEXT("FractureFlush_40x.png")), IconSize));
	Set("FractureEditor.FlushEmbeddedGeometry.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureFlush_20x.png")), SmallIconSize));

	Set("FractureEditor.AutoUV", new FSlateImageBrush(RootToContentDir(TEXT("FractureAutoUV_40x.png")), IconSize));
	Set("FractureEditor.AutoUV.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureAutoUV_20x.png")), SmallIconSize));

	Set("FractureEditor.DeleteBranch", new FSlateImageBrush(RootToContentDir(TEXT("FracturePrune_40x.png")), IconSize));
	Set("FractureEditor.DeleteBranch.Small", new FSlateImageBrush(RootToContentDir(TEXT("FracturePrune_20x.png")), SmallIconSize));
	Set("FractureEditor.Hide", new FSlateImageBrush(RootToContentDir(TEXT("FractureHide_40x.png")), IconSize));
	Set("FractureEditor.Hide.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureHide_20x.png")), SmallIconSize));
	Set("FractureEditor.Unhide", new FSlateImageBrush(RootToContentDir(TEXT("FractureUnhide_40x.png")), IconSize));
	Set("FractureEditor.Unhide.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureUnhide_20x.png")), SmallIconSize));

	Set("FractureEditor.RecomputeNormals", new FSlateImageBrush(RootToContentDir(TEXT("FractureNormals_40x.png")), IconSize));
	Set("FractureEditor.RecomputeNormals.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureNormals_20x.png")), SmallIconSize));
	Set("FractureEditor.Resample", new FSlateImageBrush(RootToContentDir(TEXT("FractureResample_40x.png")), IconSize));
	Set("FractureEditor.Resample.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureResample_20x.png")), SmallIconSize));

	Set("FractureEditor.ToMesh", new FSlateImageBrush(RootToContentDir(TEXT("MakeStaticMesh_40x.png")), IconSize));
	Set("FractureEditor.ToMesh.Small", new FSlateImageBrush(RootToContentDir(TEXT("MakeStaticMesh_20x.png")), SmallIconSize));
	Set("FractureEditor.Validate", new FSlateImageBrush(RootToContentDir(TEXT("Validate_40x.png")), IconSize));
	Set("FractureEditor.Validate.Small", new FSlateImageBrush(RootToContentDir(TEXT("Validate_20x.png")), SmallIconSize));

	Set("FractureEditor.Convex", new FSlateImageBrush(RootToContentDir(TEXT("FractureConvex_40x.png")), IconSize));
	Set("FractureEditor.Convex.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureConvex_20x.png")), SmallIconSize));
	Set("FractureEditor.CustomVoronoi", new FSlateImageBrush(RootToContentDir(TEXT("FractureCustom_40x.png")), IconSize));
	Set("FractureEditor.CustomVoronoi.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureCustom_20x.png")), SmallIconSize));
	Set("FractureEditor.FixTinyGeo", new FSlateImageBrush(RootToContentDir(TEXT("FractureGeoMerge_40x.png")), IconSize));
	Set("FractureEditor.FixTinyGeo.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureGeoMerge_20x.png")), SmallIconSize));

	// View Settings
	Set("FractureEditor.Exploded", new FSlateImageBrush(RootToContentDir(TEXT("MiniExploded_32x.png")), LabelIconSize));
	Set("FractureEditor.Levels", new FSlateImageBrush(RootToContentDir(TEXT("MiniLevel_32x.png")), LabelIconSize));
	Set("FractureEditor.Visibility", new FSlateImageBrush(RootToContentDir(TEXT("GeneralVisibility_48x.png")), SmallIconSize));
	Set("FractureEditor.ToggleShowBoneColors", new FSlateImageBrush(RootToContentDir(TEXT("GeneralVisibility_48x.png")), IconSize));
	Set("FractureEditor.ToggleShowBoneColors.Small", new FSlateImageBrush(RootToContentDir(TEXT("GeneralVisibility_48x.png")), SmallIconSize));
	Set("FractureEditor.ViewUpOneLevel", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewUp_48x.png")), IconSize));
	Set("FractureEditor.ViewUpOneLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewUp_48x.png")), SmallIconSize));
	Set("FractureEditor.ViewDownOneLevel", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewDown_48x.png")), IconSize));
	Set("FractureEditor.ViewDownOneLevel.Small", new FSlateImageBrush(RootToContentDir(TEXT("LevelViewDown_48x.png")), SmallIconSize));

	Set("FractureEditor.SpinBox", FSpinBoxStyle(FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
		.SetTextPadding(FMargin(0))
		.SetBackgroundBrush(FSlateNoResource())
		.SetHoveredBackgroundBrush(FSlateNoResource())
		.SetInactiveFillBrush(FSlateNoResource())
		.SetActiveFillBrush(FSlateNoResource())
		.SetForegroundColor(FSlateColor::UseSubduedForeground())
		.SetArrowsImage(FSlateNoResource())
	);



	Set("FractureEditor.GenerateAsset", new FSlateImageBrush(RootToContentDir(TEXT("FractureGenerateAsset_40x.png")), IconSize));
	Set("FractureEditor.GenerateAsset.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureGenerateAsset_20x.png")), SmallIconSize));
	Set("FractureEditor.ResetAsset", new FSlateImageBrush(RootToContentDir(TEXT("FractureReset_40x.png")), IconSize));
	Set("FractureEditor.ResetAsset.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureReset_20x.png")), SmallIconSize));

	Set("FractureEditor.SetInitialDynamicState", new FSlateImageBrush(RootToContentDir(TEXT("FractureState_40x.png")), IconSize));
	Set("FractureEditor.SetInitialDynamicState.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureState_20x.png")), SmallIconSize));

	Set("LevelEditor.FractureMode", new FSlateImageBrush(RootToContentDir(TEXT("FractureMode.png")), FVector2D(40.0f, 40.0f)));
	Set("LevelEditor.FractureMode.Small", new FSlateImageBrush(RootToContentDir(TEXT("FractureMode.png")), FVector2D(20.0f, 20.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FFractureEditorStyle::~FFractureEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FFractureEditorStyle& FFractureEditorStyle::Get()
{
	static FFractureEditorStyle Inst;
	return Inst;
}


