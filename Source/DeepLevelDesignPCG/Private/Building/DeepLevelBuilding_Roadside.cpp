// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingLayout.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingRoadside"

namespace DeepLevelBuildingRoadside
{
	struct FEdge
	{
		FGuid Id;
		FGuid Source;
		FVector Start;
		FVector End;
	};

	FIntVector EndpointKey(const FVector& Point, const FDeepLevelCityGrid& Grid)
	{
		return FIntVector(FMath::RoundToInt((Point.X - Grid.Origin.X) * 2.0 / Grid.TileSize),
			FMath::RoundToInt((Point.Y - Grid.Origin.Y) * 2.0 / Grid.TileSize), FMath::RoundToInt(Point.Z * 100.0));
	}

	void SimplifyPoints(TArray<FVector>& Points, const bool bClosed)
	{
		if (Points.Num() <= 2) { return; }
		TArray<FVector> Simplified;
		Simplified.Reserve(Points.Num());
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (!bClosed && (Index == 0 || Index == Points.Num() - 1))
			{
				Simplified.Add(Points[Index]);
				continue;
			}
			const FVector& Previous = Points[(Index - 1 + Points.Num()) % Points.Num()];
			const FVector& Current = Points[Index];
			const FVector& Next = Points[(Index + 1) % Points.Num()];
			const FVector Incoming = (Current - Previous).GetSafeNormal2D();
			const FVector Outgoing = (Next - Current).GetSafeNormal2D();
			if (!Incoming.Equals(Outgoing, 0.001)) { Simplified.Add(Current); }
		}
		Points = MoveTemp(Simplified);
	}

	FVector LeftNormal(const FVector& Start, const FVector& End)
	{
		const FVector Direction = (End - Start).GetSafeNormal2D();
		return FVector(-Direction.Y, Direction.X, 0.0);
	}

	FVector IntersectOffsetSegments(
		const FVector& Previous, const FVector& Vertex, const FVector& Next, const double Setback)
	{
		const FVector PreviousDirection = (Vertex - Previous).GetSafeNormal2D();
		const FVector NextDirection = (Next - Vertex).GetSafeNormal2D();
		const FVector PreviousOrigin = Vertex + LeftNormal(Previous, Vertex) * Setback;
		const FVector NextOrigin = Vertex + LeftNormal(Vertex, Next) * Setback;
		const double Cross = PreviousDirection.X * NextDirection.Y - PreviousDirection.Y * NextDirection.X;
		if (FMath::IsNearlyZero(Cross))
		{
			return Vertex + LeftNormal(Previous, Vertex) * Setback;
		}
		const FVector Delta = NextOrigin - PreviousOrigin;
		const double Distance = (Delta.X * NextDirection.Y - Delta.Y * NextDirection.X) / Cross;
		FVector Result = PreviousOrigin + PreviousDirection * Distance;
		Result.Z = Vertex.Z;
		return Result;
	}

	void ApplySetback(TArray<FVector>& Points, const bool bClosed, const double Setback)
	{
		if (FMath::IsNearlyZero(Setback) || Points.Num() < 2) { return; }
		const TArray<FVector> Source = Points;
		if (!bClosed)
		{
			Points[0] += LeftNormal(Source[0], Source[1]) * Setback;
			Points.Last() += LeftNormal(Source[Source.Num() - 2], Source.Last()) * Setback;
		}
		const int32 FirstInterior = bClosed ? 0 : 1;
		const int32 LastInterior = bClosed ? Source.Num() : Source.Num() - 1;
		for (int32 Index = FirstInterior; Index < LastInterior; ++Index)
		{
			const int32 Previous = (Index - 1 + Source.Num()) % Source.Num();
			const int32 Next = (Index + 1) % Source.Num();
			Points[Index] = IntersectOffsetSegments(Source[Previous], Source[Index], Source[Next], Setback);
		}
	}

	bool HasRoadBehind(const FDeepLevelCityLayoutSnapshot& Base, const FIntPoint& Cell, const FIntPoint& Direction)
	{
		const int32 RoadMask = static_cast<int32>(EDeepLevelCityOccupancy::Road);
		const int32 SidewalkMask = static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk);

		FIntPoint ProbeCell = Cell - Direction;
		const FDeepLevelCityCellState* Probe = Base.FindCell(ProbeCell);
		while (Probe && (Probe->OccupancyMask & SidewalkMask))
		{
			ProbeCell -= Direction;
			Probe = Base.FindCell(ProbeCell);
		}
		if (Probe && (Probe->OccupancyMask & RoadMask))
		{
			return true;
		}

		constexpr int32 MaxCornerSearchDepth = 4;
		static const FIntPoint CardinalDirections[] = {
			FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)};

		for (int32 DeltaX = -MaxCornerSearchDepth; DeltaX <= MaxCornerSearchDepth; ++DeltaX)
		{
			for (int32 DeltaY = -MaxCornerSearchDepth; DeltaY <= MaxCornerSearchDepth; ++DeltaY)
			{
				if (FMath::Abs(DeltaX) + FMath::Abs(DeltaY) > MaxCornerSearchDepth) { continue; }
				const FIntPoint RoadCandidate = Cell + FIntPoint(DeltaX, DeltaY);
				const FDeepLevelCityCellState* RoadState = Base.FindCell(RoadCandidate);
				if (!RoadState || !(RoadState->OccupancyMask & RoadMask)) { continue; }

				bool bHasPosX = false, bHasNegX = false, bHasPosY = false, bHasNegY = false;
				for (const FIntPoint& NbrDir : CardinalDirections)
				{
					const FDeepLevelCityCellState* Nbr = Base.FindCell(RoadCandidate + NbrDir);
					if (Nbr && (Nbr->OccupancyMask & RoadMask))
					{
						if (NbrDir.X > 0) { bHasPosX = true; }
						else if (NbrDir.X < 0) { bHasNegX = true; }
						else if (NbrDir.Y > 0) { bHasPosY = true; }
						else if (NbrDir.Y < 0) { bHasNegY = true; }
					}
				}
				const bool bIsCorner = (bHasPosX != bHasNegX) && (bHasPosY != bHasNegY)
					&& (bHasPosX || bHasNegX) && (bHasPosY || bHasNegY);
				if (!bIsCorner) { continue; }

				const int32 ExtX = bHasPosX ? -1 : 1;
				const int32 ExtY = bHasPosY ? -1 : 1;
				if ((Direction == FIntPoint(ExtX, 0) || Direction == FIntPoint(0, ExtY))
					&& ((Cell.X - RoadCandidate.X) * ExtX >= 0) && ((Cell.Y - RoadCandidate.Y) * ExtY >= 0))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool BuildFrontageSplines(const FDeepLevelCityLayoutSnapshot& Base,
		TArray<FFrontageSpline>& OutSplines, FText& OutError, const double FrontageSetback)
	{
		OutSplines.Reset();
		const FDeepLevelCityGrid& Grid = Base.GetGrid();
		TArray<FEdge> Edges;
		TMap<FIntPoint, const FDeepLevelCityAnchor*> SidewalkSurfaces;
		for (const FDeepLevelCityAnchor& Anchor : Base.GetAnchors())
		{
			if (Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Surface)
				&& Anchor.OccupiedCells.Num() == 1 && Anchor.SourceGuid.IsValid())
			{
				SidewalkSurfaces.Add(Anchor.OccupiedCells[0], &Anchor);
			}
		}
		static const FIntPoint Directions[] = {
			FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1)};
		const int32 RoadMask = static_cast<int32>(EDeepLevelCityOccupancy::Road);
		const int32 SidewalkMask = static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk);
		for (const TPair<FIntPoint, FDeepLevelCityCellState>& Pair : Base.GetCells())
		{
			if (!(Pair.Value.OccupancyMask & SidewalkMask)) { continue; }
			const FDeepLevelCityAnchor* const* Surface = SidewalkSurfaces.Find(Pair.Key);
			if (!Surface)
			{
				OutError = LOCTEXT("MissingSidewalkSurface", "Every Road sidewalk cell must publish a matching sidewalk surface anchor.");
				return false;
			}
			for (const FIntPoint& Direction : Directions)
			{
				const FIntPoint OutsideCell = Pair.Key + Direction;
				const FDeepLevelCityCellState* Outside = Base.FindCell(OutsideCell);
				if (Outside && (Outside->OccupancyMask & (RoadMask | SidewalkMask))) { continue; }
				if (!HasRoadBehind(Base, Pair.Key, Direction)) { continue; }

				const FVector Right(Direction.X, Direction.Y, 0.0);
				const FVector Tangent(Direction.Y, -Direction.X, 0.0);
				FVector Center = Grid.CellToWorld(Pair.Key) + Right * (Grid.TileSize * 0.5);
				Center.Z = (*Surface)->Transform.GetLocation().Z;
				const FString LocalKey = FString::Printf(TEXT("Cell:%d:%d:Frontage:%d:%d"),
					Pair.Key.X, Pair.Key.Y, Direction.X, Direction.Y);
				const FGuid EdgeId = FDeepLevelCityStableId::MakeAnchorId(
					(*Surface)->SourceGuid, LocalKey, TEXT("BuildingFrontage"));
				Edges.Add({EdgeId, (*Surface)->SourceGuid, Center - Tangent * (Grid.TileSize * 0.5),
					Center + Tangent * (Grid.TileSize * 0.5)});
			}
		}
		if (Edges.IsEmpty())
		{
			OutError = LOCTEXT("MissingFrontage", "Road layout has no mathematically valid building frontage edges.");
			return false;
		}
		Edges.Sort([](const FEdge& A, const FEdge& B) { return A.Id < B.Id; });
		TMultiMap<FIntVector, int32> Starts, Ends;
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			Starts.Add(EndpointKey(Edges[Index].Start, Grid), Index);
			Ends.Add(EndpointKey(Edges[Index].End, Grid), Index);
		}
		const auto FindNeighbor = [&Edges, &Grid](const TMultiMap<FIntVector, int32>& Map, const FVector& Point, const FEdge& Edge) -> int32
		{
			TArray<int32> Candidates;
			Map.MultiFind(EndpointKey(Point, Grid), Candidates);
			int32 Result = INDEX_NONE;
			for (const int32 Candidate : Candidates)
			{
				if (Edges[Candidate].Source != Edge.Source) { continue; }
				if (Result != INDEX_NONE) { return INDEX_NONE; }
				Result = Candidate;
			}
			return Result;
		};
		TArray<int32> Next, Previous;
		Next.Init(INDEX_NONE, Edges.Num());
		Previous.Init(INDEX_NONE, Edges.Num());
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			const int32 Neighbor = FindNeighbor(Starts, Edges[Index].End, Edges[Index]);
			if (Neighbor != INDEX_NONE && FindNeighbor(Ends, Edges[Neighbor].Start, Edges[Neighbor]) == Index)
			{
				Next[Index] = Neighbor;
				Previous[Neighbor] = Index;
			}
		}
		TBitArray<> Visited(false, Edges.Num());
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 Start = 0; Start < Edges.Num(); ++Start)
			{
				if (Visited[Start] || (Pass == 0 && Previous[Start] != INDEX_NONE)) { continue; }
				FFrontageSpline Spline;
				int32 Current = Start;
				Spline.FrontageId = Edges[Start].Id;
				while (Current != INDEX_NONE && !Visited[Current])
				{
					Visited[Current] = true;
					Spline.FrontageId = Edges[Current].Id < Spline.FrontageId ? Edges[Current].Id : Spline.FrontageId;
					Spline.Points.Add(Edges[Current].Start);
					const int32 Following = Next[Current];
					if (Following == INDEX_NONE) { Spline.Points.Add(Edges[Current].End); }
					Current = Following;
				}
				Spline.bClosed = Current == Start;
				SimplifyPoints(Spline.Points, Spline.bClosed);
				ApplySetback(Spline.Points, Spline.bClosed, FrontageSetback);
				const int32 MinimumPointCount = Spline.bClosed ? 3 : 2;
				if (Spline.Points.Num() >= MinimumPointCount)
				{
					OutSplines.Add(MoveTemp(Spline));
				}
			}
		}
		if (OutSplines.IsEmpty())
		{
			OutError = LOCTEXT("MissingValidFrontage", "Road layout has no valid open or closed building frontage.");
			return false;
		}
		OutError = FText::GetEmpty();
		return true;
	}

}

#undef LOCTEXT_NAMESPACE
