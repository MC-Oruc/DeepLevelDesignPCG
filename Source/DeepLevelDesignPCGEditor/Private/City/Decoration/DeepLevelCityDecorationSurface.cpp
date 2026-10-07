// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/DeepLevelCityDecorationSurface.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "StaticMeshResources.h"

void FDeepLevelCityDecorationSurface::Build(const AActor* Building)
{
	Meshes.Reset();
	if (!Building) { return; }
	ActorTransform = Building->GetActorTransform();
	TInlineComponentArray<UStaticMeshComponent*> Components;
	Building->GetComponents(Components, true);
	TMap<UStaticMesh*, int32> MeshIndices;
	for (const UStaticMeshComponent* Component : Components)
	{
		UStaticMesh* Asset = Component->GetStaticMesh();
		if (!Asset || !Component->IsVisible()) { continue; }
		int32* Index = MeshIndices.Find(Asset);
		if (!Index)
		{
			const FStaticMeshRenderData* Data = Asset->GetRenderData();
			if (!Data || Data->LODResources.IsEmpty()) { continue; }
			const FStaticMeshLODResources& LOD = Data->LODResources[0];
			if (!LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices() || !LOD.IndexBuffer.GetNumIndices()) { continue; }
			FMesh& Mesh = Meshes.Emplace_GetRef();
			for (uint32 Vertex = 0; Vertex < LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices(); ++Vertex)
			{
				const FVector Position(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Vertex));
				Mesh.Vertices.Add(Position);
				Mesh.Bounds += Position;
			}
			const FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
			for (int32 TriangleIndex = 0; TriangleIndex < Indices.Num(); ++TriangleIndex) { Mesh.Indices.Add(Indices[TriangleIndex]); }
			Index = &MeshIndices.Add(Asset, Meshes.Num() - 1);
		}
		FMesh& Mesh = Meshes[*Index];
		if (const auto* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			for (int32 Instance = 0; Instance < Instanced->GetInstanceCount(); ++Instance)
			{
				FTransform Transform;
				if (Instanced->GetInstanceTransform(Instance, Transform, true)) { Mesh.Transforms.Add(Transform.GetRelativeTransform(ActorTransform)); }
			}
		}
		else { Mesh.Transforms.Add(Component->GetComponentTransform().GetRelativeTransform(ActorTransform)); }
	}
}

bool FDeepLevelCityDecorationSurface::Trace(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
	bool bFound = false;
	double ClosestSquared = FVector::DistSquared(Start, End);
	for (const FMesh& Mesh : Meshes)
	{
		for (const FTransform& Relative : Mesh.Transforms)
		{
			const FTransform Transform = Relative * ActorTransform;
			const FVector LocalStart = Transform.InverseTransformPosition(Start);
			const FVector LocalEnd = Transform.InverseTransformPosition(End);
			if (!FMath::LineBoxIntersection(Mesh.Bounds, LocalStart, LocalEnd, LocalEnd - LocalStart)) { continue; }
			for (int32 Index = 0; Index + 2 < Mesh.Indices.Num(); Index += 3)
			{
				FVector Position, Normal;
				if (!FMath::SegmentTriangleIntersection(LocalStart, LocalEnd, Mesh.Vertices[Mesh.Indices[Index]],
					Mesh.Vertices[Mesh.Indices[Index + 1]], Mesh.Vertices[Mesh.Indices[Index + 2]], Position, Normal)) { continue; }
				const FVector WorldPosition = Transform.TransformPosition(Position);
				const double DistanceSquared = FVector::DistSquared(Start, WorldPosition);
				if (DistanceSquared >= ClosestSquared) { continue; }
				ClosestSquared = DistanceSquared;
				Hit.ImpactPoint = Hit.Location = WorldPosition;
				Hit.ImpactNormal = Hit.Normal = FVector(Transform.ToInverseMatrixWithScale().GetTransposed().TransformVector(Normal)).GetSafeNormal();
				if (FVector::DotProduct(Hit.ImpactNormal, End - Start) > 0) { Hit.ImpactNormal *= -1.0; Hit.Normal = Hit.ImpactNormal; }
				Hit.bBlockingHit = true;
				Hit.Distance = FMath::Sqrt(DistanceSquared);
				bFound = true;
			}
		}
	}
	return bFound;
}


bool FDeepLevelCityDecorationSurface::GetContactTransform(const FTransform& Desired,
	const FVector& Point, const FVector& Normal, FTransform& Result) const
{
	const FVector Direction = Normal.GetSafeNormal();
	if (Direction.IsNearlyZero() || Point.ContainsNaN() || Desired.ContainsNaN()) { return false; }
	double Support = TNumericLimits<double>::Max();
	bool bFound = false;
	for (const FMesh& Mesh : Meshes)
	{
		for (const FTransform& Relative : Mesh.Transforms)
		{
			const FTransform Transform = Relative * Desired;
			for (const FVector& Vertex : Mesh.Vertices)
			{
				Support = FMath::Min(Support, FVector::DotProduct(Transform.TransformPosition(Vertex) - Desired.GetLocation(), Direction));
				bFound = true;
			}
		}
	}
	if (!bFound) { return false; }
	Result = Desired;
	Result.SetLocation(Point - Direction * Support);
	return true;
}
