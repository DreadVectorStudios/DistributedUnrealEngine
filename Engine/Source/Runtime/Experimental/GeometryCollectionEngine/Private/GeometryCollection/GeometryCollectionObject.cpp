// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: UGeometryCollection methods.
=============================================================================*/
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionCache.h"
#include "UObject/DestructionObjectVersion.h"
#include "Serialization/ArchiveCountMem.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/Package.h"
#include "Materials/MaterialInstance.h"
#include "ProfilingDebugging/CookStats.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/PhysicsSettings.h"


#if WITH_EDITOR
#include "GeometryCollection/DerivedDataGeometryCollectionCooker.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "GeometryCollection/GeometryCollectionEngineSizeSpecificUtility.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
// TODO: Temp until new asset-agnostic builder API
#include "StaticMeshResources.h"
#endif

#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"
#include "Chaos/ChaosArchive.h"
#include "GeometryCollectionProxyData.h"

DEFINE_LOG_CATEGORY_STATIC(LogGeometryCollectionInternal, Log, All);

bool bGeometryCollectionEnableForcedConvexGenerationInSerialize = true;
FAutoConsoleVariableRef CVarGeometryCollectionEnableForcedConvexGenerationInSerialize(
	TEXT("p.GeometryCollectionEnableForcedConvexGenerationInSerialize"),
	bGeometryCollectionEnableForcedConvexGenerationInSerialize,
	TEXT("Enable generation of convex geometry on older destruction files.[def:true]"));


#if ENABLE_COOK_STATS
namespace GeometryCollectionCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("GeometryCollection.Usage"), TEXT(""));
	});
}
#endif

UGeometryCollection::UGeometryCollection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	#if WITH_EDITOR
	, bManualDataCreate(false)
	#endif
	, EnableClustering(true)
	, ClusterGroupIndex(0)
	, MaxClusterLevel(100)
	, DamageThreshold({ 500000.f, 50000.f, 5000.f })
	, ClusterConnectionType(EClusterConnectionTypeEnum::Chaos_MinimalSpanningSubsetDelaunayTriangulation)
	, bUseFullPrecisionUVs(false)
#if WITH_EDITORONLY_DATA
	, CollisionType_DEPRECATED(ECollisionTypeEnum::Chaos_Volumetric)
	, ImplicitType_DEPRECATED(EImplicitTypeEnum::Chaos_Implicit_Convex)
	, MinLevelSetResolution_DEPRECATED(10)
	, MaxLevelSetResolution_DEPRECATED(10)
	, MinClusterLevelSetResolution_DEPRECATED(50)
	, MaxClusterLevelSetResolution_DEPRECATED(50)
	, CollisionObjectReductionPercentage_DEPRECATED(0.0f)
#endif
	, bMassAsDensity(true)
	, Mass(2500.0f)
	, MinimumMassClamp(0.1f)
	, bUpdateMaterialFadeAmount(false)
	, bRemoveOnMaxSleep(false)
	, bShrinkOnMaxSleep(true) // true to keep backward compatibility
	, MaximumSleepTime(5.0, 10.0)
	, RemovalDuration(2.5, 5.0)
	, EnableRemovePiecesOnFracture(false)
	, GeometryCollection(new FGeometryCollection())
{
	PersistentGuid = FGuid::NewGuid();
	InvalidateCollection();
#if WITH_EDITOR
	SimulationDataGuid = StateGuid;
#endif
}

FGeometryCollectionLevelSetData::FGeometryCollectionLevelSetData()
	: MinLevelSetResolution(5)
	, MaxLevelSetResolution(10)
	, MinClusterLevelSetResolution(25)
	, MaxClusterLevelSetResolution(50)
{
}

FGeometryCollectionCollisionParticleData::FGeometryCollectionCollisionParticleData()
	: CollisionParticlesFraction(1.0f)
	, MaximumCollisionParticles(60)
{
}



FGeometryCollectionCollisionTypeData::FGeometryCollectionCollisionTypeData()
	: CollisionType(ECollisionTypeEnum::Chaos_Volumetric)
	, ImplicitType(EImplicitTypeEnum::Chaos_Implicit_Convex)
	, LevelSet()
	, CollisionParticles()
	, CollisionObjectReductionPercentage(0.0f)
	, CollisionMarginFraction(0.f)
{
}

FGeometryCollectionRemovalData::FGeometryCollectionRemovalData()
	: bRemoveOnMaxSleep(false)
	, bShrinkOnMaxSleep(true)
	, MaximumSleepTime(5.0, 10.0)
	, RemovalDuration(2.5, 5.0)
{
}

FGeometryCollectionSizeSpecificData::FGeometryCollectionSizeSpecificData()
	: MaxSize(99999.9)
	, CollisionShapes({ FGeometryCollectionCollisionTypeData()})
#if WITH_EDITORONLY_DATA
	, CollisionType_DEPRECATED(ECollisionTypeEnum::Chaos_Volumetric)
	, ImplicitType_DEPRECATED(EImplicitTypeEnum::Chaos_Implicit_Convex)
	, MinLevelSetResolution_DEPRECATED(5)
	, MaxLevelSetResolution_DEPRECATED(10)
	, MinClusterLevelSetResolution_DEPRECATED(25)
	, MaxClusterLevelSetResolution_DEPRECATED(50)
	, CollisionObjectReductionPercentage_DEPRECATED(0)
	, CollisionParticlesFraction_DEPRECATED(1.f)
	, MaximumCollisionParticles_DEPRECATED(60)
#endif
	, DamageThreshold(5000.0)
{
}


bool FGeometryCollectionSizeSpecificData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
	return false;	//We only have this function to mark custom GUID. Still want serialize tagged properties
}

#if WITH_EDITORONLY_DATA
void FGeometryCollectionSizeSpecificData::PostSerialize(const FArchive& Ar)
{
	const int32 PhysicsObjectVersion = Ar.CustomVer(FPhysicsObjectVersion::GUID);
	// make sure to load back the deprecated values in the new structure if necessary
	// IMPORTANT : this was merge backed in UE4 and PhysicsObjectVersion had to be used,
	//		that's why we need to test both version to make sure backward asset compatibility is maintained
	if (Ar.IsLoading() && PhysicsObjectVersion < FPhysicsObjectVersion::GeometryCollectionUserDefinedCollisionShapes)
	{
		if (CollisionShapes.Num())
		{
			// @todo(chaos destruction collisions) : Add support for many
			CollisionShapes[0].CollisionType = CollisionType_DEPRECATED;
			CollisionShapes[0].ImplicitType = ImplicitType_DEPRECATED;
			CollisionShapes[0].CollisionObjectReductionPercentage = CollisionObjectReductionPercentage_DEPRECATED;
			CollisionShapes[0].CollisionMarginFraction = UPhysicsSettings::Get()->SolverOptions.CollisionMarginFraction;
			CollisionShapes[0].CollisionParticles.CollisionParticlesFraction = CollisionParticlesFraction_DEPRECATED;
			CollisionShapes[0].CollisionParticles.MaximumCollisionParticles = MaximumCollisionParticles_DEPRECATED;
			CollisionShapes[0].LevelSet.MinLevelSetResolution = MinLevelSetResolution_DEPRECATED;
			CollisionShapes[0].LevelSet.MaxLevelSetResolution = MaxLevelSetResolution_DEPRECATED;
			CollisionShapes[0].LevelSet.MinClusterLevelSetResolution = MinClusterLevelSetResolution_DEPRECATED;
			CollisionShapes[0].LevelSet.MaxClusterLevelSetResolution = MaxClusterLevelSetResolution_DEPRECATED;
		}
	}
}
#endif

void FillSharedSimulationSizeSpecificData(FSharedSimulationSizeSpecificData& ToData, const FGeometryCollectionSizeSpecificData& FromData)
{
	ToData.MaxSize = FromData.MaxSize;

	ToData.CollisionShapesData.SetNumUninitialized(FromData.CollisionShapes.Num());

	if (FromData.CollisionShapes.Num())
	{
		for (int i = 0; i < FromData.CollisionShapes.Num(); i++)
		{
			ToData.CollisionShapesData[i].CollisionType = FromData.CollisionShapes[i].CollisionType;
			ToData.CollisionShapesData[i].ImplicitType = FromData.CollisionShapes[i].ImplicitType;
			ToData.CollisionShapesData[i].LevelSetData.MinLevelSetResolution = FromData.CollisionShapes[i].LevelSet.MinLevelSetResolution;
			ToData.CollisionShapesData[i].LevelSetData.MaxLevelSetResolution = FromData.CollisionShapes[i].LevelSet.MaxLevelSetResolution;
			ToData.CollisionShapesData[i].LevelSetData.MinClusterLevelSetResolution = FromData.CollisionShapes[i].LevelSet.MinClusterLevelSetResolution;
			ToData.CollisionShapesData[i].LevelSetData.MaxClusterLevelSetResolution = FromData.CollisionShapes[i].LevelSet.MaxClusterLevelSetResolution;
			ToData.CollisionShapesData[i].CollisionObjectReductionPercentage = FromData.CollisionShapes[i].CollisionObjectReductionPercentage;
			ToData.CollisionShapesData[i].CollisionMarginFraction = FromData.CollisionShapes[i].CollisionMarginFraction;
			ToData.CollisionShapesData[i].CollisionParticleData.CollisionParticlesFraction = FromData.CollisionShapes[i].CollisionParticles.CollisionParticlesFraction;
			ToData.CollisionShapesData[i].CollisionParticleData.MaximumCollisionParticles = FromData.CollisionShapes[i].CollisionParticles.MaximumCollisionParticles;
		}
	}
	ToData.DamageThreshold = FromData.DamageThreshold;
}

FGeometryCollectionSizeSpecificData UGeometryCollection::GeometryCollectionSizeSpecificDataDefaults() 
{
	FGeometryCollectionSizeSpecificData Data;

	Data.MaxSize = 99999.9;
	if (Data.CollisionShapes.Num())
	{
		Data.CollisionShapes[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
		Data.CollisionShapes[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Capsule;
		Data.CollisionShapes[0].LevelSet.MinLevelSetResolution = 5;
		Data.CollisionShapes[0].LevelSet.MaxLevelSetResolution = 10;
		Data.CollisionShapes[0].LevelSet.MinClusterLevelSetResolution = 25;
		Data.CollisionShapes[0].LevelSet.MaxClusterLevelSetResolution = 50;
		Data.CollisionShapes[0].CollisionObjectReductionPercentage = 1.0;
		Data.CollisionShapes[0].CollisionMarginFraction = UPhysicsSettings::Get()->SolverOptions.CollisionMarginFraction;
		Data.CollisionShapes[0].CollisionParticles.CollisionParticlesFraction = 1.0;
		Data.CollisionShapes[0].CollisionParticles.MaximumCollisionParticles = 60;
	}
	Data.DamageThreshold = 5000.0f;
	return Data;
}

const FGeometryCollectionSizeSpecificData& UGeometryCollection::GetSizeSpecificDataByRelativeSize(float RelativeSize) const
{
	check(SizeSpecificData.Num());
	int32 UseIdx = 0;
	float PreviousMaxSize = FLT_MAX;
	for (int32 Idx = SizeSpecificData.Num() - 1; Idx >= 0; --Idx)
	{
		ensureMsgf(PreviousMaxSize >= SizeSpecificData[Idx].MaxSize, TEXT("SizeSpecificData is not sorted"));
		PreviousMaxSize = SizeSpecificData[Idx].MaxSize;
		if (RelativeSize < SizeSpecificData[Idx].MaxSize)
			UseIdx = Idx;
		else
			break;
	}
	return SizeSpecificData[UseIdx];
}

void UGeometryCollection::ValidateSizeSpecificDataDefaults()
{
	auto HasDefault = [](const TArray<FGeometryCollectionSizeSpecificData>& DatasIn)
	{
		const float DefaultMaxSize = static_cast<float>(99999.9);
		for (const FGeometryCollectionSizeSpecificData& Data : DatasIn)
		{
			if (Data.MaxSize >= DefaultMaxSize)
			{
				return true;
			}
		}
		return false;
	};

	if (!SizeSpecificData.Num() || !HasDefault(SizeSpecificData))
	{
		FGeometryCollectionSizeSpecificData Data = GeometryCollectionSizeSpecificDataDefaults();
		if (Data.CollisionShapes.Num())
		{
#if WITH_EDITORONLY_DATA
			Data.CollisionShapes[0].CollisionType = CollisionType_DEPRECATED;
			Data.CollisionShapes[0].ImplicitType = ImplicitType_DEPRECATED;
			Data.CollisionShapes[0].LevelSet.MinLevelSetResolution = MinLevelSetResolution_DEPRECATED;
			Data.CollisionShapes[0].LevelSet.MaxLevelSetResolution = MaxLevelSetResolution_DEPRECATED;
			Data.CollisionShapes[0].LevelSet.MinClusterLevelSetResolution = MinClusterLevelSetResolution_DEPRECATED;
			Data.CollisionShapes[0].LevelSet.MaxClusterLevelSetResolution = MaxClusterLevelSetResolution_DEPRECATED;
			Data.CollisionShapes[0].CollisionObjectReductionPercentage = CollisionObjectReductionPercentage_DEPRECATED;
			Data.CollisionShapes[0].CollisionMarginFraction = UPhysicsSettings::Get()->SolverOptions.CollisionMarginFraction;
#endif
			if (Data.CollisionShapes[0].ImplicitType == EImplicitTypeEnum::Chaos_Implicit_LevelSet)
			{
				Data.CollisionShapes[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			}
		}
		SizeSpecificData.Add(Data);
	}
	check(SizeSpecificData.Num());
}

void UGeometryCollection::UpdateConvexGeometry()
{
#if WITH_EDITOR
	if (GeometryCollection)
	{
		FGeometryCollectionConvexPropertiesInterface::FConvexCreationProperties ConvexProperties = GeometryCollection->GetConvexProperties();
		FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(GeometryCollection.Get(), ConvexProperties.FractionRemove, ConvexProperties.SimplificationThreshold, ConvexProperties.CanExceedFraction);
		InvalidateCollection();
	}
#endif
}


float KgCm3ToKgM3(float Density)
{
	return Density * 1000000;
}

float KgM3ToKgCm3(float Density)
{
	return Density / 1000000;
}

void UGeometryCollection::GetSharedSimulationParams(FSharedSimulationParameters& OutParams) const
{
	const FGeometryCollectionSizeSpecificData& SizeSpecificDefault = GetDefaultSizeSpecificData();

	OutParams.bMassAsDensity = bMassAsDensity;
	OutParams.Mass = bMassAsDensity ? KgM3ToKgCm3(Mass) : Mass;	//todo(ocohen): we still have the solver working in old units. This is mainly to fix ui issues. Long term need to normalize units for best precision
	OutParams.MinimumMassClamp = MinimumMassClamp;

	FGeometryCollectionSizeSpecificData InfSize;
	if (SizeSpecificDefault.CollisionShapes.Num())
	{
		InfSize.CollisionShapes.SetNum(1); // @todo(chaos destruction collisions) : Add support for multiple shapes. 
		OutParams.MaximumCollisionParticleCount = SizeSpecificDefault.CollisionShapes[0].CollisionParticles.MaximumCollisionParticles;

		ECollisionTypeEnum SelectedCollisionType = SizeSpecificDefault.CollisionShapes[0].CollisionType;

		if (SelectedCollisionType == ECollisionTypeEnum::Chaos_Volumetric && SizeSpecificDefault.CollisionShapes[0].ImplicitType == EImplicitTypeEnum::Chaos_Implicit_LevelSet)
		{
			UE_LOG(LogGeometryCollectionInternal, Verbose, TEXT("LevelSet geometry selected but non-particle collisions selected. Forcing particle-implicit collisions for %s"), *GetPathName());
			SelectedCollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		}

		InfSize.CollisionShapes[0].CollisionType = SelectedCollisionType;
		InfSize.CollisionShapes[0].ImplicitType = SizeSpecificDefault.CollisionShapes[0].ImplicitType;
		InfSize.CollisionShapes[0].LevelSet.MinLevelSetResolution = SizeSpecificDefault.CollisionShapes[0].LevelSet.MinLevelSetResolution;
		InfSize.CollisionShapes[0].LevelSet.MaxLevelSetResolution = SizeSpecificDefault.CollisionShapes[0].LevelSet.MaxLevelSetResolution;
		InfSize.CollisionShapes[0].LevelSet.MinClusterLevelSetResolution = SizeSpecificDefault.CollisionShapes[0].LevelSet.MinClusterLevelSetResolution;
		InfSize.CollisionShapes[0].LevelSet.MaxClusterLevelSetResolution = SizeSpecificDefault.CollisionShapes[0].LevelSet.MaxClusterLevelSetResolution;
		InfSize.CollisionShapes[0].CollisionObjectReductionPercentage = SizeSpecificDefault.CollisionShapes[0].CollisionObjectReductionPercentage;
		InfSize.CollisionShapes[0].CollisionMarginFraction = SizeSpecificDefault.CollisionShapes[0].CollisionMarginFraction;
		InfSize.CollisionShapes[0].CollisionParticles.CollisionParticlesFraction = SizeSpecificDefault.CollisionShapes[0].CollisionParticles.CollisionParticlesFraction;
		InfSize.CollisionShapes[0].CollisionParticles.MaximumCollisionParticles = SizeSpecificDefault.CollisionShapes[0].CollisionParticles.MaximumCollisionParticles;
	}
	InfSize.MaxSize = 99999.9;
	OutParams.SizeSpecificData.SetNum(SizeSpecificData.Num() + 1);
	FillSharedSimulationSizeSpecificData(OutParams.SizeSpecificData[0], InfSize);

	for (int32 Idx = 0; Idx < SizeSpecificData.Num(); ++Idx)
	{
		FillSharedSimulationSizeSpecificData(OutParams.SizeSpecificData[Idx+1], SizeSpecificData[Idx]);
	}

	if (EnableRemovePiecesOnFracture)
	{
		FixupRemoveOnFractureMaterials(OutParams);
	}

	OutParams.SizeSpecificData.Sort();	//can we do this at editor time on post edit change?
}

void UGeometryCollection::FixupRemoveOnFractureMaterials(FSharedSimulationParameters& SharedParms) const
{
	// Match RemoveOnFracture materials with materials in model and record the material indices

	int32 NumMaterials = Materials.Num();
	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; MaterialIndex++)
	{
		UMaterialInterface* MaterialInfo = Materials[MaterialIndex];

		for (int32 ROFMaterialIndex = 0; ROFMaterialIndex < RemoveOnFractureMaterials.Num(); ROFMaterialIndex++)
		{
			if (MaterialInfo == RemoveOnFractureMaterials[ROFMaterialIndex])
			{
				SharedParms.RemoveOnFractureIndices.Add(MaterialIndex);
				break;
			}
		}

	}
}

void UGeometryCollection::Reset()
{
	if (GeometryCollection.IsValid())
	{
		Modify();
		GeometryCollection->Empty();
		Materials.Empty();
		EmbeddedGeometryExemplar.Empty();
		InvalidateCollection();
	}
}

/** AppendGeometry */
int32 UGeometryCollection::AppendGeometry(const UGeometryCollection & Element, bool ReindexAllMaterials, const FTransform& TransformRoot)
{
	Modify();
	InvalidateCollection();

	// add all materials
	// if there are none, we assume all material assignments in Element are shared by this GeometryCollection
	// otherwise, we assume all assignments come from the contained materials
	int32 MaterialIDOffset = 0;
	if (Element.Materials.Num() > 0)
	{
		MaterialIDOffset = Materials.Num();
		Materials.Append(Element.Materials);
	}	

	return GeometryCollection->AppendGeometry(*Element.GetGeometryCollection(), MaterialIDOffset, ReindexAllMaterials, TransformRoot);
}

/** NumElements */
int32 UGeometryCollection::NumElements(const FName & Group) const
{
	return GeometryCollection->NumElements(Group);
}

/** RemoveElements */
void UGeometryCollection::RemoveElements(const FName & Group, const TArray<int32>& SortedDeletionList)
{
	Modify();
	GeometryCollection->RemoveElements(Group, SortedDeletionList);
	InvalidateCollection();
}

int UGeometryCollection::GetDefaultSizeSpecificDataIndex() const
{
	int LargestIndex = INDEX_NONE;
	float MaxSize = TNumericLimits<float>::Lowest();
	for (int i = 0; i < SizeSpecificData.Num(); i++)
	{
		const float SizeSpecificDataMaxSize = SizeSpecificData[i].MaxSize;
		if (MaxSize < SizeSpecificDataMaxSize)
		{
			MaxSize = SizeSpecificDataMaxSize;
			LargestIndex = i;
		}
	}
	check(LargestIndex != INDEX_NONE && LargestIndex < SizeSpecificData.Num());
	return LargestIndex;
}

/** Size Specific Data Access */
FGeometryCollectionSizeSpecificData& UGeometryCollection::GetDefaultSizeSpecificData()
{
	if (!SizeSpecificData.Num())
	{
		SizeSpecificData.Add(GeometryCollectionSizeSpecificDataDefaults());
	}
	const int DefaultSizeIndex = GetDefaultSizeSpecificDataIndex();
	return SizeSpecificData[DefaultSizeIndex];
}

const FGeometryCollectionSizeSpecificData& UGeometryCollection::GetDefaultSizeSpecificData() const
{
	ensure(SizeSpecificData.Num());
	const int DefaultSizeIndex = GetDefaultSizeSpecificDataIndex();
	return SizeSpecificData[DefaultSizeIndex];
}

/** ReindexMaterialSections */
void UGeometryCollection::ReindexMaterialSections()
{
	Modify();
	GeometryCollection->ReindexMaterials();
	InvalidateCollection();
}

void UGeometryCollection::InitializeMaterials()
{
	Modify();

	// Last Material is the selection one
	UMaterialInterface* BoneSelectedMaterial = LoadObject<UMaterialInterface>(nullptr, GetSelectedMaterialPath(), nullptr, LOAD_None, nullptr);

	TManagedArray<int32>& MaterialID = GeometryCollection->MaterialID;

	// normally we filter out instances of the selection material ID, but if it's actually used on any face we have to keep it
	bool bBoneSelectedMaterialIsUsed = false;
	for (int32 FaceIdx = 0; FaceIdx < MaterialID.Num(); ++FaceIdx)
	{
		int32 FaceMaterialID = MaterialID[FaceIdx];
		if (FaceMaterialID < Materials.Num() && Materials[FaceMaterialID] == BoneSelectedMaterial)
		{
			bBoneSelectedMaterialIsUsed = true;
			break;
		}
	}
	
	// We're assuming that all materials are arranged in pairs, so first we collect these.
	using FMaterialPair = TPair<UMaterialInterface*, UMaterialInterface*>;
	TSet<FMaterialPair> MaterialSet;
	for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
	{
		UMaterialInterface* ExteriorMaterial = Materials[MaterialIndex];
		if (ExteriorMaterial == BoneSelectedMaterial && !bBoneSelectedMaterialIsUsed) // skip unused bone selected material
		{
			continue;
		}
		
		// If we have an odd number of materials, the last material duplicates itself.
		UMaterialInterface* InteriorMaterial = Materials[MaterialIndex];
		while (++MaterialIndex < Materials.Num())
		{
			if (Materials[MaterialIndex] == BoneSelectedMaterial && !bBoneSelectedMaterialIsUsed) // skip bone selected material
			{
				continue;
			}
			InteriorMaterial = Materials[MaterialIndex];
			break;
		}

		MaterialSet.Add(FMaterialPair(ExteriorMaterial, InteriorMaterial));
	}
	
	// create the final material array only containing unique materials
	// alternating exterior and interior materials
	TMap<UMaterialInterface*, int32> ExteriorMaterialPtrToArrayIndex;
	TMap<UMaterialInterface*, int32> InteriorMaterialPtrToArrayIndex;
	TArray<UMaterialInterface*> FinalMaterials;
	for (const FMaterialPair& Curr : MaterialSet)
	{
		// Add base material
		TTuple< UMaterialInterface*, int32> BaseTuple(Curr.Key, FinalMaterials.Add(Curr.Key));
		ExteriorMaterialPtrToArrayIndex.Add(BaseTuple);

		// Add interior material
		TTuple< UMaterialInterface*, int32> InteriorTuple(Curr.Value, FinalMaterials.Add(Curr.Value));
		InteriorMaterialPtrToArrayIndex.Add(InteriorTuple);
	}

	// Reassign material ID for each face given the new consolidated array of materials
	for (int32 Material = 0; Material < MaterialID.Num(); ++Material)
	{
		if (MaterialID[Material] < Materials.Num())
		{
			UMaterialInterface* OldMaterialPtr = Materials[MaterialID[Material]];
			if (MaterialID[Material] % 2 == 0)
			{
				MaterialID[Material] = *ExteriorMaterialPtrToArrayIndex.Find(OldMaterialPtr);
			}
			else
			{
				MaterialID[Material] = *InteriorMaterialPtrToArrayIndex.Find(OldMaterialPtr);
			}
		}
	}

	// Set new material array on the collection
	Materials = FinalMaterials;

	// Last Material is the selection one
	BoneSelectedMaterialIndex = Materials.Add(BoneSelectedMaterial);

	GeometryCollection->ReindexMaterials();
	InvalidateCollection();
}



/** Returns true if there is anything to render */
bool UGeometryCollection::HasVisibleGeometry() const
{
	if(ensureMsgf(GeometryCollection.IsValid(), TEXT("Geometry Collection %s has an invalid internal collection")))
	{
		return GeometryCollection->HasVisibleGeometry();
	}

	return false;
}

struct FPackedHierarchyNode_Old
{
	FSphere		LODBounds[64];
	FSphere		Bounds[64];
	struct
	{
		uint32	MinLODError_MaxParentLODError;
		uint32	ChildStartReference;
		uint32	ResourcePageIndex_NumPages_GroupPartSize;
	} Misc[64];
};

FArchive& operator<<(FArchive& Ar, FPackedHierarchyNode_Old& Node)
{
	for (uint32 i = 0; i < 64; i++)
	{
		Ar << Node.LODBounds[i];
		Ar << Node.Bounds[i];
		Ar << Node.Misc[i].MinLODError_MaxParentLODError;
		Ar << Node.Misc[i].ChildStartReference;
		Ar << Node.Misc[i].ResourcePageIndex_NumPages_GroupPartSize;
	}

	return Ar;
}


struct FPageStreamingState_Old
{
	uint32			BulkOffset;
	uint32			BulkSize;
	uint32			PageUncompressedSize;
	uint32			DependenciesStart;
	uint32			DependenciesNum;
};

FArchive& operator<<(FArchive& Ar, FPageStreamingState_Old& PageStreamingState)
{
	Ar << PageStreamingState.BulkOffset;
	Ar << PageStreamingState.BulkSize;
	Ar << PageStreamingState.PageUncompressedSize;
	Ar << PageStreamingState.DependenciesStart;
	Ar << PageStreamingState.DependenciesNum;
	return Ar;
}

/** Serialize */
void UGeometryCollection::Serialize(FArchive& Ar)
{
	bool bCreateSimulationData = false;
	Ar.UsingCustomVersion(FDestructionObjectVersion::GUID);
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
	
	Chaos::FChaosArchive ChaosAr(Ar);

	// The Geometry Collection we will be archiving. This may be replaced with a transient, stripped back Geometry Collection if we are cooking.
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> ArchiveGeometryCollection = GeometryCollection;

	bool bIsCookedOrCooking = Ar.IsCooking();

#if WITH_EDITOR
	//Early versions did not have tagged properties serialize first
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::GeometryCollectionInDDC)
	{
		if (Ar.IsLoading())
		{
			GeometryCollection->Serialize(ChaosAr);
		}
		else
		{
			ArchiveGeometryCollection->Serialize(ChaosAr);
		}
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::AddedTimestampedGeometryComponentCache)
	{
		if (Ar.IsLoading())
		{
			// Strip old recorded cache data
			int32 DummyNumFrames;
			TArray<TArray<FTransform>> DummyTransforms;

			Ar << DummyNumFrames;
			DummyTransforms.SetNum(DummyNumFrames);
			for (int32 Index = 0; Index < DummyNumFrames; ++Index)
			{
				Ar << DummyTransforms[Index];
			}
		}
	}
	else
#endif
	{
		// Push up the chain to hit tagged properties too
		// This should have always been in here but because we have saved assets
		// from before this line was here it has to be gated
		Super::Serialize(Ar);
	}


	if (!SizeSpecificData.Num())
	{
		ValidateSizeSpecificDataDefaults();
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::DensityUnitsChanged)
	{
		if (bMassAsDensity)
		{
			Mass = KgCm3ToKgM3(Mass);
		}
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) >= FDestructionObjectVersion::GeometryCollectionInDDC)
	{
		Ar << bIsCookedOrCooking;
	}
#if WITH_EDITOR
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) == FDestructionObjectVersion::GeometryCollectionInDDC)
	{
		//This version only saved content into DDC so skip serializing, but copy from DDC at a specific version
		bool bCopyFromDDC = Ar.IsLoading();
		CreateSimulationDataImp(bCopyFromDDC, TEXT("8724C70A140146B5A2F4CF0C16083041"));
	}
#endif
	//new versions serialize geometry collection after tagged properties
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) >= FDestructionObjectVersion::GeometryCollectionInDDCAndAsset)
	{
#if WITH_EDITOR
		if (Ar.IsSaving() && !Ar.IsTransacting())
		{
			CreateSimulationDataImp(/*bCopyFromDDC=*/false);	//make sure content is built before saving
		}
#endif
		if (Ar.IsLoading())
		{
			GeometryCollection->Serialize(ChaosAr);
		}
		else
		{
			ArchiveGeometryCollection->Serialize(ChaosAr);
		}

		// Fix up the type change for implicits here, previously they were unique ptrs, now they're shared
		TManagedArray<TUniquePtr<Chaos::FImplicitObject>>* OldAttr = ArchiveGeometryCollection->FindAttributeTyped<TUniquePtr<Chaos::FImplicitObject>>(FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
		TManagedArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>>* NewAttr = ArchiveGeometryCollection->FindAttributeTyped<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>>(FGeometryDynamicCollection::SharedImplicitsAttribute, FTransformCollection::TransformGroup);
		if (OldAttr)
		{
			if (!NewAttr)
			{
				NewAttr = &ArchiveGeometryCollection->AddAttribute<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>>(FGeometryDynamicCollection::SharedImplicitsAttribute, FTransformCollection::TransformGroup);

				const int32 NumElems = GeometryCollection->NumElements(FTransformCollection::TransformGroup);
				for (int32 Index = 0; Index < NumElems; ++Index)
				{
					(*NewAttr)[Index] = TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>((*OldAttr)[Index].Release());
				}
			}

			ArchiveGeometryCollection->RemoveAttribute(FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
		}
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::GroupAndAttributeNameRemapping)
	{
		ArchiveGeometryCollection->UpdateOldAttributeNames();
		InvalidateCollection();
		bCreateSimulationData = true;
	}


	// will generate convex bodies when they dont exist. 
	if (Ar.CustomVer(FPhysicsObjectVersion::GUID) < FPhysicsObjectVersion::GeometryCollectionConvexDefaults)
	{
#if WITH_EDITOR
		if (bGeometryCollectionEnableForcedConvexGenerationInSerialize)
		{
			if (!FGeometryCollectionConvexUtility::HasConvexHullData(GeometryCollection.Get()) &&
				GeometryCollection::SizeSpecific::UsesImplicitCollisionType(SizeSpecificData, EImplicitTypeEnum::Chaos_Implicit_Convex))
			{
				GeometryCollection::SizeSpecific::SetImplicitCollisionType(SizeSpecificData, EImplicitTypeEnum::Chaos_Implicit_Box, EImplicitTypeEnum::Chaos_Implicit_Convex);
				bCreateSimulationData = true;
				InvalidateCollection();
			}
		}
#endif
	}

#if WITH_EDITORONLY_DATA
	if (bCreateSimulationData)
	{
		CreateSimulationData();
	}

	//for all versions loaded, make sure sim data is up to date
 	if (Ar.IsLoading())
	{
		EnsureDataIsCooked();	//make sure loaded content is built
	}
#endif
}

const TCHAR* UGeometryCollection::GetSelectedMaterialPath()
{
	return TEXT("/Engine/EditorMaterials/GeometryCollection/SelectedGeometryMaterial.SelectedGeometryMaterial");
}

#if WITH_EDITOR

void UGeometryCollection::CreateSimulationDataImp(bool bCopyFromDDC, const TCHAR* OverrideVersion)
{
	COOK_STAT(auto Timer = GeometryCollectionCookStats::UsageStats.TimeSyncWork());

	// Skips the DDC fetch entirely for testing the builder without adding to the DDC
	const static bool bSkipDDC = false;

	//Use the DDC to build simulation data. If we are loading in the editor we then serialize this data into the geometry collection
	TArray<uint8> DDCData;
	FDerivedDataGeometryCollectionCooker* GeometryCollectionCooker = new FDerivedDataGeometryCollectionCooker(*this);
	GeometryCollectionCooker->SetOverrideVersion(OverrideVersion);

	if (GeometryCollectionCooker->CanBuild())
	{
		if (bSkipDDC) 
		{
			GeometryCollectionCooker->Build(DDCData);
			COOK_STAT(Timer.AddMiss(DDCData.Num()));
		}
		else
		{
			bool bBuilt = false;
			const bool bSuccess = GetDerivedDataCacheRef().GetSynchronous(GeometryCollectionCooker, DDCData, &bBuilt);
			COOK_STAT(Timer.AddHitOrMiss(!bSuccess || bBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, DDCData.Num()));
		}

		if (bCopyFromDDC)
		{
			FMemoryReader Ar(DDCData, true);	// Must be persistent for BulkData to serialize
			Chaos::FChaosArchive ChaosAr(Ar);
			GeometryCollection->Serialize(ChaosAr);
		}
	}
}

void UGeometryCollection::CreateSimulationData()
{
	CreateSimulationDataImp(/*bCopyFromDDC=*/false);
	SimulationDataGuid = StateGuid;
}

TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> UGeometryCollection::GenerateMinimalGeometryCollection() const
{
	TMap<FName, TSet<FName>> SkipList;
	static TSet<FName> GeometryGroups{ FGeometryCollection::GeometryGroup, FGeometryCollection::VerticesGroup, FGeometryCollection::FacesGroup };

	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> DuplicateGeometryCollection(new FGeometryCollection());
	DuplicateGeometryCollection->AddAttribute<bool>(FGeometryCollection::SimulatableParticlesAttribute, FTransformCollection::TransformGroup);
	DuplicateGeometryCollection->AddAttribute<FVector>("InertiaTensor", FGeometryCollection::TransformGroup);
	DuplicateGeometryCollection->AddAttribute<float>("Mass", FGeometryCollection::TransformGroup);
	DuplicateGeometryCollection->AddAttribute<FTransform>("MassToLocal", FGeometryCollection::TransformGroup);
	DuplicateGeometryCollection->AddAttribute<FGeometryDynamicCollection::FSharedImplicit>(
		FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
	DuplicateGeometryCollection->CopyMatchingAttributesFrom(*GeometryCollection, &SkipList);

	return DuplicateGeometryCollection;
}

#endif

void UGeometryCollection::InitResources()
{
}

void UGeometryCollection::ReleaseResources()
{
}

void UGeometryCollection::InvalidateCollection()
{
	StateGuid = FGuid::NewGuid();
}

#if WITH_EDITOR
bool UGeometryCollection::IsSimulationDataDirty() const
{
	return StateGuid != SimulationDataGuid;
}
#endif

int32 UGeometryCollection::AttachEmbeddedGeometryExemplar(const UStaticMesh* Exemplar)
{
	FSoftObjectPath NewExemplarPath(Exemplar);
	
	// Check first if the exemplar is already attached
	for (int32 ExemplarIndex = 0; ExemplarIndex < EmbeddedGeometryExemplar.Num(); ++ExemplarIndex)
	{
		if (NewExemplarPath == EmbeddedGeometryExemplar[ExemplarIndex].StaticMeshExemplar)
		{
			return ExemplarIndex;
		}
	}

	return EmbeddedGeometryExemplar.Emplace( NewExemplarPath );
}

void UGeometryCollection::RemoveExemplars(const TArray<int32>& SortedRemovalIndices)
{
	if (SortedRemovalIndices.Num() > 0)
	{
		for (int32 Index = SortedRemovalIndices.Num() - 1; Index >= 0; --Index)
		{
			EmbeddedGeometryExemplar.RemoveAt(Index);
		}
	}
}

FGuid UGeometryCollection::GetIdGuid() const
{
	return PersistentGuid;
}

FGuid UGeometryCollection::GetStateGuid() const
{
	return StateGuid;
}

#if WITH_EDITOR

void UGeometryCollection::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		FName PropertyName = PropertyChangedEvent.Property->GetFName();

		bool bDoInvalidateCollection = false;
		bool bDoEnsureDataIsCooked = false;
		bool bValidateSizeSpecificDataDefaults = false;
		bool bDoUpdateConvexGeometry = false;
		bool bRebuildSimulationData = false;

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UGeometryCollection, bUseFullPrecisionUVs))
		{
			bDoInvalidateCollection = true;
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UGeometryCollection, SizeSpecificData))
		{
			bDoInvalidateCollection = true;
			bDoUpdateConvexGeometry = true;
			bValidateSizeSpecificDataDefaults = true;
			bRebuildSimulationData = true;
		}		
		else if (PropertyName.ToString().Contains(FString("ImplicitType")))
			//SizeSpecificData.Num() && SizeSpecificData[0].CollisionShapes.Num() &&
			//	PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UGeometryCollection, SizeSpecificData[0].CollisionShapes[0].ImplicitType))
		{
				bDoInvalidateCollection = true;
				bDoUpdateConvexGeometry = true;
				bRebuildSimulationData = true;
		}
		else if (PropertyChangedEvent.Property->GetFName() != GET_MEMBER_NAME_CHECKED(UGeometryCollection, Materials))
		{
			bDoInvalidateCollection = true;
			bRebuildSimulationData = true;
		}


		if (bDoInvalidateCollection)
		{
			InvalidateCollection();
		}

		if (bValidateSizeSpecificDataDefaults)
		{
			ValidateSizeSpecificDataDefaults();
		}

		if (bDoUpdateConvexGeometry)
		{
			UpdateConvexGeometry();
		}

		if (bDoEnsureDataIsCooked)
		{
			EnsureDataIsCooked();
		}

		if (bRebuildSimulationData)
		{
			if (!bManualDataCreate)
			{
				CreateSimulationData();
			}
		}
	}
}

bool UGeometryCollection::Modify(bool bAlwaysMarkDirty /*= true*/)
{
	bool bSuperResult = Super::Modify(bAlwaysMarkDirty);

	UPackage* Package = GetOutermost();
	if (Package->IsDirty())
	{
		InvalidateCollection();
	}

	return bSuperResult;
}

void UGeometryCollection::EnsureDataIsCooked(bool bInitResources)
{
	if (StateGuid != LastBuiltGuid)
	{
		CreateSimulationDataImp(/*bCopyFromDDC=*/ true);
		LastBuiltGuid = StateGuid;
	}
}
#endif

void UGeometryCollection::PostLoad()
{
	Super::PostLoad();

	// Initialize rendering resources.
	if (FApp::CanEverRender())
	{
		InitResources();
	}
}

void UGeometryCollection::BeginDestroy()
{
	Super::BeginDestroy();
	ReleaseResources();
}
