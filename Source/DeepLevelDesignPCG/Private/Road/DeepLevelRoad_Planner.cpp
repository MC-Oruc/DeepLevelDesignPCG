// Copyright <--\, Inc. All Rights Reserved.

#include "Road/DeepLevelRoadPCG.h"

// ---- DeepLevelRoadNetworkPlanner ----


#include "Containers/Queue.h"
#include "Data/PCGSplineData.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadNetworkPlanner"

namespace
{
	constexpr int32 PositiveX = static_cast<int32>(EDeepLevelRoadConnection::PositiveX);
	constexpr int32 PositiveY = static_cast<int32>(EDeepLevelRoadConnection::PositiveY);
	constexpr int32 NegativeX = static_cast<int32>(EDeepLevelRoadConnection::NegativeX);
	constexpr int32 NegativeY = static_cast<int32>(EDeepLevelRoadConnection::NegativeY);

	struct FGridDirection
	{
		int32 Mask;
		int32 OppositeMask;
		FIntPoint Offset;
	};

	const TArray<FGridDirection>& GetDirections()
	{
		static const TArray<FGridDirection> Directions = {
			{PositiveX, NegativeX, FIntPoint(1, 0)},
			{PositiveY, NegativeY, FIntPoint(0, 1)},
			{NegativeX, PositiveX, FIntPoint(-1, 0)},
			{NegativeY, PositiveY, FIntPoint(0, -1)}};
		return Directions;
	}

	int32 CountBits(const int32 Mask)
	{
		return FMath::CountBits64(static_cast<uint64>(Mask));
	}

	bool IsStraight(const int32 Mask)
	{
		return Mask == (PositiveX | NegativeX) || Mask == (PositiveY | NegativeY);
	}

	int32 RotateMask(const int32 Mask, const int32 QuarterTurns)
	{
		int32 Result = Mask;
		for (int32 Turn = 0; Turn < QuarterTurns; ++Turn)
		{
			int32 Rotated = 0;
			if (Result & PositiveX) Rotated |= PositiveY;
			if (Result & PositiveY) Rotated |= NegativeX;
			if (Result & NegativeX) Rotated |= NegativeY;
			if (Result & NegativeY) Rotated |= PositiveX;
			Result = Rotated;
		}
		return Result;
	}

	FIntPoint ToGridCell(const FVector& Location, const FVector& Origin, const double CellSize)
	{
		return FIntPoint(
			FMath::RoundToInt((Location.X - Origin.X) / CellSize),
			FMath::RoundToInt((Location.Y - Origin.Y) / CellSize));
	}

	struct FRoadGridGraph
	{
		TMap<FIntPoint, int32> Connections;

		void AddEdge(const FIntPoint& From, const FIntPoint& To)
		{
			const FIntPoint Delta = To - From;
			for (const FGridDirection& Direction : GetDirections())
			{
				if (Delta == Direction.Offset)
				{
					Connections.FindOrAdd(From) |= Direction.Mask;
					Connections.FindOrAdd(To) |= Direction.OppositeMask;
					return;
				}
			}
		}

		void AddAxisSegment(const FIntPoint& Start, const FIntPoint& End)
		{
			const FIntPoint Delta = End - Start;
			const FIntPoint Step(FMath::Sign(Delta.X), FMath::Sign(Delta.Y));
			FIntPoint Current = Start;
			while (Current != End)
			{
				const FIntPoint Next = Current + Step;
				AddEdge(Current, Next);
				Current = Next;
			}
		}
	};

	bool AddSplineToGraph(
		const UPCGSplineData& Spline,
		const FVector& GridOrigin,
		const double CellSize,
		const int32 SplineIndex,
		FRoadGridGraph& Graph,
		FText& OutError)
	{
		const TArray<FSplinePoint> Points = Spline.GetSplinePoints();
		if (Points.Num() < 2)
		{
			OutError = FText::Format(
				LOCTEXT("TooFewRoadPoints", "Road spline {0} needs at least two points."),
				FText::AsNumber(SplineIndex + 1));
			return false;
		}

		TArray<FIntPoint> Cells;
		Cells.Reserve(Points.Num());
		const FTransform SplineTransform = Spline.GetTransform();
		for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
		{
			if (Points[PointIndex].Type != ESplinePointType::Linear)
			{
				OutError = FText::Format(
					LOCTEXT("NonLinearRoadPoint", "Road spline {0}, point {1} is not Linear. Tile roads support only Linear spline points."),
					FText::AsNumber(SplineIndex + 1),
					FText::AsNumber(PointIndex + 1));
				return false;
			}
			Cells.Add(ToGridCell(
				SplineTransform.TransformPosition(Points[PointIndex].Position),
				GridOrigin,
				CellSize));
		}

		const int32 SegmentCount = Spline.IsClosed() ? Cells.Num() : Cells.Num() - 1;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
		{
			const FIntPoint Start = Cells[SegmentIndex];
			const FIntPoint End = Cells[(SegmentIndex + 1) % Cells.Num()];
			const FIntPoint Delta = End - Start;
			if ((Delta.X == 0) == (Delta.Y == 0))
			{
				OutError = FText::Format(
					LOCTEXT("InvalidRoadSegment", "Road spline {0}, segment {1} is diagonal or collapses after grid snapping. Each segment must occupy one grid axis."),
					FText::AsNumber(SplineIndex + 1),
					FText::AsNumber(SegmentIndex + 1));
				return false;
			}
			Graph.AddAxisSegment(Start, End);
		}
		return true;
	}

	struct FJunctionDistance
	{
		int32 Distance = MAX_int32;
		int32 DirectionTowardJunction = 0;
	};

	void BuildJunctionDistanceField(
		const FRoadGridGraph& Graph,
		TSet<FIntPoint>& OutJunctionCells,
		TMap<FIntPoint, FJunctionDistance>& OutDistances)
	{
		TArray<FIntPoint> Sources;
		for (const TPair<FIntPoint, int32>& Pair : Graph.Connections)
		{
			if (CountBits(Pair.Value) >= 3)
			{
				Sources.Add(Pair.Key);
			}
		}
		Sources.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			return A.X == B.X ? A.Y < B.Y : A.X < B.X;
		});

		TQueue<FIntPoint> Queue;
		for (const FIntPoint& Source : Sources)
		{
			OutJunctionCells.Add(Source);
			OutDistances.Add(Source, {0, 0});
			Queue.Enqueue(Source);
		}

		FIntPoint Current;
		while (Queue.Dequeue(Current))
		{
			const int32 CurrentMask = Graph.Connections.FindChecked(Current);
			const int32 CurrentDistance = OutDistances.FindChecked(Current).Distance;
			for (const FGridDirection& Direction : GetDirections())
			{
				if ((CurrentMask & Direction.Mask) == 0)
				{
					continue;
				}

				const FIntPoint Neighbor = Current + Direction.Offset;
				FJunctionDistance& NeighborDistance = OutDistances.FindOrAdd(Neighbor);
				const int32 ProposedDistance = CurrentDistance + 1;
				if (ProposedDistance < NeighborDistance.Distance)
				{
					NeighborDistance.Distance = ProposedDistance;
					NeighborDistance.DirectionTowardJunction = Direction.OppositeMask;
					Queue.Enqueue(Neighbor);
				}
				else if (ProposedDistance == NeighborDistance.Distance
					&& Direction.OppositeMask < NeighborDistance.DirectionTowardJunction)
				{
					NeighborDistance.DirectionTowardJunction = Direction.OppositeMask;
				}
			}
		}
	}

	TSet<FIntPoint> BuildSidewalkCells(const FRoadGridGraph& Graph, const int32 Width)
	{
		TSet<FIntPoint> SidewalkCells;
		for (const TPair<FIntPoint, int32>& Pair : Graph.Connections)
		{
			TArray<FIntPoint, TInlineAllocator<4>> Normals;
			if ((Pair.Value & (PositiveX | NegativeX)) != 0)
			{
				Normals.AddUnique(FIntPoint(0, 1));
				Normals.AddUnique(FIntPoint(0, -1));
			}
			if ((Pair.Value & (PositiveY | NegativeY)) != 0)
			{
				Normals.AddUnique(FIntPoint(1, 0));
				Normals.AddUnique(FIntPoint(-1, 0));
			}

			for (const FIntPoint& Normal : Normals)
			{
				for (int32 Offset = 1; Offset <= Width; ++Offset)
				{
					const FIntPoint Candidate = Pair.Key + Normal * Offset;
					if (!Graph.Connections.Contains(Candidate))
					{
						SidewalkCells.Add(Candidate);
					}
				}
			}

			if (CountBits(Pair.Value) == 2 && !IsStraight(Pair.Value))
			{
				const int32 ExteriorX = (Pair.Value & PositiveX) != 0 ? -1 : 1;
				const int32 ExteriorY = (Pair.Value & PositiveY) != 0 ? -1 : 1;
				for (int32 XOffset = 1; XOffset <= Width; ++XOffset)
				{
					for (int32 YOffset = 1; YOffset <= Width; ++YOffset)
					{
						const FIntPoint Candidate =
							Pair.Key + FIntPoint(ExteriorX * XOffset, ExteriorY * YOffset);
						if (!Graph.Connections.Contains(Candidate))
						{
							SidewalkCells.Add(Candidate);
						}
					}
				}
			}
		}
		return SidewalkCells;
	}

	FText DescribeRequiredTile(const int32 ConnectionMask, const int32 ApproachDirection)
	{
		if (ApproachDirection != 0)
		{
			return LOCTEXT("RequiredJunctionApproach", "Junction Approach");
		}
		switch (CountBits(ConnectionMask))
		{
		case 1: return LOCTEXT("RequiredDeadEnd", "Dead End");
		case 2: return IsStraight(ConnectionMask)
			? LOCTEXT("RequiredStraight", "Straight")
			: LOCTEXT("RequiredCorner", "Corner");
		case 3: return LOCTEXT("RequiredTJunction", "T-Junction");
		case 4: return LOCTEXT("RequiredFourWay", "Four-Way Junction");
		default: return LOCTEXT("RequiredInvalid", "a valid road tile");
		}
	}

	struct FCandidate
	{
		const FDeepLevelRoadTileDefinition* Definition = nullptr;
		int32 QuarterTurns = 0;
	};

	bool SelectCandidate(
		const UDeepLevelRoadTileCatalog& Catalog,
		const EDeepLevelRoadTileKind Kind,
		const int32 RequiredConnections,
		const int32 ApproachDirection,
		const FIntPoint& Cell,
		const int32 Seed,
		FCandidate& OutCandidate)
	{
		TArray<FCandidate> Candidates;
		double TotalWeight = 0.0;
		for (const FDeepLevelRoadTileDefinition& Definition : Catalog.Tiles)
		{
			if (Definition.GetKind() != Kind)
			{
				continue;
			}
			if (Kind == EDeepLevelRoadTileKind::Sidewalk)
			{
				Candidates.Add({&Definition, 0});
				TotalWeight += Definition.SelectionWeight;
				continue;
			}

			for (int32 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
			{
				if (RotateMask(Definition.ConnectionMask, QuarterTurns) == RequiredConnections
					&& Definition.IsJunctionApproach() == (ApproachDirection != 0)
					&& (!Definition.IsJunctionApproach()
						|| RotateMask(Definition.ApproachJunctionDirectionMask, QuarterTurns) == ApproachDirection))
				{
					Candidates.Add({&Definition, QuarterTurns});
					TotalWeight += Definition.SelectionWeight;
				}
			}
		}
		if (Candidates.IsEmpty() || TotalWeight <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const uint32 Hash = HashCombineFast(
			GetTypeHash(Seed),
			HashCombineFast(
				GetTypeHash(Cell.X),
				HashCombineFast(GetTypeHash(Cell.Y), HashCombineFast(GetTypeHash(RequiredConnections), GetTypeHash(ApproachDirection)))));
		FRandomStream Random(static_cast<int32>(Hash));
		double Selection = Random.FRand() * TotalWeight;
		for (const FCandidate& Candidate : Candidates)
		{
			Selection -= Candidate.Definition->SelectionWeight;
			if (Selection <= 0.0)
			{
				OutCandidate = Candidate;
				return true;
			}
		}
		OutCandidate = Candidates.Last();
		return true;
	}

	FTransform MakeTransform(
		const FDeepLevelRoadTileDefinition& Definition,
		const FIntPoint& Cell,
		const FVector& GridOrigin,
		const double CellSize,
		const int32 QuarterTurns)
	{
		const FRotator Rotation(0.0, QuarterTurns * 90.0, 0.0);
		const FVector TargetCenter(
			GridOrigin.X + Cell.X * CellSize,
			GridOrigin.Y + Cell.Y * CellSize,
			GridOrigin.Z + Definition.PlacementVolume.Extent.Z);
		const FTransform LocalVolumeTransform(
			Definition.PlacementVolume.Rotation,
			Definition.PlacementVolume.Center);
		return LocalVolumeTransform.Inverse() * FTransform(Rotation, TargetCenter);
	}

	FTransform MakeSurfaceTransform(
		const FDeepLevelRoadTileDefinition& Definition,
		const FTransform& MeshTransform)
	{
		const FQuat LocalRotation = Definition.PlacementVolume.Rotation.Quaternion();
		const FVector LocalSurfaceCenter = Definition.PlacementVolume.Center
			+ LocalRotation.RotateVector(FVector(0.0, 0.0, Definition.PlacementVolume.Extent.Z));
		FTransform Surface = FTransform(LocalRotation, LocalSurfaceCenter) * MeshTransform;
		Surface.SetScale3D(FVector::OneVector);
		return Surface;
	}

	bool AddRoadPlacement(
		const UDeepLevelRoadTileCatalog& Catalog,
		const FIntPoint& Cell,
		const int32 Connections,
		const int32 ApproachDirection,
		const FDeepLevelCityGrid& Grid,
		const int32 Seed,
		FDeepLevelRoadNetworkPlan& OutPlan,
		FText& OutError)
	{
		FCandidate Candidate;
		if (!SelectCandidate(
			Catalog,
			EDeepLevelRoadTileKind::Road,
			Connections,
			ApproachDirection,
			Cell,
			Seed,
			Candidate))
		{
			OutError = FText::Format(
				LOCTEXT("MissingRoadTile", "Road Network needs {0} at grid cell ({1}, {2}), but the catalog has no matching calibrated tile."),
				DescribeRequiredTile(Connections, ApproachDirection),
				FText::AsNumber(Cell.X),
				FText::AsNumber(Cell.Y));
			return false;
		}

		FDeepLevelRoadTilePlacement& Placement = OutPlan.Placements.Emplace_GetRef();
		Placement.TileMesh = Candidate.Definition->TileMesh;
		Placement.TileMaterialOverride = Candidate.Definition->TileMaterialOverride;
		Placement.Transform = MakeTransform(*Candidate.Definition, Cell, Grid.Origin, Grid.TileSize, Candidate.QuarterTurns);
		Placement.SurfaceTransform = MakeSurfaceTransform(*Candidate.Definition, Placement.Transform);
		Placement.GridCell = Cell;
		Placement.Kind = EDeepLevelRoadTileKind::Road;
		Placement.ConnectionMask = Connections;
		Placement.bJunctionApproach = ApproachDirection != 0;
		return true;
	}

	void SortCells(TArray<FIntPoint>& Cells)
	{
		Cells.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			return A.X == B.X ? A.Y < B.Y : A.X < B.X;
		});
	}

	bool ApplyCellOverrides(
		const TConstArrayView<FDeepLevelRoadCellOverride> Overrides,
		const FVector& GridOrigin,
		const double GridSize,
		FDeepLevelRoadNetworkPlan& Plan,
		FText& OutError)
	{
		TSet<FIntPoint> AuthoredCells;
		for (const FDeepLevelRoadCellOverride& Override : Overrides)
		{
			if (AuthoredCells.Contains(Override.GridCell))
			{
				OutError = FText::Format(
					LOCTEXT("DuplicateCellOverride", "Road Network has more than one Local Override for grid cell ({0}, {1})."),
					FText::AsNumber(Override.GridCell.X),
					FText::AsNumber(Override.GridCell.Y));
				return false;
			}
			AuthoredCells.Add(Override.GridCell);

			const int32 PlacementIndex = Plan.Placements.IndexOfByPredicate([&Override](const FDeepLevelRoadTilePlacement& Placement)
			{
				return Placement.GridCell == Override.GridCell;
			});

			if (Override.Mode == EDeepLevelRoadCellOverrideMode::Remove)
			{
				if (PlacementIndex == INDEX_NONE)
				{
					OutError = FText::Format(
						LOCTEXT("MissingRemovedCell", "Local Override cannot remove empty grid cell ({0}, {1})."),
						FText::AsNumber(Override.GridCell.X),
						FText::AsNumber(Override.GridCell.Y));
					return false;
				}
				Plan.Placements.RemoveAt(PlacementIndex);
				continue;
			}

			if (Override.LocalOffset.ContainsNaN()
				|| Override.RotationOffset.ContainsNaN()
				|| Override.ScaleMultiplier.ContainsNaN()
				|| FMath::IsNearlyZero(Override.ScaleMultiplier.X)
				|| FMath::IsNearlyZero(Override.ScaleMultiplier.Y)
				|| FMath::IsNearlyZero(Override.ScaleMultiplier.Z))
			{
				OutError = FText::Format(
					LOCTEXT("InvalidOverrideTransform", "Local Override at grid cell ({0}, {1}) has an invalid transform."),
					FText::AsNumber(Override.GridCell.X),
					FText::AsNumber(Override.GridCell.Y));
				return false;
			}

			FDeepLevelRoadTilePlacement* Placement = PlacementIndex == INDEX_NONE ? nullptr : &Plan.Placements[PlacementIndex];
			if (Override.Mode == EDeepLevelRoadCellOverrideMode::Modify && !Placement)
			{
				OutError = FText::Format(
					LOCTEXT("MissingModifiedCell", "Local Override cannot modify empty grid cell ({0}, {1}). Use Add instead."),
					FText::AsNumber(Override.GridCell.X),
					FText::AsNumber(Override.GridCell.Y));
				return false;
			}
			if (Override.Mode == EDeepLevelRoadCellOverrideMode::Add)
			{
				if (Placement)
				{
					OutError = FText::Format(
						LOCTEXT("OccupiedAddedCell", "Local Override cannot add to occupied grid cell ({0}, {1}). Use Modify instead."),
						FText::AsNumber(Override.GridCell.X),
						FText::AsNumber(Override.GridCell.Y));
					return false;
				}
				if (Override.ReplacementMesh.IsNull())
				{
					OutError = FText::Format(
						LOCTEXT("MissingAddedMesh", "Local Override Add at grid cell ({0}, {1}) requires a mesh."),
						FText::AsNumber(Override.GridCell.X),
						FText::AsNumber(Override.GridCell.Y));
					return false;
				}

				Placement = &Plan.Placements.Emplace_GetRef();
				Placement->GridCell = Override.GridCell;
				Placement->Kind = Override.AddedTileKind;
				Placement->Transform.SetLocation(GridOrigin + FVector(Override.GridCell.X * GridSize, Override.GridCell.Y * GridSize, 0.0));
				Placement->SurfaceTransform = Placement->Transform;
			}

			if (!Override.ReplacementMesh.IsNull())
			{
				Placement->TileMesh = Override.ReplacementMesh;
			}
			if (Override.bOverrideMaterial)
			{
				Placement->TileMaterialOverride = Override.MaterialOverride;
			}

			FTransform& Transform = Placement->Transform;
			FTransform& SurfaceTransform = Placement->SurfaceTransform;
			const FVector TranslationDelta = Transform.TransformVectorNoScale(Override.LocalOffset);
			Transform.AddToTranslation(TranslationDelta);
			SurfaceTransform.AddToTranslation(TranslationDelta);
			Transform.ConcatenateRotation(Override.RotationOffset.Quaternion());
			SurfaceTransform.ConcatenateRotation(Override.RotationOffset.Quaternion());
			Transform.SetScale3D(Transform.GetScale3D() * Override.ScaleMultiplier);
			Transform.NormalizeRotation();
		}

		Plan.RoadCellCount = 0;
		for (const FDeepLevelRoadTilePlacement& Placement : Plan.Placements)
		{
			Plan.RoadCellCount += Placement.Kind == EDeepLevelRoadTileKind::Road ? 1 : 0;
		}
		Plan.SidewalkCellCount = Plan.Placements.Num() - Plan.RoadCellCount;
		return true;
	}
}

bool FDeepLevelRoadNetworkPlanner::BuildPlan(
	const UDeepLevelRoadTileCatalog& Catalog,
	const TArray<const UPCGSplineData*>& Splines,
	const FDeepLevelCityGrid& Grid,
	const int32 Seed,
	FDeepLevelRoadNetworkPlan& OutPlan,
	FText& OutError,
	const TConstArrayView<FDeepLevelRoadCellOverride> CellOverrides)
{
	OutPlan = {};
	OutError = FText::GetEmpty();
	if (!Grid.Validate(OutError) || !Catalog.ValidateForGeneration(OutError))
	{
		return false;
	}
	const double SizeTolerance = FMath::Max(Grid.TileSize * 0.01, 1.0);
	for (int32 Index = 0; Index < Catalog.Tiles.Num(); ++Index)
	{
		const FVector Extent = Catalog.Tiles[Index].PlacementVolume.Extent;
		if (!FMath::IsNearlyEqual(Extent.X * 2.0, Grid.TileSize, SizeTolerance)
			|| !FMath::IsNearlyEqual(Extent.Y * 2.0, Grid.TileSize, SizeTolerance))
		{
			OutError = FText::Format(
				LOCTEXT("TileSizeMismatch", "Road Tile Catalog entry {0} must be calibrated to the City Layout tile size of {1}."),
				FText::AsNumber(Index + 1),
				FText::AsNumber(Grid.TileSize));
			return false;
		}
	}
	if (Splines.IsEmpty())
	{
		OutError = LOCTEXT("MissingSplines", "Road Network requires at least one road spline.");
		return false;
	}

	FRoadGridGraph Graph;
	for (int32 SplineIndex = 0; SplineIndex < Splines.Num(); ++SplineIndex)
	{
		if (!Splines[SplineIndex])
		{
			OutError = FText::Format(
				LOCTEXT("NullRoadSpline", "Road spline {0} is invalid."),
				FText::AsNumber(SplineIndex + 1));
			return false;
		}
		if (!AddSplineToGraph(
			*Splines[SplineIndex],
			Grid.Origin,
			Grid.TileSize,
			SplineIndex,
			Graph,
			OutError))
		{
			return false;
		}
	}
	if (Graph.Connections.IsEmpty())
	{
		OutError = LOCTEXT("EmptyNetwork", "Road Network splines do not cover any grid edges.");
		return false;
	}

	TSet<FIntPoint> JunctionCells;
	TMap<FIntPoint, FJunctionDistance> JunctionDistances;
	BuildJunctionDistanceField(Graph, JunctionCells, JunctionDistances);

	TArray<FIntPoint> JunctionPlacements = JunctionCells.Array();
	TArray<FIntPoint> ApproachPlacements;
	TArray<FIntPoint> OrdinaryPlacements;
	for (const TPair<FIntPoint, int32>& Pair : Graph.Connections)
	{
		if (JunctionCells.Contains(Pair.Key))
		{
			continue;
		}
		const FJunctionDistance* Distance = JunctionDistances.Find(Pair.Key);
		if (Distance && Distance->Distance == 1 && IsStraight(Pair.Value))
		{
			ApproachPlacements.Add(Pair.Key);
		}
		else
		{
			OrdinaryPlacements.Add(Pair.Key);
		}
	}
	SortCells(JunctionPlacements);
	SortCells(ApproachPlacements);
	SortCells(OrdinaryPlacements);

	for (const FIntPoint& Cell : JunctionPlacements)
	{
		if (!AddRoadPlacement(Catalog, Cell, Graph.Connections.FindChecked(Cell), 0, Grid, Seed, OutPlan, OutError))
		{
			return false;
		}
	}
	for (const FIntPoint& Cell : ApproachPlacements)
	{
		const FJunctionDistance& Distance = JunctionDistances.FindChecked(Cell);
		if (!AddRoadPlacement(
			Catalog,
			Cell,
			Graph.Connections.FindChecked(Cell),
			Distance.DirectionTowardJunction,
			Grid,
			Seed,
			OutPlan,
			OutError))
		{
			return false;
		}
	}
	for (const FIntPoint& Cell : OrdinaryPlacements)
	{
		if (!AddRoadPlacement(Catalog, Cell, Graph.Connections.FindChecked(Cell), 0, Grid, Seed, OutPlan, OutError))
		{
			return false;
		}
	}
	OutPlan.RoadCellCount = Graph.Connections.Num();

	TArray<FIntPoint> SidewalkPlacements = BuildSidewalkCells(Graph, Catalog.SidewalkWidthInTiles).Array();
	SortCells(SidewalkPlacements);
	for (const FIntPoint& Cell : SidewalkPlacements)
	{
		FCandidate Candidate;
		if (!SelectCandidate(Catalog, EDeepLevelRoadTileKind::Sidewalk, 0, 0, Cell, Seed, Candidate))
		{
			OutError = LOCTEXT("MissingSidewalkTile", "Road Network has no calibrated sidewalk tile.");
			return false;
		}

		FDeepLevelRoadTilePlacement& Placement = OutPlan.Placements.Emplace_GetRef();
		Placement.TileMesh = Candidate.Definition->TileMesh;
		Placement.TileMaterialOverride = Candidate.Definition->TileMaterialOverride;
		Placement.Transform = MakeTransform(*Candidate.Definition, Cell, Grid.Origin, Grid.TileSize, Candidate.QuarterTurns);
		Placement.SurfaceTransform = MakeSurfaceTransform(*Candidate.Definition, Placement.Transform);
		Placement.GridCell = Cell;
		Placement.Kind = EDeepLevelRoadTileKind::Sidewalk;
	}
	OutPlan.SidewalkCellCount = SidewalkPlacements.Num();
	return ApplyCellOverrides(CellOverrides, Grid.Origin, Grid.TileSize, OutPlan, OutError);
}

bool FDeepLevelRoadNetworkPlanner::AppendSidewalkInfills(
	const UDeepLevelRoadTileCatalog& Catalog,
	const FDeepLevelCityGrid& Grid,
	const TConstArrayView<FDeepLevelCityAnchor> Anchors,
	const int32 Seed,
	FDeepLevelRoadNetworkPlan& InOutPlan,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	constexpr int32 SafetyWidthInTiles = 1;
	TSet<FIntPoint> OccupiedCells;
	for (const FDeepLevelRoadTilePlacement& Placement : InOutPlan.Placements)
	{
		OccupiedCells.Add(Placement.GridCell);
	}
	TMap<FIntPoint, FGuid> RequestedCells;
	for (const FDeepLevelCityAnchor& Anchor : Anchors)
	{
		if (Anchor.Geometry != EDeepLevelCityAnchorGeometry::Surface
			|| !Anchor.Tags.HasTagExact(DeepLevelCityTags::Anchor_Sidewalk_Infill))
		{
			continue;
		}
		if (!Anchor.Transform.IsValid() || Anchor.Extent.ContainsNaN()
			|| Anchor.Extent.X <= UE_DOUBLE_SMALL_NUMBER || Anchor.Extent.Y <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutError = LOCTEXT("InvalidSidewalkInfill", "City Layout contains an invalid sidewalk infill surface.");
			return false;
		}

		const FVector Center = Anchor.Transform.GetLocation();
		const double SafetyDepth = Grid.TileSize * SafetyWidthInTiles;
		const FVector2D Min(Center.X - Anchor.Extent.X - SafetyDepth, Center.Y - Anchor.Extent.Y - SafetyDepth);
		const FVector2D Max(Center.X + Anchor.Extent.X + SafetyDepth, Center.Y + Anchor.Extent.Y + SafetyDepth);
		const int32 MinX = FMath::FloorToInt((Min.X - Grid.Origin.X) / Grid.TileSize) - 1;
		const int32 MaxX = FMath::CeilToInt((Max.X - Grid.Origin.X) / Grid.TileSize) + 1;
		const int32 MinY = FMath::FloorToInt((Min.Y - Grid.Origin.Y) / Grid.TileSize) - 1;
		const int32 MaxY = FMath::CeilToInt((Max.Y - Grid.Origin.Y) / Grid.TileSize) + 1;
		const double HalfTile = Grid.TileSize * 0.5;
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				const FIntPoint Cell(X, Y);
				const FVector CellCenter = Grid.CellToWorld(Cell);
				const bool bOverlaps = CellCenter.X + HalfTile > Min.X + UE_DOUBLE_KINDA_SMALL_NUMBER
					&& CellCenter.X - HalfTile < Max.X - UE_DOUBLE_KINDA_SMALL_NUMBER
					&& CellCenter.Y + HalfTile > Min.Y + UE_DOUBLE_KINDA_SMALL_NUMBER
					&& CellCenter.Y - HalfTile < Max.Y - UE_DOUBLE_KINDA_SMALL_NUMBER;
				if (bOverlaps && !OccupiedCells.Contains(Cell))
				{
					RequestedCells.FindOrAdd(Cell, Anchor.StableId);
				}
			}
		}
	}

	TArray<FIntPoint> SortedCells;
	RequestedCells.GetKeys(SortedCells);
	SortCells(SortedCells);
	for (const FIntPoint& Cell : SortedCells)
	{
		FCandidate Candidate;
		if (!SelectCandidate(Catalog, EDeepLevelRoadTileKind::Sidewalk, 0, 0, Cell, Seed, Candidate))
		{
			OutError = LOCTEXT("MissingSidewalkInfillTile", "Road Network has no calibrated sidewalk tile for building infills.");
			return false;
		}
		FDeepLevelRoadTilePlacement& Placement = InOutPlan.Placements.Emplace_GetRef();
		Placement.StableId = FDeepLevelCityStableId::MakeAnchorId(
			RequestedCells.FindChecked(Cell),
			FString::Printf(TEXT("Cell:%d:%d"), Cell.X, Cell.Y),
			TEXT("SidewalkInfillTile"));
		Placement.TileMesh = Candidate.Definition->TileMesh;
		Placement.TileMaterialOverride = Candidate.Definition->TileMaterialOverride;
		Placement.Transform = MakeTransform(*Candidate.Definition, Cell, Grid.Origin, Grid.TileSize, Candidate.QuarterTurns);
		Placement.SurfaceTransform = MakeSurfaceTransform(*Candidate.Definition, Placement.Transform);
		Placement.GridCell = Cell;
		Placement.Kind = EDeepLevelRoadTileKind::Sidewalk;
		++InOutPlan.SidewalkInfillCount;
		++InOutPlan.SidewalkCellCount;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
