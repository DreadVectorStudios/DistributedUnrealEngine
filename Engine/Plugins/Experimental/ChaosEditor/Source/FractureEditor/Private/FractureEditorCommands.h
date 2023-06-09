// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/Commands/Commands.h"

class FFractureEditorCommands : public TCommands<FFractureEditorCommands>
{
	public:
		FFractureEditorCommands(); 

		virtual void RegisterCommands() override;

	public:
		
		// Selection Commands
		TSharedPtr< FUICommandInfo > SelectAll;
		TSharedPtr< FUICommandInfo > SelectNone;
		TSharedPtr< FUICommandInfo > SelectNeighbors;
		TSharedPtr< FUICommandInfo > SelectParent;
		TSharedPtr< FUICommandInfo > SelectChildren;
		TSharedPtr< FUICommandInfo > SelectSiblings;
		TSharedPtr< FUICommandInfo > SelectAllInLevel;
		TSharedPtr< FUICommandInfo > SelectInvert;

		// View Settings
		TSharedPtr< FUICommandInfo > ToggleShowBoneColors;
		TSharedPtr< FUICommandInfo > ViewUpOneLevel;
		TSharedPtr< FUICommandInfo > ViewDownOneLevel;
		TSharedPtr< FUICommandInfo > ExplodeMore;
		TSharedPtr< FUICommandInfo > ExplodeLess;

		// Cluster Commands
		TSharedPtr< FUICommandInfo > AutoCluster;
		TSharedPtr< FUICommandInfo > ClusterMagnet;
		TSharedPtr< FUICommandInfo > Cluster;
		TSharedPtr< FUICommandInfo > Uncluster;
		TSharedPtr< FUICommandInfo > Flatten;
		TSharedPtr< FUICommandInfo > MoveUp;
		TSharedPtr< FUICommandInfo > ClusterMerge;

		// Edit Commands
		TSharedPtr< FUICommandInfo > DeleteBranch;
		
		// Generate Commands
		TSharedPtr< FUICommandInfo > GenerateAsset;
		TSharedPtr< FUICommandInfo > ResetAsset;

		// Embed Commands
		TSharedPtr< FUICommandInfo > AddEmbeddedGeometry;
		TSharedPtr< FUICommandInfo > AutoEmbedGeometry;
		TSharedPtr< FUICommandInfo > FlushEmbeddedGeometry;

		// UV Commands
		TSharedPtr< FUICommandInfo > AutoUV;
		
		// Fracture Commands
		TSharedPtr< FUICommandInfo > Uniform;
		TSharedPtr< FUICommandInfo > Radial;
		TSharedPtr< FUICommandInfo > Clustered;
		TSharedPtr< FUICommandInfo > Planar;
		TSharedPtr< FUICommandInfo > Slice;
		TSharedPtr< FUICommandInfo > Brick;
		TSharedPtr< FUICommandInfo > Texture;

		// Cleanup Commands
		TSharedPtr< FUICommandInfo > RecomputeNormals;
		TSharedPtr< FUICommandInfo > Resample;
		TSharedPtr< FUICommandInfo > ConvertToMesh;
		TSharedPtr< FUICommandInfo > Mesh;
		TSharedPtr< FUICommandInfo > FixTinyGeo;
		TSharedPtr< FUICommandInfo > MakeConvex;

		// Property Commands
		TSharedPtr< FUICommandInfo > SetInitialDynamicState;
		
};

