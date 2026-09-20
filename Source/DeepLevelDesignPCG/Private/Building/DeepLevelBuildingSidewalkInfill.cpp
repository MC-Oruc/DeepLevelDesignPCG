// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingSidewalkInfill.h"

#include "Building/DeepLevelBuildingLayout.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingSidewalkInfill"

namespace DeepLevelBuildingSidewalkInfill
{
	namespace
	{
		constexpr double Tolerance = 1.0;

		struct FRect
		{
			double MinX = 0.0;
			double MaxX = 0.0;
			double MinY = 0.0;
			double MaxY = 0.0;
			double SurfaceZ = 0.0;
		};

		const FDeepLevelBuildingPlacementDefinition* FindDefinition(
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			const FDeepLevelBuildingLinePlacement& Placement)
		{
			return Catalog.Buildings.FindByPredicate([&Placement](const FDeepLevelBuildingPlacementDefinition& Definition)
			{
				return Definition.BuildingClass.ToSoftObjectPath() == Placement.BuildingClass.ToSoftObjectPath();
			});
		}

		bool MakeRect(
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			const FDeepLevelBuildingLinePlacement& Placement,
			FRect& OutRect)
		{
			const FDeepLevelBuildingPlacementDefinition* Definition = FindDefinition(Catalog, Placement);
			if (!Definition) { return false; }
			const FTransform ActorTransform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
				*Definition, Placement.StreetFace, Placement.PathSample.Location,
				Placement.PathSample.Forward, Placement.PathSample.Right);
			DeepLevelBuildingLayoutGeometry::FFootprint Corners;
			DeepLevelBuildingLayoutGeometry::MakeFootprintCorners(Definition->PlacementVolume, ActorTransform, Corners);
			OutRect.MinX = OutRect.MinY = TNumericLimits<double>::Max();
			OutRect.MaxX = OutRect.MaxY = TNumericLimits<double>::Lowest();
			for (const FVector2D& Corner : Corners)
			{
				OutRect.MinX = FMath::Min(OutRect.MinX, Corner.X);
				OutRect.MaxX = FMath::Max(OutRect.MaxX, Corner.X);
				OutRect.MinY = FMath::Min(OutRect.MinY, Corner.Y);
				OutRect.MaxY = FMath::Max(OutRect.MaxY, Corner.Y);
			}
			for (int32 Index = 0; Index < Corners.Num(); ++Index)
			{
				const FVector2D Edge = Corners[(Index + 1) % Corners.Num()] - Corners[Index];
				if (FMath::Abs(Edge.X) > Tolerance && FMath::Abs(Edge.Y) > Tolerance) { return false; }
			}
			OutRect.SurfaceZ = Placement.PathSample.Location.Z;
			return true;
		}

		bool Contains(const FRect& Rect, const FVector2D& Point)
		{
			return Point.X > Rect.MinX + Tolerance && Point.X < Rect.MaxX - Tolerance
				&& Point.Y > Rect.MinY + Tolerance && Point.Y < Rect.MaxY - Tolerance;
		}

		void AddUniqueCut(TArray<double>& Cuts, const double Value)
		{
			if (!Cuts.ContainsByPredicate([Value](const double Existing) { return FMath::IsNearlyEqual(Existing, Value, Tolerance); }))
			{
				Cuts.Add(Value);
			}
		}

		void AddOccupiedCells(const FDeepLevelCityGrid& Grid, const FRect& Rect, TArray<FIntPoint>& OutCells)
		{
			const FIntPoint Min = Grid.WorldToCell(FVector(Rect.MinX, Rect.MinY, Rect.SurfaceZ));
			const FIntPoint Max = Grid.WorldToCell(FVector(Rect.MaxX, Rect.MaxY, Rect.SurfaceZ));
			for (int32 X = FMath::Min(Min.X, Max.X); X <= FMath::Max(Min.X, Max.X); ++X)
			{
				for (int32 Y = FMath::Min(Min.Y, Max.Y); Y <= FMath::Max(Min.Y, Max.Y); ++Y)
				{
					const FVector Center = Grid.CellToWorld(FIntPoint(X, Y));
					if (Center.X >= Rect.MinX - Tolerance && Center.X <= Rect.MaxX + Tolerance
						&& Center.Y >= Rect.MinY - Tolerance && Center.Y <= Rect.MaxY + Tolerance)
					{
						OutCells.Add(FIntPoint(X, Y));
					}
				}
			}
			if (OutCells.IsEmpty()) { OutCells.Add(Grid.WorldToCell(FVector((Rect.MinX + Rect.MaxX) * 0.5, (Rect.MinY + Rect.MaxY) * 0.5, Rect.SurfaceZ))); }
		}
	}

	bool BuildAnchors(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelCityGrid& Grid,
		const FGuid& SourceGuid,
		const int32 SourceRevision,
		const FDeepLevelBuildingLinePlan& Before,
		const FDeepLevelBuildingLinePlan& After,
		TArray<FDeepLevelCityAnchor>& OutAnchors,
		FText& OutError)
	{
		OutAnchors.Reset();
		OutError = FText::GetEmpty();
		if (Before.Placements.Num() != After.Placements.Num())
		{
			OutError = LOCTEXT("PlacementCountMismatch", "Sidewalk infill requires matching pre- and post-alignment placement counts.");
			return false;
		}

		TSet<FGuid> FrontageIds;
		for (const FDeepLevelBuildingLinePlacement& Placement : After.Placements)
		{
			if (Placement.FrontageId.IsValid()) { FrontageIds.Add(Placement.FrontageId); }
		}
		TArray<FGuid> SortedFrontages = FrontageIds.Array();
		SortedFrontages.Sort([](const FGuid& A, const FGuid& B) { return A < B; });
		for (const FGuid& FrontageId : SortedFrontages)
		{
			TArray<FRect> OldRects;
			TArray<FRect> NewRects;
			TArray<double> Xs;
			TArray<double> Ys;
			for (int32 Index = 0; Index < After.Placements.Num(); ++Index)
			{
				if (After.Placements[Index].FrontageId != FrontageId) { continue; }
				FRect OldRect;
				FRect NewRect;
				if (!MakeRect(Catalog, Before.Placements[Index], OldRect) || !MakeRect(Catalog, After.Placements[Index], NewRect))
				{
					OutError = LOCTEXT("UnsupportedFootprint", "Sidewalk infill currently requires axis-aligned building footprints.");
					return false;
				}
				OldRects.Add(OldRect);
				NewRects.Add(NewRect);
				for (const FRect* Rect : {&OldRects.Last(), &NewRects.Last()})
				{
					AddUniqueCut(Xs, Rect->MinX); AddUniqueCut(Xs, Rect->MaxX);
					AddUniqueCut(Ys, Rect->MinY); AddUniqueCut(Ys, Rect->MaxY);
				}
			}
			Xs.Sort(); Ys.Sort();
			TArray<FRect> Runs;
			for (int32 Y = 0; Y + 1 < Ys.Num(); ++Y)
			{
				FRect* Current = nullptr;
				for (int32 X = 0; X + 1 < Xs.Num(); ++X)
				{
					const FVector2D Center((Xs[X] + Xs[X + 1]) * 0.5, (Ys[Y] + Ys[Y + 1]) * 0.5);
					const FRect* OldOwner = OldRects.FindByPredicate([&Center](const FRect& Rect) { return Contains(Rect, Center); });
					const bool bStillOccupied = NewRects.ContainsByPredicate([&Center](const FRect& Rect) { return Contains(Rect, Center); });
					if (!OldOwner || bStillOccupied) { Current = nullptr; continue; }
					if (Current && FMath::IsNearlyEqual(Current->MaxX, Xs[X], Tolerance)) { Current->MaxX = Xs[X + 1]; }
					else { Current = &Runs.Emplace_GetRef(FRect{Xs[X], Xs[X + 1], Ys[Y], Ys[Y + 1], OldOwner->SurfaceZ}); }
				}
			}
			TArray<FRect> Patches;
			for (const FRect& Run : Runs)
			{
				FRect* Previous = Patches.FindByPredicate([&Run](const FRect& Rect)
				{
					return FMath::IsNearlyEqual(Rect.MinX, Run.MinX, Tolerance)
						&& FMath::IsNearlyEqual(Rect.MaxX, Run.MaxX, Tolerance)
						&& FMath::IsNearlyEqual(Rect.MaxY, Run.MinY, Tolerance)
						&& FMath::IsNearlyEqual(Rect.SurfaceZ, Run.SurfaceZ, Tolerance);
				});
				if (Previous) { Previous->MaxY = Run.MaxY; }
				else { Patches.Add(Run); }
			}

			for (const FRect& Patch : Patches)
			{
				if (Patch.MaxX - Patch.MinX <= Tolerance || Patch.MaxY - Patch.MinY <= Tolerance) { continue; }
				const FString LocalKey = FString::Printf(TEXT("%s:%.0f:%.0f:%.0f:%.0f"),
					*FrontageId.ToString(EGuidFormats::Digits), Patch.MinX, Patch.MaxX, Patch.MinY, Patch.MaxY);
				FDeepLevelCityAnchor& Anchor = OutAnchors.Emplace_GetRef();
				Anchor.StableId = FDeepLevelCityStableId::MakeAnchorId(SourceGuid, LocalKey, TEXT("SidewalkInfill"));
				Anchor.Geometry = EDeepLevelCityAnchorGeometry::Surface;
				Anchor.Transform = FTransform(FVector((Patch.MinX + Patch.MaxX) * 0.5, (Patch.MinY + Patch.MaxY) * 0.5, Patch.SurfaceZ));
				Anchor.Extent = FVector((Patch.MaxX - Patch.MinX) * 0.5, (Patch.MaxY - Patch.MinY) * 0.5, 0.0);
				Anchor.Tags.AddTag(DeepLevelCityTags::Anchor_Sidewalk_Infill);
				Anchor.SourceRevision = SourceRevision;
				AddOccupiedCells(Grid, Patch, Anchor.OccupiedCells);
			}
		}
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
