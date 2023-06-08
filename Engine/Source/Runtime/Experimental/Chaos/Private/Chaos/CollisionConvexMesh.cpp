// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionConvexMesh.h"
#include "HAL/IConsoleManager.h"

namespace Chaos
{
	// CVars variables for controlling geometry complexity checking and simplification 
	int32 FConvexBuilder::PerformGeometryCheck = 0;

	int32 FConvexBuilder::PerformGeometryReduction = 0;

	int32 FConvexBuilder::VerticesThreshold = 50;

	int32 FConvexBuilder::ComputeHorizonEpsilonFromMeshExtends = 1;

	FAutoConsoleVariableRef CVarConvexGeometryCheckEnable(TEXT("p.Chaos.ConvexGeometryCheckEnable"), FConvexBuilder::PerformGeometryCheck, TEXT("Perform convex geometry complexity check for Chaos physics."));

	FAutoConsoleVariableRef CVarConvexGeometrySimplifyEnable(TEXT("p.Chaos.PerformGeometryReduction"), FConvexBuilder::PerformGeometryReduction, TEXT("Perform convex geometry simplification to increase performance in Chaos physics."));

	FAutoConsoleVariableRef CVarConvexParticlesWarningThreshold(TEXT("p.Chaos.ConvexParticlesWarningThreshold"), FConvexBuilder::VerticesThreshold, TEXT("Threshold beyond which we warn about collision geometry complexity."));
	
	void FConvexBuilder::Build(const TArray<FVec3Type>& InVertices, TArray<FPlaneType>& OutPlanes, TArray<TArray<int32>>& OutFaceIndices, TArray<FVec3Type>& OutVertices, FAABB3Type& OutLocalBounds)
	{
		OutPlanes.Reset();
		OutVertices.Reset();
		OutLocalBounds = FAABB3Type::EmptyAABB();

		const int32 NumVerticesIn = InVertices.Num();
		if (NumVerticesIn == 0)
		{
			return;
		}


		const TArray<FVec3Type>* VerticesToUse = &InVertices;
		TArray<FVec3Type> ModifiedVertices;

		// For triangles and planar shapes, create a very thin prism as a convex
		auto Inflate = [](const TArray<FVec3Type>& Source, TArray<FVec3Type>& Destination, const FVec3Type& Normal, FRealType Inflation)
		{
			const int32 NumSource = Source.Num();
			Destination.Reset();
			Destination.SetNum(NumSource * 2);

			for (int32 Index = 0; Index < NumSource; ++Index)
			{
				Destination[Index] = Source[Index];
				Destination[NumSource + Index] = Source[Index] + Normal * Inflation;
			}
		};

		FVec3Type PlanarNormal(0);
		if (NumVerticesIn == 3)
		{
			const bool bIsValidTriangle = IsValidTriangle(InVertices[0], InVertices[1], InVertices[2], PlanarNormal);

			//TODO_SQ_IMPLEMENTATION: should do proper cleanup to avoid this
			if (ensureMsgf(bIsValidTriangle, TEXT("FConvexBuilder::Build(): Generated invalid triangle!")))
			{
				Inflate(InVertices, ModifiedVertices, PlanarNormal, TriQuadPrismInflation());
				VerticesToUse = &ModifiedVertices;
				UE_LOG(LogChaos, Verbose, TEXT("Encountered a triangle in convex hull generation. Will prepare a prism of thickness %.5f in place of a triangle."), TriQuadPrismInflation());
			}
			else
			{
				return;
			}
		}
		else if (IsPlanarShape(InVertices, PlanarNormal))
		{
			Inflate(InVertices, ModifiedVertices, PlanarNormal, TriQuadPrismInflation());
			VerticesToUse = &ModifiedVertices;
			UE_LOG(LogChaos, Verbose, TEXT("Encountered a planar shape in convex hull generation. Will prepare a prism of thickness %.5f in place of a triangle."), TriQuadPrismInflation());
		}

		const int32 NumVerticesToUse = VerticesToUse->Num();

		OutLocalBounds = FAABB3Type((*VerticesToUse)[0], (*VerticesToUse)[0]);
		for (int32 VertexIndex = 0; VertexIndex < NumVerticesToUse; ++VertexIndex)
		{
			OutLocalBounds.GrowToInclude((*VerticesToUse)[VertexIndex]);
		}

		if (NumVerticesToUse >= 4)
		{
			// @todo(chaos): Deprecate the older convex code path.
			TArray<TVec3<int32>> Indices;
			Params BuildParams;
			BuildParams.HorizonEpsilon = Chaos::FConvexBuilder::SuggestEpsilon(*VerticesToUse);
			BuildConvexHull(*VerticesToUse, Indices, BuildParams);
			OutPlanes.Reserve(Indices.Num());
			TMap<int32, int32> IndexMap; // maps original particle indices to output particle indices
			int32 NewIdx = 0;

			const auto AddIndex = [&IndexMap, &NewIdx](const int32 OriginalIdx)
			{
				if (int32* Idx = IndexMap.Find(OriginalIdx))
				{
					return *Idx;
				}
				IndexMap.Add(OriginalIdx, NewIdx);
				return NewIdx++;
			};

			for (const TVec3<int32>& Idx : Indices)
			{
				FVec3Type Vs[3] = { (*VerticesToUse)[Idx[0]], (*VerticesToUse)[Idx[1]], (*VerticesToUse)[Idx[2]] };
				const FVec3Type Normal = FVec3Type::CrossProduct(Vs[1] - Vs[0], Vs[2] - Vs[0]).GetUnsafeNormal();
				OutPlanes.Add(FPlaneType(Vs[0], Normal));
				TArray<int32> FaceIndices;
				FaceIndices.SetNum(3);
				FaceIndices[0] = AddIndex(Idx[0]);
				FaceIndices[1] = AddIndex(Idx[1]);
				FaceIndices[2] = AddIndex(Idx[2]);
				OutFaceIndices.Add(FaceIndices);
			}

			OutVertices.SetNum(IndexMap.Num());
			for (const auto& Elem : IndexMap)
			{
				OutVertices[Elem.Value] = (*VerticesToUse)[Elem.Key];
			}
		}

		UE_CLOG(OutVertices.Num() == 0, LogChaos, Warning, TEXT("Convex hull generation produced zero convex particles, collision will fail for this primitive."));
	}

}
