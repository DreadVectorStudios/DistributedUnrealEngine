// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshAABBTree3.h"
#include "Selections/MeshConnectedComponents.h"
#include "MeshTransforms.h"
#include "MeshConstraints.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Spatial/PointHashGrid3.h"

struct FPlanarCells;
struct FInternalSurfaceMaterials;
struct FNoiseSettings;

namespace UE
{
namespace PlanarCut
{

/**
 * Add attributes necessary for a dynamic mesh to represent geometry from an FGeometryCollection
 */
void PLANARCUT_API SetGeometryCollectionAttributes(FDynamicMesh3& Mesh, int32 NumUVLayers);

// functions for DynamicMesh3 meshes that have FGeometryCollection attributes set
namespace AugmentedDynamicMesh
{
	void PLANARCUT_API SetVisibility(FDynamicMesh3& Mesh, int TID, bool bIsVisible);
	bool PLANARCUT_API GetVisibility(const FDynamicMesh3& Mesh, int TID);
	void PLANARCUT_API SetUV(FDynamicMesh3& Mesh, int VID, FVector2f UV, int UVLayer);
	void PLANARCUT_API GetUV(const FDynamicMesh3& Mesh, int VID, FVector2f& UV, int UVLayer);
	void PLANARCUT_API SetTangent(FDynamicMesh3& Mesh, int VID, FVector3f Normal, FVector3f TangentU, FVector3f TangentV);
	void PLANARCUT_API GetTangent(const FDynamicMesh3& Mesh, int VID, FVector3f& U, FVector3f& V);
	/// Initialize UV overlays based on the custom AugmentedDynamicMesh per-vertex UV attributes.  Optionally use FirstUVLayer to skip layers
	void PLANARCUT_API InitializeOverlayToPerVertexUVs(FDynamicMesh3& Mesh, int32 NumUVLayers, int32 FirstUVLayer = 0);
	void PLANARCUT_API InitializeOverlayToPerVertexTangents(FDynamicMesh3& Mesh);
	void PLANARCUT_API ComputeTangents(FDynamicMesh3& Mesh, bool bOnlyOddMaterials, const TArrayView<const int32>& WhichMaterials, bool bRecomputeNormals = true);
	void PLANARCUT_API AddCollisionSamplesPerComponent(FDynamicMesh3& Mesh, double Spacing);
	void PLANARCUT_API SplitOverlayAttributesToPerVertex(FDynamicMesh3& Mesh, bool bSplitUVs = true, bool bSplitTangents = true);
}


/**
 * Dynamic mesh representation of cutting cells, to be used to fracture a mesh
 */
struct PLANARCUT_API FCellMeshes
{
	struct FCellInfo
	{
		FDynamicMesh3 AugMesh;

		FCellInfo(int32 NumUVLayers = 1)
		{
			SetGeometryCollectionAttributes(AugMesh, NumUVLayers);
		}

		// TODO: compute spatial in advance?  (only useful if we rework mesh booleans to support it)
		//FDynamicMeshAABBTree3 Spatial;
	};

	TIndirectArray<FCellInfo> CellMeshes;
	int32 OutsideCellIndex = -1;
	int32 NumUVLayers = 1;

	// Noise Offsets, to randomize where perlin noise is sampled
	FVector NoiseOffsetX;
	FVector NoiseOffsetY;
	FVector NoiseOffsetZ;

	void SetNumCells(int32 NumMeshes)
	{
		CellMeshes.Reset();
		for (int32 Idx = 0; Idx < NumMeshes; Idx++)
		{
			CellMeshes.Add(new FCellInfo(NumUVLayers));
		}
	}

	FCellMeshes(int32 NumUVLayers)
	{
		InitEmpty();
	}

	FCellMeshes(int32 NumUVLayers, const FPlanarCells& Cells, FAxisAlignedBox3d DomainBounds, double Grout, double ExtendDomain, bool bIncludeOutsideCell);

	FCellMeshes(int32 NumUVLayers, FDynamicMesh3& SingleCutter, const FInternalSurfaceMaterials& Materials, TOptional<FTransform> Transform);

	// Special function to just make the "grout" part of the planar mesh cells
	// Used to make the multi-plane cuts with grout easier to implement
	void MakeOnlyPlanarGroutCell(const FPlanarCells& Cells, FAxisAlignedBox3d DomainBounds, double Grout);

	void RemeshForNoise(FDynamicMesh3& Mesh, EEdgeRefineFlags EdgeFlags, double TargetEdgeLen);

	float OctaveNoise(const FVector& V, const FNoiseSettings& Settings);

	FVector NoiseVector(const FVector& Pos, const FNoiseSettings& Settings);

	FVector3d NoiseDisplacement(const FVector3d& Pos, const FNoiseSettings& Settings);

	void ApplyNoise(FDynamicMesh3& Mesh, FVector3d Normal, const FNoiseSettings& Settings, bool bProjectBoundariesToNormal = false);

	/**
	 * Convert plane index to material ID
	 * @return material ID encoding the source plane into a triangle mesh
	 */
	int PlaneToMaterial(int Plane)
	{
		return -(Plane + 1);
	}

	/**
	 * Convert material ID to plane index
	 * @return index of source plane for triangle, or -1 if no such plane
	 */
	int MaterialToPlane(int MaterialID)
	{
		return MaterialID >= 0 ? -1 : -(MaterialID + 1);
	}

	void InitEmpty()
	{
		NoiseOffsetX = FMath::VRand() * 100;
		NoiseOffsetY = FMath::VRand() * 100;
		NoiseOffsetZ = FMath::VRand() * 100;
		OutsideCellIndex = -1;
	}

	void Init(int32 NumUVLayersIn, const FPlanarCells& Cells, FAxisAlignedBox3d DomainBounds, double Grout, double ExtendDomain, bool bIncludeOutsideCell);

	void ApplyGeneralGrout(double Grout);

	void AppendMesh(FDynamicMesh3& Base, FDynamicMesh3& ToAppend, bool bFlipped);

private:

	void CreateMeshesForBoundedPlanesWithoutNoise(int NumCells, const FPlanarCells& Cells, const FAxisAlignedBox3d& DomainBounds, bool bNoise, double GlobalUVScale);

	// Approximately calculate a "safe" spacing that would not require the remesher to create more than a million new vertices
	double GetSafeNoiseSpacing(float SurfaceArea, float TargetSpacing);

	void CreateMeshesForBoundedPlanesWithNoise(int NumCells, const FPlanarCells& Cells, const FAxisAlignedBox3d& DomainBounds, bool bNoise, double GlobalUVScale);
	void CreateMeshesForSinglePlane(const FPlanarCells& Cells, const FAxisAlignedBox3d& DomainBounds, bool bNoise, double GlobalUVScale, double Grout, bool bOnlyGrout);
};


// Holds Geometry from an FGeometryCollection in an FDynamicMesh3 representation, and convert both directions
// Also supports cutting geometry with FCellMeshes
struct PLANARCUT_API FDynamicMeshCollection
{
	struct FMeshData
	{
		FDynamicMesh3 AugMesh;

		// FDynamicMeshAABBTree3 Spatial; // TODO: maybe refactor mesh booleans to allow version where caller provides spatial data; it's computed every boolean now
		// FTransform3d Transform; // TODO: maybe pretransform the data to a space that is good for cutting; refactor mesh boolean so there is an option to have it not transform input
		int32 TransformIndex; // where the mesh was from in the geometry collection
		FTransform FromCollection; // transform that was used to go from the geometry collection to the local space used for processing

		FMeshData(int32 NumUVLayers)
		{
			SetGeometryCollectionAttributes(AugMesh, NumUVLayers);
		}

		FMeshData(const FDynamicMesh3& Mesh, int32 TransformIndex, FTransform FromCollection) : AugMesh(Mesh), TransformIndex(TransformIndex), FromCollection(FromCollection)
		{}

		void SetMesh(const FDynamicMesh3& NewAugMesh)
		{
			ClearCachedBounds();
			AugMesh = NewAugMesh;
		}

		/// Note this relies on the caller to also call ClearCachedBounds() as needed; it will not automatically invalidate any computed bounds
		const FAxisAlignedBox3d& GetCachedBounds()
		{
			if (!bHasBounds)
			{
				Bounds = AugMesh.GetBounds(true);
				bHasBounds = true;
			}
			return Bounds;
		}

		void ClearCachedBounds()
		{
			bHasBounds = false;
		}

	private:
		bool bHasBounds = false;
		FAxisAlignedBox3d Bounds;
	};
	TIndirectArray<FMeshData> Meshes;
	FAxisAlignedBox3d Bounds;
	
	FDynamicMeshCollection(const FGeometryCollection* Collection, const TArrayView<const int32>& TransformIndices, FTransform TransformCollection, bool bSaveIsolatedVertices = false)
	{
		Init(Collection, TransformIndices, TransformCollection, bSaveIsolatedVertices);
	}

	FDynamicMeshCollection(const FGeometryCollection* Collection, const TManagedArray<FTransform>& Transforms, const TArrayView<const int32>& TransformIndices, FTransform TransformCollection, bool bSaveIsolatedVertices = false)
	{
		Init(Collection, Transforms, TransformIndices, TransformCollection, bSaveIsolatedVertices);
	}

	void Init(const FGeometryCollection* Collection, const TArrayView<const int32>& TransformIndices, FTransform TransformCollection, bool bSaveIsolatedVertices = false)
	{
		Init(Collection, Collection->Transform, TransformIndices, TransformCollection, bSaveIsolatedVertices);
	}

	void Init(const FGeometryCollection* Collection, const TManagedArray<FTransform>& Transforms, const TArrayView<const int32>& TransformIndices, FTransform TransformCollection, bool bSaveIsolatedVertices = false);

	int32 CutWithMultiplePlanes(
		const TArrayView<const FPlane>& Planes,
		double Grout,
		double CollisionSampleSpacing,
		FGeometryCollection* Collection,
		FInternalSurfaceMaterials& InternalSurfaceMaterials,
		bool bSetDefaultInternalMaterialsFromCollection
	);

	/**
	 * Cut collection meshes with cell meshes, and append results to a geometry collection
	 *
	 * @param InternalSurfaceMaterials Internal material info (used for material ID)
	 * @param CellConnectivity The connectivity between cells: PlaneTag -> The two cells separated by triangles with this tag
	 * @param CellsMeshes Meshed versions of the cells, with noise and material properties baked in
	 * @param Collection Results will be stored in this
	 * @param bSetDefaultInternalMaterialsFromCollection If true, set internal materials to the most common external material + 1, following a convenient artist convention
	 * @return Index of the first created geometry
	 */
	int32 CutWithCellMeshes(const FInternalSurfaceMaterials& InternalSurfaceMaterials, const TArray<TPair<int32, int32>>& CellConnectivity, FCellMeshes& CellMeshes, FGeometryCollection* Collection, bool bSetDefaultInternalMaterialsFromCollection, double CollisionSampleSpacing);

	static void SetVisibility(FGeometryCollection& Collection, int32 GeometryIdx, bool bVisible)
	{
		int32 FaceEnd = Collection.FaceCount[GeometryIdx] + Collection.FaceStart[GeometryIdx];
		for (int32 FaceIdx = Collection.FaceStart[GeometryIdx]; FaceIdx < FaceEnd; FaceIdx++)
		{
			Collection.Visible[FaceIdx] = bVisible;
		}
	}

	// Split mesh into connected components, including implicit connections by co-located vertices
	bool SplitIslands(FDynamicMesh3& Source, TArray<FDynamicMesh3>& SeparatedMeshes);

	FString GetBoneName(FGeometryCollection& Output, int TransformParent, int SubPartIndex)
	{
		return Output.BoneName[TransformParent] + "_" + FString::FromInt(SubPartIndex);
	}

	void AddCollisionSamples(double CollisionSampleSpacing);

	// Update all geometry in a GeometryCollection w/ the meshes in the MeshCollection
	// Resizes the GeometryCollection as needed
	bool UpdateAllCollections(FGeometryCollection& Collection);

	static int32 AppendToCollection(const FTransform& FromCollection, FDynamicMesh3& Mesh, double CollisionSampleSpacing, int32 TransformParent, FString BoneName, FGeometryCollection& Output, int32 InternalMaterialID);

private:

	void SetGeometryVisibility(FGeometryCollection* Collection, const TArray<int32>& GeometryIndices, bool bVisible);

	// Update an existing geometry in a collection w/ a new mesh (w/ the same number of faces and vertices!)
	static bool UpdateCollection(const FTransform& FromCollection, FDynamicMesh3& Mesh, int32 GeometryIdx, FGeometryCollection& Output, int32 InternalMaterialID);

	void FillVertexHash(const FDynamicMesh3& Mesh, TPointHashGrid3d<int>& VertHash);

	bool IsNeighboring(
		FDynamicMesh3& MeshA, const TPointHashGrid3d<int>& VertHashA, const FAxisAlignedBox3d& BoundsA,
		FDynamicMesh3& MeshB, const TPointHashGrid3d<int>& VertHashB, const FAxisAlignedBox3d& BoundsB);
};


}} // namespace UE::PlanarCut