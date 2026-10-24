// Copyright <--\, Inc. All Rights Reserved.
#include "Building/DeepLevelBuildingFacadeVisibility.h"
#include "City/DeepLevelCityLayout.h"
#include "Curve/PolygonIntersectionUtils.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingFacades"

namespace
{
using namespace UE::Geometry;
constexpr double BoundaryTolerance = 0.1;
constexpr double ProbeOffset = 1.0;

bool IsXFace(EDeepLevelBuildingVolumeFace Face)
{
	return Face == EDeepLevelBuildingVolumeFace::PositiveX || Face == EDeepLevelBuildingVolumeFace::NegativeX;
}

FVector FacadeNormal(EDeepLevelBuildingVolumeFace Face)
{
	switch (Face)
	{
	case EDeepLevelBuildingVolumeFace::PositiveX: return FVector::ForwardVector;
	case EDeepLevelBuildingVolumeFace::NegativeX: return -FVector::ForwardVector;
	case EDeepLevelBuildingVolumeFace::PositiveY: return FVector::RightVector;
	case EDeepLevelBuildingVolumeFace::NegativeY: return -FVector::RightVector;
	}
	checkNoEntry();
	return FVector::ZeroVector;
}

double PathsLength(const TArray<TArray<FVector2d>>& Paths)
{
	double Length = 0.0;
	for (const auto& Path : Paths)
	{
		for (int32 I = 1; I < Path.Num(); ++I) { Length += (Path[I] - Path[I - 1]).Length(); }
	}
	return Length;
}

bool ClipPath(TArray<FVector2d>& Points, TConstArrayView<FGeneralPolygon2d> Regions, TArray<TArray<FVector2d>>& Result)
{
	Result.Reset();
	if (Regions.IsEmpty()) { return true; }
	TArray<TArrayView<FVector2d>> Paths{MakeArrayView(Points)};
	return PathsIntersection(Paths, Regions, Result);
}

bool TouchesBorder(const FGeneralPolygon2d& Region, const FBox2D& Bounds)
{
	for (const auto& Point : Region.GetOuter().GetVertices())
	{
		if (FMath::Abs(Point.X - Bounds.Min.X) <= BoundaryTolerance || FMath::Abs(Point.X - Bounds.Max.X) <= BoundaryTolerance
			|| FMath::Abs(Point.Y - Bounds.Min.Y) <= BoundaryTolerance || FMath::Abs(Point.Y - Bounds.Max.Y) <= BoundaryTolerance) { return true; }
	}
	return false;
}
}

bool FDeepLevelBuildingFacadeGeometry::IsValid() const
{
	return LocalFrame.IsValid() && LocalFrame.GetScale3D().GetAbsMin() > UE_DOUBLE_SMALL_NUMBER
		&& !Extent.ContainsNaN() && Extent.GetMin() > 0.0 && (ForbiddenFaces & ~uint8(15)) == 0;
}

EDeepLevelBuildingVolumeFace DeepLevelBuildingFacades::FindNearestFace(
	const FDeepLevelBuildingFacadeGeometry& Geometry, const FVector& ActorLocalPoint)
{
	const FVector Local = Geometry.LocalFrame.InverseTransformPosition(ActorLocalPoint);
	const double Distances[] = {FMath::Abs(Local.X - Geometry.Extent.X), FMath::Abs(Local.X + Geometry.Extent.X),
		FMath::Abs(Local.Y - Geometry.Extent.Y), FMath::Abs(Local.Y + Geometry.Extent.Y)};
	uint8 Nearest = 0;
	for (uint8 Face = 1; Face < 4; ++Face) { if (Distances[Face] < Distances[Nearest]) { Nearest = Face; } }
	return static_cast<EDeepLevelBuildingVolumeFace>(Nearest);
}

bool DeepLevelBuildingFacades::IsPointVisible(const FDeepLevelCityBuilding& Building,
	EDeepLevelBuildingVolumeFace Face, const FVector& ActorLocalPoint)
{
	const uint8 FaceIndex = static_cast<uint8>(Face);
	if (FaceIndex >= 4 || !Building.FacadeGeometry.IsValid()
		|| (Building.FacadeGeometry.ForbiddenFaces & (1 << FaceIndex)) != 0) { return false; }
	const FVector Local = Building.FacadeGeometry.LocalFrame.InverseTransformPosition(ActorLocalPoint);
	const double HalfSpan = IsXFace(Face) ? Building.FacadeGeometry.Extent.Y : Building.FacadeGeometry.Extent.X;
	const double Position = 0.5 * (1.0 + (IsXFace(Face) ? Local.Y : Local.X) / HalfSpan);
	const double Tolerance = BoundaryTolerance / (2.0 * HalfSpan);
	return Building.VisibleFacades.ContainsByPredicate([&](const FDeepLevelBuildingFacadeSpan& Span)
	{
		return Span.Face == Face && Position >= Span.Start - Tolerance && Position <= Span.End + Tolerance;
	});
}

bool DeepLevelBuildingFacades::Resolve(TArray<FDeepLevelCityBuilding>& Buildings,
	const FDeepLevelCityGrid& Grid, const TMap<FIntPoint, FDeepLevelCityCellState>& Cells, FText& Error)
{
	if (Buildings.IsEmpty()) { Error = FText::GetEmpty(); return true; }
	TArray<const FDeepLevelCityBuilding*> Ordered;
	for (auto& Building : Buildings)
	{
		Building.VisibleFacades.Reset();
		Building.NeighborFaces = Building.EnclosedFaces = 0;
		if (!Building.FacadeGeometry.IsValid() || !Building.Transform.IsValid()
			|| Building.Transform.GetScale3D().GetAbsMin() <= UE_DOUBLE_SMALL_NUMBER)
		{
			Error = LOCTEXT("MissingGeometry", "Published buildings require valid calibrated facade geometry.");
			return false;
		}
		Ordered.Add(&Building);
	}
	Ordered.Sort([](const auto& A, const auto& B) { return A.StableId < B.StableId; });
	TArray<FGeneralPolygon2d> Footprints;
	FBox2D Bounds(ForceInit);
	for (const auto* Building : Ordered)
	{
		const auto& Geometry = Building->FacadeGeometry;
		const FTransform Frame = Geometry.LocalFrame * Building->Transform;
		if (FMath::Abs(Frame.TransformVector(FVector::UpVector).GetSafeNormal().Z) < 1.0 - UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			Error = LOCTEXT("NonPlanarFootprint", "Building facade visibility requires upright placement volumes on the flat City Layout grid.");
			return false;
		}
		TArray<FVector2d> Corners;
		for (const FVector2D& Sign : {FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1)})
		{
			const FVector World = Frame.TransformPosition(FVector(Sign.X * Geometry.Extent.X, Sign.Y * Geometry.Extent.Y, 0));
			Corners.Emplace(World.X, World.Y);
			Bounds += FVector2D(World.X, World.Y);
		}
		FPolygon2d Polygon(Corners);
		if (FMath::Abs(Polygon.SignedArea()) <= BoundaryTolerance * BoundaryTolerance)
		{
			Error = LOCTEXT("DegenerateFootprint", "A published building has a degenerate facade footprint.");
			return false;
		}
		if (Polygon.IsClockwise()) { Polygon.Reverse(); }
		Footprints.Emplace(Polygon);
	}
	Bounds = Bounds.ExpandBy(FMath::Max(Grid.TileSize, 100.0));
	TArray<FVector2d> FrameVertices{Bounds.Min, FVector2d(Bounds.Max.X, Bounds.Min.Y), Bounds.Max, FVector2d(Bounds.Min.X, Bounds.Max.Y)};
	TArray<FGeneralPolygon2d> Domain{FGeneralPolygon2d(FPolygon2d(FrameVertices))}, Solid, Free;
	if (!PolygonsUnion(Footprints, Solid, false) || !PolygonsDifference(Domain, Solid, Free))
	{
		Error = LOCTEXT("TopologyFailed", "Building facade topology could not be computed from the published footprints.");
		return false;
	}
	TArray<FGeneralPolygon2d> Exterior;
	for (const auto& Region : Free)
	{
		bool bExterior = TouchesBorder(Region, Bounds);
		if (!bExterior)
		{
			for (const auto& Pair : Cells)
			{
				if ((Pair.Value.OccupancyMask & static_cast<int32>(EDeepLevelCityOccupancy::Road)) == 0) { continue; }
				const FVector Road = Grid.CellToWorld(Pair.Key);
				if (Region.Contains(FVector2d(Road.X, Road.Y))) { bExterior = true; break; }
			}
		}
		if (bExterior) { Exterior.Add(Region); }
	}
	for (auto& Building : Buildings)
	{
		const auto& Geometry = Building.FacadeGeometry;
		const FTransform Frame = Geometry.LocalFrame * Building.Transform;
		for (uint8 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
		{
			const auto Face = static_cast<EDeepLevelBuildingVolumeFace>(FaceIndex);
			const FVector Normal = FacadeNormal(Face);
			FVector Start = Normal * (IsXFace(Face) ? Geometry.Extent.X : Geometry.Extent.Y), End = Start;
			if (IsXFace(Face)) { Start.Y = -Geometry.Extent.Y; End.Y = Geometry.Extent.Y; }
			else { Start.X = -Geometry.Extent.X; End.X = Geometry.Extent.X; }
			const FVector WorldNormal = FVector(Frame.ToInverseMatrixWithScale().GetTransposed().TransformVector(Normal)).GetSafeNormal();
			const FVector WorldStart = Frame.TransformPosition(Start) + WorldNormal * ProbeOffset;
			const FVector WorldEnd = Frame.TransformPosition(End) + WorldNormal * ProbeOffset;
			TArray<FVector2d> Probe{FVector2d(WorldStart.X, WorldStart.Y), FVector2d(WorldEnd.X, WorldEnd.Y)};
			const FVector2d Direction = Probe[1] - Probe[0];
			const double LengthSquared = Direction.SquaredLength();
			TArray<TArray<FVector2d>> Visible, FreeParts, Blocked;
			if (!ClipPath(Probe, Exterior, Visible) || !ClipPath(Probe, Free, FreeParts) || !ClipPath(Probe, Solid, Blocked))
			{
				Error = LOCTEXT("FaceClipFailed", "A published building facade could not be clipped against city topology.");
				return false;
			}
			if (PathsLength(Blocked) > BoundaryTolerance) { Building.NeighborFaces |= (1 << FaceIndex); }
			if (PathsLength(FreeParts) - PathsLength(Visible) > BoundaryTolerance) { Building.EnclosedFaces |= (1 << FaceIndex); }
			if ((Geometry.ForbiddenFaces & (1 << FaceIndex)) != 0) { continue; }
			TArray<FVector2D> Intervals;
			for (const auto& Path : Visible)
			{
				if (Path.Num() < 2) { continue; }
				const double A = FVector2d::DotProduct(Path[0] - Probe[0], Direction) / LengthSquared;
				const double B = FVector2d::DotProduct(Path.Last() - Probe[0], Direction) / LengthSquared;
				Intervals.Emplace(FMath::Clamp(FMath::Min(A, B), 0.0, 1.0), FMath::Clamp(FMath::Max(A, B), 0.0, 1.0));
			}
			Intervals.Sort([](const FVector2D& A, const FVector2D& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
			const double Tolerance = BoundaryTolerance / FMath::Sqrt(LengthSquared);
			for (const auto& Interval : Intervals)
			{
				if (Interval.Y - Interval.X <= Tolerance) { continue; }
				if (!Building.VisibleFacades.IsEmpty() && Building.VisibleFacades.Last().Face == Face
					&& Interval.X <= Building.VisibleFacades.Last().End + Tolerance)
				{ Building.VisibleFacades.Last().End = FMath::Max(Building.VisibleFacades.Last().End, Interval.Y); }
				else
				{
					auto& Span = Building.VisibleFacades.Emplace_GetRef();
					Span.Face = Face; Span.Start = Interval.X; Span.End = Interval.Y;
				}
			}
		}
	}
	Error = FText::GetEmpty();
	return true;
}

#undef LOCTEXT_NAMESPACE
