// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingLayout.h"
#include "Data/PCGSplineData.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingRoadside"

namespace DeepLevelBuildingRoadside
{
	struct FEdge
	{
		FGuid Id;
		FGuid Source;
		FVector Start;
		FVector End;
		int32 Depth = 0;
	};

	using namespace DeepLevelBuildingLayoutGeometry;

	FIntVector EndpointKey(const FVector& Point, const FDeepLevelCityGrid& Grid)
	{
		return FIntVector(FMath::RoundToInt((Point.X - Grid.Origin.X) * 2.0 / Grid.TileSize),
			FMath::RoundToInt((Point.Y - Grid.Origin.Y) * 2.0 / Grid.TileSize), FMath::RoundToInt(Point.Z * 100.0));
	}

	bool BuildPlan(const FDeepLevelCityLayoutSnapshot& Base, const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FGuid& BuildingSource, const int32 Seed, const double Variety, const double CornerPreference,
		const EDeepLevelCornerPlacementFlags Corners, const TConstArrayView<FBox2D> ExclusionBounds, FDeepLevelBuildingLinePlan& OutPlan,
		int32& OutRejectedCount, FText& OutError)
	{
		OutPlan = {};
		OutRejectedCount = 0;
		const FDeepLevelCityGrid& Grid = Base.GetGrid();
		TArray<FEdge> Edges;
		bool bFoundRoadEdge = false;
		for (const FDeepLevelCityAnchor& Anchor : Base.GetAnchors())
		{
			if (!Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Edge))
			{
				continue;
			}
			bFoundRoadEdge = true;
			const FVector Tangent = Anchor.Transform.GetUnitAxis(EAxis::X);
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Tangent);
			const FIntPoint Step(FMath::RoundToInt(Right.X), FMath::RoundToInt(Right.Y));
			if (Anchor.Geometry != EDeepLevelCityAnchorGeometry::Segment || Anchor.OccupiedCells.Num() != 1
				|| !Anchor.SourceGuid.IsValid() || FMath::Abs(Step.X) + FMath::Abs(Step.Y) != 1
				|| !Right.Equals(FVector(Step.X, Step.Y, 0), 0.01))
			{
				OutError = LOCTEXT("InvalidEdge", "Roadside Building requires oriented, grid-aligned sidewalk edges with a source and one sidewalk cell.");
				return false;
			}
			FIntPoint Cell = Anchor.OccupiedCells[0];
			const FDeepLevelCityCellState* Road = Base.FindCell(Cell - Step);
			if (!Road || !(Road->OccupancyMask & static_cast<int32>(EDeepLevelCityOccupancy::Road)))
			{
				OutError = LOCTEXT("MissingAdjacentRoad", "A sidewalk edge must face an adjacent Road cell.");
				return false;
			}
			int32 Depth = 0;
			while (const FDeepLevelCityCellState* Sidewalk = Base.FindCell(Cell))
			{
				if (!(Sidewalk->OccupancyMask & static_cast<int32>(EDeepLevelCityOccupancy::Sidewalk))) { break; }
				++Depth;
				Cell += Step;
			}
			if (Depth == 0)
			{
				OutError = LOCTEXT("MissingSidewalk", "A sidewalk edge must reference a sidewalk cell.");
				return false;
			}
			if (Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Road_Junction))
			{
				continue;
			}
			FVector Center = Grid.CellToWorld(Cell - Step) + Right * (Grid.TileSize * 0.5);
			Center.Z = Anchor.Transform.GetLocation().Z;
			Edges.Add({Anchor.StableId, Anchor.SourceGuid, Center - Tangent * (Grid.TileSize * 0.5),
				Center + Tangent * (Grid.TileSize * 0.5), Depth});
		}
		if (!bFoundRoadEdge)
		{
			OutError = LOCTEXT("MissingEdges", "Roadside Building requires Road sidewalk-edge data in the City Layout base snapshot.");
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
			for (int32 Candidate : Candidates)
			{
				if (Edges[Candidate].Source != Edge.Source || Edges[Candidate].Depth != Edge.Depth) { continue; }
				if (FVector::DotProduct((Edge.End - Edge.Start).GetSafeNormal(),
					(Edges[Candidate].End - Edges[Candidate].Start).GetSafeNormal()) < 0.99) { continue; }
				if (Result != INDEX_NONE) { return INDEX_NONE; }
				Result = Candidate;
			}
			return Result;
		};
		TArray<int32> Next, Previous;
		Next.Init(INDEX_NONE, Edges.Num()); Previous.Init(INDEX_NONE, Edges.Num());
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			const int32 Neighbor = FindNeighbor(Starts, Edges[Index].End, Edges[Index]);
			if (Neighbor != INDEX_NONE && FindNeighbor(Ends, Edges[Neighbor].Start, Edges[Neighbor]) == Index)
			{
				Next[Index] = Neighbor; Previous[Neighbor] = Index;
			}
		}
		TArray<FFootprint> AcceptedFootprints;
		TMultiMap<FIntPoint, int32> FootprintsByCell;
		TBitArray<> Visited(false, Edges.Num());
		const auto OverlapsExclusion = [&ExclusionBounds](const FFootprint& Footprint)
		{
			for (const FBox2D& Bounds : ExclusionBounds)
			{
				const FFootprint Box = {Bounds.Min, FVector2D(Bounds.Max.X, Bounds.Min.Y), Bounds.Max, FVector2D(Bounds.Min.X, Bounds.Max.Y)};
				if (FootprintsOverlap(Footprint, Box)) { return true; }
			}
			return false;
		};
		const auto IsClear = [&Base, &Grid, &AcceptedFootprints, &FootprintsByCell, &OverlapsExclusion](
			const FDeepLevelBuildingPlacementDefinition& Definition,
			const FDeepLevelBuildingLinePlacement& Placement,
			FFootprint& OutFootprint,
			TArray<FIntPoint>& OutCells)
		{
			const FTransform Transform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(Definition,
				Placement.StreetFace, Placement.PathSample.Location, Placement.PathSample.Forward, Placement.PathSample.Right);
			MakeFootprintCorners(Definition.PlacementVolume, Transform, OutFootprint);
			RasterizeFootprint(Grid, OutFootprint, OutCells);
			if (OverlapsExclusion(OutFootprint)) { return false; }
			TSet<int32> Neighbors;
			for (const FIntPoint& Cell : OutCells)
			{
				const FDeepLevelCityCellState* State = Base.FindCell(Cell);
				if (!Grid.ContainsCell(Cell) || (State && State->OccupancyMask != 0)) { return false; }
				TArray<int32> LocalNeighbors;
				FootprintsByCell.MultiFind(Cell, LocalNeighbors);
				for (const int32 Neighbor : LocalNeighbors) { Neighbors.Add(Neighbor); }
			}
			for (const int32 Neighbor : Neighbors)
			{
				if (FootprintsOverlap(OutFootprint, AcceptedFootprints[Neighbor])) { return false; }
			}
			return true;
		};
		// Open paths first; remaining paths are closed loops with a stable starting edge.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			for (int32 Start = 0; Start < Edges.Num(); ++Start)
			{
				if (Visited[Start] || (Pass == 0 && Previous[Start] != INDEX_NONE)) { continue; }
				TArray<FSplinePoint> Points;
				int32 Current = Start;
				FGuid Frontage = Edges[Start].Id;
				while (Current != INDEX_NONE && !Visited[Current])
				{
					Visited[Current] = true;
					Frontage = Edges[Current].Id < Frontage ? Edges[Current].Id : Frontage;
					Points.Emplace(Points.Num(), Edges[Current].Start, ESplinePointType::Linear);
					const int32 Following = Next[Current];
					if (Following == INDEX_NONE) { Points.Emplace(Points.Num(), Edges[Current].End, ESplinePointType::Linear); }
					Current = Following;
				}
				const bool bClosed = Current == Start;
				TStrongObjectPtr<UPCGSplineData> Spline(NewObject<UPCGSplineData>());
				Spline->Initialize(Points, bClosed, FTransform::Identity);
				FDeepLevelBuildingLinePath Path;
				FDeepLevelBuildingLinePlan Candidates;
				const int32 PathSeed = HashCombineFast(Seed, HashCombineFast(GetTypeHash(BuildingSource), GetTypeHash(Frontage)));
				if (!FDeepLevelBuildingLinePath::Build(*Spline, Path, OutError)
					|| !FDeepLevelBuildingLinePlanner::BuildPlan(Catalog, Path, PathSeed, Variety, CornerPreference, Corners, Candidates, OutError, true))
				{
					return false;
				}
				for (FDeepLevelBuildingLinePlacement& Placement : Candidates.Placements)
				{
					const FDeepLevelBuildingPlacementDefinition* Definition = Catalog.Buildings.FindByPredicate(
						[&Placement](const auto& Entry) { return Entry.BuildingClass == Placement.BuildingClass; });
					check(Definition);
					FFootprint Footprint;
					TArray<FIntPoint> Cells;
					if (!IsClear(*Definition, Placement, Footprint, Cells))
					{
						struct FAlternative
						{
							const FDeepLevelBuildingPlacementDefinition* Definition = nullptr;
							EDeepLevelBuildingVolumeFace Face = EDeepLevelBuildingVolumeFace::NegativeY;
							double Width = 0.0;
							uint32 TieBreaker = 0;
						};
						const double SlotWidth = Placement.CoverageEnd - Placement.CoverageStart;
						TArray<FAlternative> Alternatives;
						for (int32 DefinitionIndex = 0; DefinitionIndex < Catalog.Buildings.Num(); ++DefinitionIndex)
						{
							const FDeepLevelBuildingPlacementDefinition& CandidateDefinition = Catalog.Buildings[DefinitionIndex];
							if (CandidateDefinition.SelectionWeight <= 0.0) { continue; }
							TArray<EDeepLevelBuildingVolumeFace> Faces;
							DeepLevelBuildingLinePacking::FCatalogModel::GetEligibleFaces(CandidateDefinition, Faces);
							for (const EDeepLevelBuildingVolumeFace Face : Faces)
							{
								if (!DeepLevelBuildingLinePacking::FCatalogModel::IsSpanFaceValid(CandidateDefinition, Face)) { continue; }
								const FDeepLevelResolvedBuildingGeometry Geometry =
									FDeepLevelBuildingPlacementGeometry::ResolveGeometry(CandidateDefinition, Face);
								const double Width = Geometry.HalfWidth * 2.0;
								if (Width > SlotWidth + UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }
								Alternatives.Add({&CandidateDefinition, Face, Width, HashCombineFast(
									GetTypeHash(PathSeed + FMath::RoundToInt(Placement.Distance)),
									HashCombineFast(GetTypeHash(DefinitionIndex), GetTypeHash(static_cast<uint8>(Face))))});
							}
						}
						Alternatives.Sort([](const FAlternative& A, const FAlternative& B)
						{
							if (!FMath::IsNearlyEqual(A.Width, B.Width)) { return A.Width > B.Width; }
							return A.TieBreaker < B.TieBreaker;
						});
						bool bFoundAlternative = false;
						for (const FAlternative& Alternative : Alternatives)
						{
							FDeepLevelBuildingLinePlacement Candidate = Placement;
							Candidate.BuildingClass = Alternative.Definition->BuildingClass;
							Candidate.StreetFace = Alternative.Face;
							Candidate.CoverageStart = Candidate.Distance - Alternative.Width * 0.5;
							Candidate.CoverageEnd = Candidate.Distance + Alternative.Width * 0.5;
							FFootprint CandidateFootprint;
							TArray<FIntPoint> CandidateCells;
							if (!IsClear(*Alternative.Definition, Candidate, CandidateFootprint, CandidateCells)) { continue; }
							Placement = MoveTemp(Candidate);
							Footprint = MoveTemp(CandidateFootprint);
							Cells = MoveTemp(CandidateCells);
							bFoundAlternative = true;
							break;
						}
						if (!bFoundAlternative) { ++OutRejectedCount; continue; }
					}
					const int32 AcceptedIndex = AcceptedFootprints.Add(Footprint);
					for (const FIntPoint& Cell : Cells) { FootprintsByCell.Add(Cell, AcceptedIndex); }
					Placement.FrontageId = Frontage;
					OutPlan.Placements.Add(MoveTemp(Placement));
				}
			}
		}
		OutError = FText::GetEmpty();
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
