// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingPCG.h"

// ---- DeepLevelBuildingLinePlanner ----



bool FDeepLevelBuildingLinePlanner::BuildPlan(
	const UDeepLevelBuildingPlacementCatalog& Catalog,
	const FDeepLevelBuildingLinePath& Path,
	const int32 Seed,
	const double VarietyStrength,
	const double CornerPreference,
	const EDeepLevelCornerPlacementFlags CornerPlacement,
	FDeepLevelBuildingLinePlan& OutPlan,
	FText& OutError,
	const bool bAllowEmpty,
	const bool bSkipUnplaceableCorners,
	const EDeepLevelBuildingClearancePolicy ClearancePolicy,
	const FDeepLevelBuildingPlacementCandidateResolver* CandidateResolver)
{
	return FDeepLevelBuildingLinePackingSolver::Solve(
		Catalog,
		Path,
		Seed,
		VarietyStrength,
		CornerPreference,
		CornerPlacement,
		OutPlan,
		OutError,
		bAllowEmpty,
		bSkipUnplaceableCorners,
		ClearancePolicy,
		CandidateResolver);
}

// ---- DeepLevelBuildingLinePackingSolver ----



#define LOCTEXT_NAMESPACE "DeepLevelBuildingLinePackingSolver"

namespace DeepLevelBuildingLinePacking
{
	constexpr double CornerStartDegrees = 15.0;

	enum class ECornerRejectionReason : uint8
	{
		None,
		NoEligibleFace,
		OutsideBlock,
		InsufficientArea,
		AllCandidatesRepeat
	};
	struct FCornerReservation
	{
		double CornerDistance = 0.0;
		FResolvedElement Element;
	};

	bool BuildCornerSample(
		const FDeepLevelBuildingLinePath& Path,
		const double CornerDistance,
		FDeepLevelBuildingLinePathSample& OutSample,
		FVector2D& OutIncoming,
		FVector2D& OutOutgoing,
		FVector2D& OutBlockInward)
	{
		FDeepLevelBuildingLinePathSample CenterSample;
		FDeepLevelBuildingLinePathSample BeforeSample;
		FDeepLevelBuildingLinePathSample AfterSample;
		if (!Path.Sample(CornerDistance, CenterSample)
			|| !Path.Sample(CornerDistance - 1.0, BeforeSample)
			|| !Path.Sample(CornerDistance + 1.0, AfterSample))
		{
			return false;
		}

		OutIncoming = FVector2D(BeforeSample.Forward).GetSafeNormal();
		OutOutgoing = FVector2D(AfterSample.Forward).GetSafeNormal();
		if (OutIncoming.IsNearlyZero() || OutOutgoing.IsNearlyZero())
		{
			return false;
		}
		OutSample.Location = CenterSample.Location;
		OutSample.Forward = FVector(OutIncoming.X, OutIncoming.Y, 0.0);
		OutSample.Right = FVector(-OutIncoming.Y, OutIncoming.X, 0.0);

		FVector2D Bisector = (OutIncoming + OutOutgoing).GetSafeNormal();
		if (Bisector.IsNearlyZero())
		{
			Bisector = FVector2D(OutSample.Right);
		}
		const FVector2D SplineRight(OutSample.Right);
		if (FVector2D::DotProduct(Bisector, SplineRight) < 0.0)
		{
			Bisector = -Bisector;
		}
		OutBlockInward = Bisector;
		return true;
	}

	bool IsOuterCornerOnSplineRight(const FVector2D& Incoming, const FVector2D& Outgoing)
	{
		return FClearance::Cross2D(Incoming, Outgoing) < 0.0;
	}

	bool BuildCornerReservation(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const FDeepLevelBuildingLinePath& Path,
		const double CornerDistance,
		const double LeftLimit,
		const double RightLimit,
		const int32 Seed,
		const int32 CornerIndex,
		const double VarietyStrength,
		const double CornerPreference,
		const TSet<int32>& RecentCornerBuildings,
		const TArray<FClearanceShape>& ExistingCornerShapes,
		const EDeepLevelBuildingClearancePolicy ClearancePolicy,
		const FDeepLevelBuildingPlacementCandidateResolver* CandidateResolver,
		FCornerReservation& OutReservation,
		ECornerRejectionReason& OutRejectionReason)
	{
		struct FCandidate
		{
			FResolvedElement Element;
			double Score = 0.0;
			uint32 TieBreaker = 0;
		};

		FDeepLevelBuildingLinePathSample CornerSample;
		FVector2D Incoming;
		FVector2D Outgoing;
		FVector2D BlockInward;
		if (!BuildCornerSample(Path, CornerDistance, CornerSample, Incoming, Outgoing, BlockInward))
		{
			return false;
		}

		TArray<FCandidate> Candidates;
		int32 RejectedByExposure = 0;
		int32 RejectedByBlock = 0;
		int32 RejectedByArea = 0;
		for (int32 BuildingIndex = 0; BuildingIndex < Catalog.Buildings.Num(); ++BuildingIndex)
		{
			const FDeepLevelBuildingPlacementDefinition& Definition = Catalog.Buildings[BuildingIndex];
			if (Definition.SelectionWeight <= 0.0)
			{
				continue;
			}
			TArray<EDeepLevelBuildingVolumeFace> Faces;
			FCatalogModel::GetEligibleFaces(Definition, Faces);
			for (const EDeepLevelBuildingVolumeFace Face : Faces)
			{
				const bool bForwardEnd = FClearance::Cross2D(Incoming, Outgoing) >= 0.0;
				if (!FCatalogModel::IsCornerFaceValid(Definition, Face, bForwardEnd))
				{
					++RejectedByExposure;
					continue;
				}
				const FElementVariant Variant = FCatalogModel::MakeElementVariant(Definition, BuildingIndex, Face);
				FDeepLevelBuildingLinePathSample AnchoredSample = CornerSample;
				const double AnchorDirection = bForwardEnd ? -1.0 : 1.0;
				AnchoredSample.Location += AnchoredSample.Forward * Variant.HalfWidth * AnchorDirection;
				if (CandidateResolver)
				{
					bool bResolved = false;
					const FVector TangentInward = CornerSample.Forward * AnchorDirection;
					for (const double TangentShift : {0.0, 10.0, 25.0, 50.0, 100.0})
					{
						FDeepLevelBuildingLinePathSample TestSample = AnchoredSample;
						TestSample.Location += TangentInward * TangentShift;
						if ((*CandidateResolver)(Definition, Face, TestSample))
						{
							AnchoredSample = TestSample;
							bResolved = true;
							break;
						}
					}
					if (!bResolved)
					{
						++RejectedByBlock;
						continue;
					}
				}

				FCandidate Candidate;
				Candidate.Element.BuildingIndex = BuildingIndex;
				Candidate.Element.Face = Face;
				Candidate.Element.ExposedFaceMask = FCatalogModel::MakeFaceMask(Face)
					| FCatalogModel::MakeFaceMask(FCatalogModel::GetAdjacentFace(Face, bForwardEnd));
				Candidate.Element.Distance = CornerDistance;
				Candidate.Element.PathSample = AnchoredSample;
				Candidate.Element.Shape = FClearance::MakeShape(
					AnchoredSample,
					Variant.HalfWidth,
					Variant.HalfDepth,
					Variant.HalfHeight);
				Candidate.Element.bCornerPlacement = true;

				const FVector2D CornerLocation(CornerSample.Location);
				const FVector2D CenterDelta = Candidate.Element.Shape.Footprint.Center - CornerLocation;
				const double IncomingReserve = FMath::Max(
					0.0,
					FClearance::ProjectRadius(Candidate.Element.Shape.Footprint, Incoming)
						- FVector2D::DotProduct(CenterDelta, Incoming));
				const double OutgoingReserve = FMath::Max(
					0.0,
					FClearance::ProjectRadius(Candidate.Element.Shape.Footprint, Outgoing)
						+ FVector2D::DotProduct(CenterDelta, Outgoing));
				Candidate.Element.CoverageStart = CornerDistance - IncomingReserve;
				Candidate.Element.CoverageEnd = CornerDistance + OutgoingReserve;
				if (Candidate.Element.CoverageStart < LeftLimit - UE_DOUBLE_KINDA_SMALL_NUMBER
					|| Candidate.Element.CoverageEnd > RightLimit + UE_DOUBLE_KINDA_SMALL_NUMBER
					|| FClearance::IntersectsAny(Candidate.Element.Shape, ExistingCornerShapes, false))
				{
					++RejectedByArea;
					continue;
				}

				const double CornerExposure = FCatalogModel::CornerExposureScore(Definition, Face, bForwardEnd);
				Candidate.Score = Definition.SelectionWeight
					* FMath::Lerp(1.0, FMath::Max(CornerExposure, 0.1), CornerPreference);
				if (VarietyStrength > UE_DOUBLE_KINDA_SMALL_NUMBER
					&& RecentCornerBuildings.Contains(BuildingIndex))
				{
					Candidate.Score *= FMath::Lerp(1.0, 0.05, VarietyStrength);
				}
				Candidate.TieBreaker = HashCombineFast(
					GetTypeHash(Seed + CornerIndex),
					HashCombineFast(GetTypeHash(BuildingIndex), GetTypeHash(static_cast<uint8>(Face))));
				Candidates.Add(MoveTemp(Candidate));
			}
		}

		if (Candidates.IsEmpty())
		{
			if (RejectedByBlock > 0 && RejectedByExposure == 0 && RejectedByArea == 0)
			{
				OutRejectionReason = ECornerRejectionReason::OutsideBlock;
			}
			else if (RejectedByArea > 0)
			{
				OutRejectionReason = ECornerRejectionReason::InsufficientArea;
			}
			else
			{
				OutRejectionReason = ECornerRejectionReason::NoEligibleFace;
			}
			return false;
		}
		const bool bHasFreshCandidate = VarietyStrength > UE_DOUBLE_KINDA_SMALL_NUMBER
			&& Candidates.ContainsByPredicate([&RecentCornerBuildings](const FCandidate& Candidate)
			{
				return !RecentCornerBuildings.Contains(Candidate.Element.BuildingIndex);
			});
		if (bHasFreshCandidate)
		{
			Candidates.RemoveAll([&RecentCornerBuildings](const FCandidate& Candidate)
			{
				return RecentCornerBuildings.Contains(Candidate.Element.BuildingIndex);
			});
		}
		Candidates.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (!FMath::IsNearlyEqual(A.Score, B.Score, 1.e-6))
			{
				return A.Score > B.Score;
			}
			return A.TieBreaker < B.TieBreaker;
		});

		OutReservation.CornerDistance = CornerDistance;
		OutReservation.Element = MoveTemp(Candidates[0].Element);
		return true;
	}

	void UpdateHistoryForElement(const FResolvedElement& Element, FSelectionHistory& History)
	{
		if (History.FirstBuildingDistance[Element.BuildingIndex] >= TNumericLimits<double>::Max() * 0.5)
		{
			History.FirstBuildingDistance[Element.BuildingIndex] = Element.Distance;
		}
		History.LastBuildingDistance[Element.BuildingIndex] = Element.Distance;
		History.LastBuildingFace[Element.BuildingIndex] = Element.Face;
		History.HasBuildingFace[Element.BuildingIndex] = true;
	}

	bool ValidateResolvedLayout(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const TArray<FResolvedElement>& Elements,
		const EDeepLevelBuildingClearancePolicy ClearancePolicy)
	{
		const bool bDecorativeBlock = ClearancePolicy == EDeepLevelBuildingClearancePolicy::DecorativeBlock;
		for (int32 AIndex = 0; AIndex < Elements.Num(); ++AIndex)
		{
			if (!Catalog.Buildings.IsValidIndex(Elements[AIndex].BuildingIndex)
				|| !FCatalogModel::ValidateExposure(
					Catalog.Buildings[Elements[AIndex].BuildingIndex],
					Elements[AIndex].ExposedFaceMask))
			{
				return false;
			}
			for (int32 BIndex = AIndex + 1; BIndex < Elements.Num(); ++BIndex)
			{
				const bool bCornerPair = Elements[AIndex].bCornerPlacement || Elements[BIndex].bCornerPlacement;
				const bool bFacadesIntersect = FClearance::FacadesIntersect(Elements[AIndex].Shape.Facade, Elements[BIndex].Shape.Facade);
				const bool bFootprintsOverlap = FClearance::FootprintsOverlap(Elements[AIndex].Shape.Footprint, Elements[BIndex].Shape.Footprint);
				if (!bDecorativeBlock && (bFacadesIntersect || bFootprintsOverlap))
				{
					return false;
				}
				if (bDecorativeBlock)
				{
					if (bCornerPair && (bFacadesIntersect || bFootprintsOverlap))
					{
						return false;
					}
					if (!bCornerPair)
					{
						const bool bSameSpan = Elements[AIndex].SpanIndex != INDEX_NONE && Elements[AIndex].SpanIndex == Elements[BIndex].SpanIndex;
						if (bFacadesIntersect)
						{
							return false;
						}
						if (!bSameSpan && bFootprintsOverlap)
						{
							return false;
						}
					}
				}
			}
		}
		return true;
	}
}

bool FDeepLevelBuildingLinePackingSolver::Solve(
	const UDeepLevelBuildingPlacementCatalog& Catalog,
	const FDeepLevelBuildingLinePath& Path,
	const int32 Seed,
	const double VarietyStrength,
	const double CornerPreference,
	const EDeepLevelCornerPlacementFlags CornerPlacement,
	FDeepLevelBuildingLinePlan& OutPlan,
	FText& OutError,
	const bool bAllowEmpty,
	const bool bSkipUnplaceableCorners,
	const EDeepLevelBuildingClearancePolicy ClearancePolicy,
	const FDeepLevelBuildingPlacementCandidateResolver* CandidateResolver)
{
	using namespace DeepLevelBuildingLinePacking;
	const bool bDecorativeBlock = ClearancePolicy == EDeepLevelBuildingClearancePolicy::DecorativeBlock;
	OutPlan = {};
	OutError = FText::GetEmpty();
	if (Path.GetLength() <= UE_DOUBLE_SMALL_NUMBER)
	{
		OutError = LOCTEXT("InvalidSplineLength", "Spline length must be greater than zero.");
		return false;
	}
	if (Catalog.Buildings.IsEmpty())
	{
		OutError = LOCTEXT("EmptyCatalog", "Building catalog contains no buildings.");
		return false;
	}

	TArray<FModuleVariant> Variants;
	int32 ModuleCount = 0;
	if (!FCatalogModel::Build(Catalog, Variants, ModuleCount, OutError))
	{
		return false;
	}
	const double ClampedVariety = FMath::Clamp(VarietyStrength, 0.0, 1.0);
	const double ClampedCornerPreference = FMath::Clamp(CornerPreference, 0.0, 1.0);
	TArray<double> CornerDistances;
	Path.GetHardCornerDistances(CornerStartDegrees, CornerDistances);

	TArray<TOptional<FCornerReservation>> CornerReservations;
	CornerReservations.SetNum(CornerDistances.Num());
	TArray<FClearanceShape> CornerShapes;
	TSet<int32> RecentCornerBuildings;
	TOptional<int32> FirstCornerBuilding;
	for (int32 CornerIndex = 0; CornerIndex < CornerDistances.Num(); ++CornerIndex)
	{
		FDeepLevelBuildingLinePathSample CornerSample;
		FVector2D Incoming;
		FVector2D Outgoing;
		FVector2D BlockInward;
		if (!BuildCornerSample(Path, CornerDistances[CornerIndex], CornerSample, Incoming, Outgoing, BlockInward))
		{
			OutError = FText::Format(
				LOCTEXT("CornerFrameFailure", "Building Line could not resolve spline corner {0}."),
				FText::AsNumber(CornerIndex + 1));
			return false;
		}
		const bool bOuterCorner = IsOuterCornerOnSplineRight(Incoming, Outgoing);
		const EDeepLevelCornerPlacementFlags RequiredFlag = bOuterCorner
			? EDeepLevelCornerPlacementFlags::Outer
			: EDeepLevelCornerPlacementFlags::Inner;
		if (!EnumHasAnyFlags(CornerPlacement, RequiredFlag))
		{
			continue;
		}

		double LeftLimit = CornerIndex == 0
			? 0.0
			: (CornerDistances[CornerIndex - 1] + CornerDistances[CornerIndex]) * 0.5;
		double RightLimit = CornerIndex + 1 == CornerDistances.Num()
			? Path.GetLength()
			: (CornerDistances[CornerIndex] + CornerDistances[CornerIndex + 1]) * 0.5;
		if (Path.IsClosed())
		{
			const double PreviousCorner = CornerIndex == 0
				? CornerDistances.Last() - Path.GetLength()
				: CornerDistances[CornerIndex - 1];
			const double NextCorner = CornerIndex + 1 == CornerDistances.Num()
				? CornerDistances[0] + Path.GetLength()
				: CornerDistances[CornerIndex + 1];
			LeftLimit = (PreviousCorner + CornerDistances[CornerIndex]) * 0.5;
			RightLimit = (CornerDistances[CornerIndex] + NextCorner) * 0.5;
		}
		if (Path.IsClosed() && CornerIndex + 1 == CornerDistances.Num() && FirstCornerBuilding.IsSet())
		{
			RecentCornerBuildings.Add(FirstCornerBuilding.GetValue());
		}
		ECornerRejectionReason RejectionReason = ECornerRejectionReason::None;
		FCornerReservation Reservation;
		if (!BuildCornerReservation(
			Catalog,
			Path,
			CornerDistances[CornerIndex],
			LeftLimit,
			RightLimit,
			Seed,
			CornerIndex,
			ClampedVariety,
			ClampedCornerPreference,
			RecentCornerBuildings,
			CornerShapes,
			ClearancePolicy,
			CandidateResolver,
			Reservation,
			RejectionReason))
		{
			if (bSkipUnplaceableCorners)
			{
				OutPlan.SkippedCornerIndices.Add(CornerIndex + 1);
				continue;
			}
			const TCHAR* ReasonLabel = TEXT("unknown");
			switch (RejectionReason)
			{
			case ECornerRejectionReason::NoEligibleFace: ReasonLabel = TEXT("no eligible exposure face"); break;
			case ECornerRejectionReason::OutsideBlock: ReasonLabel = TEXT("footprint outside block boundary"); break;
			case ECornerRejectionReason::InsufficientArea: ReasonLabel = TEXT("insufficient area between neighbors"); break;
			case ECornerRejectionReason::AllCandidatesRepeat: ReasonLabel = TEXT("all candidates filtered by variety"); break;
			default: break;
			}
			OutError = FText::Format(
				LOCTEXT("CornerReservationFailure", "No calibrated building satisfies constraints at spline corner {0}: {1}."),
				FText::AsNumber(CornerIndex + 1),
				FText::FromStringView(ReasonLabel));
			return false;
		}
		if (!FirstCornerBuilding.IsSet())
		{
			FirstCornerBuilding = Reservation.Element.BuildingIndex;
		}
		RecentCornerBuildings.Add(Reservation.Element.BuildingIndex);
		if (RecentCornerBuildings.Num() >= Catalog.Buildings.Num())
		{
			RecentCornerBuildings.Reset();
		}
		CornerShapes.Add(Reservation.Element.Shape);
		CornerReservations[CornerIndex] = MoveTemp(Reservation);
	}

	FSelectionHistory History = FCatalogModel::MakeHistory(Catalog.Buildings.Num(), ModuleCount);
	TArray<FResolvedElement> ResolvedElements;
	TArray<FClearanceShape> Shapes = CornerShapes;
	const int32 CornerShapeCount = CornerShapes.Num();
	int32 SelectionIndex = 0;
	auto PackSpan = [
		&Path,
		&CornerDistances,
		&Variants,
		&Catalog,
		Seed,
		ClampedVariety,
		&SelectionIndex,
		&History,
		CornerShapeCount,
		&Shapes,
		&ResolvedElements,
		&OutError,
		bAllowEmpty,
		bDecorativeBlock,
		ClearancePolicy,
		CandidateResolver](const double SpanStart, const double SpanEnd, const int32 ZoneIndex, const bool bCloseLoop)
	{
		TArray<FResolvedElement> SpanElements;
		FSelectionHistory SpanHistory;
		if (!FSpanPacker::Plan(
			Path,
			SpanStart,
			SpanEnd,
			!CornerDistances.IsEmpty(),
			Variants,
			Catalog,
			Seed,
			SelectionIndex,
			ClampedVariety,
			Catalog.Buildings.Num() > 1,
			bCloseLoop && !bDecorativeBlock,
			ClearancePolicy,
			CandidateResolver,
			History,
			CornerShapeCount,
			ZoneIndex,
			Shapes,
			SpanElements,
			SpanHistory))
		{
			if (bAllowEmpty)
			{
				return true;
			}
			OutError = FText::Format(
				LOCTEXT("SpanPackingFailure", "Building Line cannot safely pack route span {0}; check placement volumes near this section."),
				FText::AsNumber(ZoneIndex + 1));
			return false;
		}
		SelectionIndex += SpanElements.Num();
		ResolvedElements.Append(MoveTemp(SpanElements));
		History = MoveTemp(SpanHistory);
		return true;
	};
	auto AddCorner = [&ResolvedElements, &History, &SelectionIndex](const FCornerReservation& Corner)
	{
		ResolvedElements.Add(Corner.Element);
		UpdateHistoryForElement(Corner.Element, History);
		++SelectionIndex;
	};

	if (Path.IsClosed())
	{
		const bool bHasSeamCorner = !CornerDistances.IsEmpty()
			&& FMath::IsNearlyZero(CornerDistances[0], UE_DOUBLE_KINDA_SMALL_NUMBER);
		const TOptional<FCornerReservation>* SeamCorner = bHasSeamCorner ? &CornerReservations[0] : nullptr;
		double SpanStart = 0.0;
		int32 FirstTraversedCorner = 0;
		if (SeamCorner)
		{
			FirstTraversedCorner = 1;
			if (SeamCorner->IsSet())
			{
				AddCorner(SeamCorner->GetValue());
				SpanStart = SeamCorner->GetValue().Element.CoverageEnd;
			}
		}

		for (int32 CornerIndex = FirstTraversedCorner; CornerIndex < CornerDistances.Num(); ++CornerIndex)
		{
			const TOptional<FCornerReservation>& Corner = CornerReservations[CornerIndex];
			const double SpanEnd = Corner.IsSet()
				? Corner.GetValue().Element.CoverageStart
				: CornerDistances[CornerIndex];
			if (!PackSpan(SpanStart, SpanEnd, CornerIndex, false))
			{
				return false;
			}
			if (Corner.IsSet())
			{
				AddCorner(Corner.GetValue());
				SpanStart = Corner.GetValue().Element.CoverageEnd;
			}
			else
			{
				SpanStart = CornerDistances[CornerIndex];
			}
		}

		const double FinalSpanEnd = SeamCorner && SeamCorner->IsSet()
			? Path.GetLength() + SeamCorner->GetValue().Element.CoverageStart
			: Path.GetLength();
		if (!PackSpan(SpanStart, FinalSpanEnd, CornerDistances.Num(), true))
		{
			return false;
		}
	}
	else
	{
		double SpanStart = 0.0;
		for (int32 ZoneIndex = 0; ZoneIndex <= CornerDistances.Num(); ++ZoneIndex)
		{
			const TOptional<FCornerReservation>* Corner = ZoneIndex < CornerReservations.Num()
				? &CornerReservations[ZoneIndex]
				: nullptr;
			const double SpanEnd = Corner
				? (Corner->IsSet() ? Corner->GetValue().Element.CoverageStart : CornerDistances[ZoneIndex])
				: Path.GetLength();
			if (!PackSpan(SpanStart, SpanEnd, ZoneIndex, false))
			{
				return false;
			}
			if (Corner)
			{
				if (Corner->IsSet())
				{
					AddCorner(Corner->GetValue());
					SpanStart = Corner->GetValue().Element.CoverageEnd;
				}
				else
				{
					SpanStart = CornerDistances[ZoneIndex];
				}
			}
		}
	}

	ResolvedElements.Sort([](const FResolvedElement& A, const FResolvedElement& B)
	{
		return A.Distance < B.Distance;
	});
	if (ResolvedElements.IsEmpty())
	{
		if (bAllowEmpty) { return true; }
		OutError = LOCTEXT("NoBuildingFits", "No calibrated building fits within the Building Line spline.");
		return false;
	}
	if (!ValidateResolvedLayout(Catalog, ResolvedElements, ClearancePolicy))
	{
		OutError = LOCTEXT(
			"LayoutValidationFailure",
			"Building Line rejected a layout that violates face exposure or intersects calibrated placement volumes.");
		return false;
	}

	double UsedLength = 0.0;
	for (const FResolvedElement& Element : ResolvedElements)
	{
		FDeepLevelBuildingLinePlacement& Placement = OutPlan.Placements.Emplace_GetRef();
		Placement.BuildingClass = Catalog.Buildings[Element.BuildingIndex].BuildingClass;
		Placement.Distance = Element.Distance;
		Placement.CoverageStart = Element.CoverageStart;
		Placement.CoverageEnd = Element.CoverageEnd;
		Placement.StreetFace = Element.Face;
		Placement.PathSample = Element.PathSample;
		Placement.bCornerPlacement = Element.bCornerPlacement;
		const FDeepLevelResolvedBuildingGeometry Geometry = FDeepLevelBuildingPlacementGeometry::ResolveGeometry(
			Catalog.Buildings[Element.BuildingIndex],
			Element.Face);
		UsedLength += Geometry.HalfWidth * 2.0;
	}
	OutPlan.UsedLength = UsedLength;
	if (CornerDistances.IsEmpty())
	{
		OutPlan.StartOffset = FMath::Max(0.0, OutPlan.Placements[0].CoverageStart);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingLineClearance ----


namespace DeepLevelBuildingLinePacking
{
	double FClearance::Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	bool FClearance::FacadesIntersect(const FFacadeSegment& A, const FFacadeSegment& B)
	{
		const FVector2D R = A.End - A.Start;
		const FVector2D S = B.End - B.Start;
		const FVector2D Offset = B.Start - A.Start;
		const double Denominator = Cross2D(R, S);
		if (FMath::Abs(Denominator) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			if (FMath::Abs(Cross2D(Offset, R)) > IntersectionTolerance)
			{
				return false;
			}
			const FVector2D Axis = R.GetSafeNormal();
			if (Axis.IsNearlyZero())
			{
				return false;
			}
			const double AMin = FVector2D::DotProduct(A.Start, Axis);
			const double AMax = FVector2D::DotProduct(A.End, Axis);
			const double BMin = FVector2D::DotProduct(B.Start, Axis);
			const double BMax = FVector2D::DotProduct(B.End, Axis);
			return FMath::Min(FMath::Max(AMin, AMax), FMath::Max(BMin, BMax))
				- FMath::Max(FMath::Min(AMin, AMax), FMath::Min(BMin, BMax)) > IntersectionTolerance;
		}

		const double T = Cross2D(Offset, S) / Denominator;
		const double U = Cross2D(Offset, R) / Denominator;
		constexpr double EndpointTolerance = 1.e-3;
		return T > EndpointTolerance && T < 1.0 - EndpointTolerance
			&& U > EndpointTolerance && U < 1.0 - EndpointTolerance;
	}

	double FClearance::ProjectRadius(const FFootprint& Footprint, const FVector2D& Axis)
	{
		return FMath::Abs(FVector2D::DotProduct(Footprint.Forward, Axis)) * Footprint.HalfWidth
			+ FMath::Abs(FVector2D::DotProduct(Footprint.Right, Axis)) * Footprint.HalfDepth;
	}

	bool FClearance::FootprintsOverlap(const FFootprint& A, const FFootprint& B)
	{
		if (A.MaxZ <= B.MinZ + IntersectionTolerance || B.MaxZ <= A.MinZ + IntersectionTolerance)
		{
			return false;
		}
		const FVector2D Delta = B.Center - A.Center;
		for (const FVector2D& Axis : {A.Forward, A.Right, B.Forward, B.Right})
		{
			if (FMath::Abs(FVector2D::DotProduct(Delta, Axis))
				>= ProjectRadius(A, Axis) + ProjectRadius(B, Axis) - IntersectionTolerance)
			{
				return false;
			}
		}
		return true;
	}

bool FClearance::IntersectsAny(
	const FClearanceShape& Shape,
	const TArray<FClearanceShape>& ExistingShapes,
	const bool bIgnoreFootprintOverlap)
{
	if (bIgnoreFootprintOverlap)
	{
		return false;
	}
	return ExistingShapes.ContainsByPredicate([&Shape](const FClearanceShape& Existing)
	{
		return FacadesIntersect(Shape.Facade, Existing.Facade)
			|| FootprintsOverlap(Shape.Footprint, Existing.Footprint);
	});
}

	FClearanceShape FClearance::MakeShape(
		const FDeepLevelBuildingLinePathSample& Sample,
		const double HalfWidth,
		const double HalfDepth,
		const double HalfHeight)
	{
		FClearanceShape Shape;
		const FVector2D Location(Sample.Location);
		Shape.Footprint.Forward = FVector2D(Sample.Forward).GetSafeNormal();
		Shape.Footprint.Right = FVector2D(Sample.Right).GetSafeNormal();
		Shape.Facade = {
			Location - Shape.Footprint.Forward * HalfWidth,
			Location + Shape.Footprint.Forward * HalfWidth};
		Shape.Footprint.Center = Location + Shape.Footprint.Right * HalfDepth;
		Shape.Footprint.HalfWidth = HalfWidth;
		Shape.Footprint.HalfDepth = HalfDepth;
		Shape.Footprint.MinZ = Sample.Location.Z;
		Shape.Footprint.MaxZ = Sample.Location.Z + HalfHeight * 2.0;
		return Shape;
	}
}
