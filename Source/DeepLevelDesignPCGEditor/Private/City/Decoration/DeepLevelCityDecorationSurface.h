// Copyright <--\, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

class AActor;
struct FHitResult;

/** Editor-only render triangles of preview actors, independent of gameplay collision. */
class FDeepLevelCityDecorationSurface
{
public:
	void Build(const AActor* Building);
	void UpdateActorTransform(const FTransform& Transform) { ActorTransform = Transform; }
	bool GetContactTransform(const FTransform& Desired, const FVector& Point, const FVector& Normal, FTransform& Result) const;
	bool Trace(const FVector& Start, const FVector& End, FHitResult& Hit) const;
	bool IsEmpty() const { return Meshes.IsEmpty(); }
private:
	struct FMesh
	{
		TArray<FVector> Vertices;
		TArray<uint32> Indices;
		TArray<FTransform> Transforms;
		FBox Bounds = FBox(ForceInit);
	};
	TArray<FMesh> Meshes;
	FTransform ActorTransform = FTransform::Identity;
};
