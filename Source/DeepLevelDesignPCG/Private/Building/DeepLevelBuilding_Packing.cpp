// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingPCG.h"

// ---- DeepLevelBuildingLineSpanPacker ----


#include "Algo/Reverse.h"

namespace DeepLevelBuildingLinePacking
{
	namespace
	{
		struct FSearchNode
		{
			int32 PreviousNode = INDEX_NONE;
			int32 VariantIndex = INDEX_NONE;
			int32 PlacementCount = 0;
			double UsedWidth = 0.0;
			double Score = 0.0;
			FSelectionHistory History;
		};

		bool HasFirstDistance(const double Distance)
		{
			return Distance < TNumericLimits<double>::Max() * 0.5;
		}

		bool HasLastDistance(const double Distance)
		{
			return Distance > -TNumericLimits<double>::Max() * 0.5;
		}

		double ScoreTransition(
			const FModuleVariant& Variant,
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			const int32 Seed,
			const int32 SelectionIndex,
			const double StartDistance,
			const double VarietyStrength,
			const bool bHasBuildingAlternatives,
			FSelectionHistory& InOutHistory)
		{
			double Score = FMath::Loge(FMath::Max(Variant.Weight, 0.001));
			double LocalOffset = 0.0;
			for (const FElementVariant& Element : Variant.Elements)
			{
				const double CenterDistance = StartDistance + LocalOffset + Element.HalfWidth;
				const FDeepLevelBuildingPlacementDefinition& Definition = Catalog.Buildings[Element.BuildingIndex];
				const double VisualWidth = FMath::Max(
					Definition.PlacementVolume.Extent.X,
					Definition.PlacementVolume.Extent.Y) * 2.0;
				const double LastDistance = InOutHistory.LastBuildingDistance[Element.BuildingIndex];
				if (LastDistance > -TNumericLimits<double>::Max() * 0.5)
				{
					const double RepeatDistance = CenterDistance - LastDistance;
					const double Readiness = FMath::Clamp(
						RepeatDistance / FMath::Max(VisualWidth * 4.0, 1.0),
						0.05,
						1.0);
					Score -= (1.0 - Readiness) * VarietyStrength * 25.0;
					const double MinimumRepeatDistance = Element.HalfWidth * 4.0;
					if (bHasBuildingAlternatives && RepeatDistance + UE_DOUBLE_KINDA_SMALL_NUMBER < MinimumRepeatDistance)
					{
						Score -= VarietyStrength * 1000.0;
					}
				}
				if (InOutHistory.HasBuildingFace[Element.BuildingIndex])
				{
					const double FaceVariety = InOutHistory.LastBuildingFace[Element.BuildingIndex] == Element.Face ? 0.35 : 1.25;
					Score += FMath::Loge(FMath::Lerp(1.0, FaceVariety, VarietyStrength));
				}
				Score += FMath::Loge(FMath::Max(Element.ExposureScore, 0.01));
				if (!HasFirstDistance(InOutHistory.FirstBuildingDistance[Element.BuildingIndex]))
				{
					InOutHistory.FirstBuildingDistance[Element.BuildingIndex] = CenterDistance;
				}
				InOutHistory.LastBuildingDistance[Element.BuildingIndex] = CenterDistance;
				InOutHistory.LastBuildingFace[Element.BuildingIndex] = Element.Face;
				InOutHistory.HasBuildingFace[Element.BuildingIndex] = true;
				LocalOffset += Element.HalfWidth * 2.0;
			}

			const double LastModuleDistance = InOutHistory.LastModuleDistance[Variant.ModuleIndex];
			if (LastModuleDistance > -TNumericLimits<double>::Max() * 0.5)
			{
				const double Readiness = FMath::Clamp(
					(StartDistance - LastModuleDistance) / FMath::Max(Variant.Width * 4.0, 1.0),
					0.05,
					1.0);
				Score -= (1.0 - Readiness) * VarietyStrength * 10.0;
			}
			if (!HasFirstDistance(InOutHistory.FirstModuleDistance[Variant.ModuleIndex]))
			{
				InOutHistory.FirstModuleDistance[Variant.ModuleIndex] = StartDistance;
			}
			InOutHistory.LastModuleDistance[Variant.ModuleIndex] = StartDistance + Variant.Width;

			const uint32 Hash = HashCombineFast(
				GetTypeHash(Seed + SelectionIndex),
				HashCombineFast(GetTypeHash(Variant.ModuleIndex), Variant.Signature));
			FRandomStream Random(static_cast<int32>(Hash));
			return Score + Random.FRand() * 0.25;
		}

		double ScoreCircularClosure(
			const FSelectionHistory& History,
			const TArray<FModuleVariant>& Variants,
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			const double RouteLength,
			const double VarietyStrength,
			const bool bHasBuildingAlternatives)
		{
			double Score = 0.0;
			for (int32 BuildingIndex = 0; BuildingIndex < Catalog.Buildings.Num(); ++BuildingIndex)
			{
				const double FirstDistance = History.FirstBuildingDistance[BuildingIndex];
				const double LastDistance = History.LastBuildingDistance[BuildingIndex];
				if (!HasFirstDistance(FirstDistance) || !HasLastDistance(LastDistance))
				{
					continue;
				}
				const FDeepLevelBuildingPlacementDefinition& Definition = Catalog.Buildings[BuildingIndex];
				const double VisualWidth = FMath::Max(
					Definition.PlacementVolume.Extent.X,
					Definition.PlacementVolume.Extent.Y) * 2.0;
				const double RepeatDistance = FMath::Max(0.0, RouteLength - LastDistance + FirstDistance);
				const double Readiness = FMath::Clamp(
					RepeatDistance / FMath::Max(VisualWidth * 4.0, 1.0),
					0.05,
					1.0);
				Score -= (1.0 - Readiness) * VarietyStrength * 25.0;
				const double MinimumRepeatDistance = Definition.PlacementVolume.Extent.X * 4.0;
				if (bHasBuildingAlternatives && RepeatDistance + UE_DOUBLE_KINDA_SMALL_NUMBER < MinimumRepeatDistance)
				{
					Score -= VarietyStrength * 1000.0;
				}
			}

			for (int32 ModuleIndex = 0; ModuleIndex < History.FirstModuleDistance.Num(); ++ModuleIndex)
			{
				const double FirstDistance = History.FirstModuleDistance[ModuleIndex];
				const double LastDistance = History.LastModuleDistance[ModuleIndex];
				if (!HasFirstDistance(FirstDistance) || !HasLastDistance(LastDistance))
				{
					continue;
				}
				double ModuleWidth = 1.0;
				for (const FModuleVariant& Variant : Variants)
				{
					if (Variant.ModuleIndex == ModuleIndex)
					{
						ModuleWidth = FMath::Max(ModuleWidth, Variant.Width);
					}
				}
				const double RepeatDistance = FMath::Max(0.0, RouteLength - LastDistance + FirstDistance);
				const double Readiness = FMath::Clamp(
					RepeatDistance / FMath::Max(ModuleWidth * 4.0, 1.0),
					0.05,
					1.0);
				Score -= (1.0 - Readiness) * VarietyStrength * 10.0;
			}
			return Score;
		}

		void InsertState(
			FSearchNode&& Candidate,
			const int32 BucketIndex,
			TArray<FSearchNode>& Nodes,
			TArray<TArray<int32>>& Buckets)
		{
			TArray<int32>& Bucket = Buckets[BucketIndex];
			for (const int32 ExistingIndex : Bucket)
			{
				const FSearchNode& Existing = Nodes[ExistingIndex];
				if (FMath::IsNearlyEqual(Existing.UsedWidth, Candidate.UsedWidth, 0.1)
					&& Existing.VariantIndex == Candidate.VariantIndex
					&& Existing.Score >= Candidate.Score)
				{
					return;
				}
			}
			const int32 NodeIndex = Nodes.Add(MoveTemp(Candidate));
			Bucket.Add(NodeIndex);
			Bucket.Sort([&Nodes](const int32 A, const int32 B)
			{
				return Nodes[A].Score > Nodes[B].Score;
			});
			if (Bucket.Num() > MaximumStatesPerWidth)
			{
				Bucket.SetNum(MaximumStatesPerWidth, EAllowShrinking::No);
			}
		}

		void BuildCandidates(
			const double SpanLength,
			const double SpanStart,
			const TArray<FModuleVariant>& Variants,
			const UDeepLevelBuildingPlacementCatalog& Catalog,
			const int32 Seed,
			const int32 SelectionIndex,
			const double VarietyStrength,
			const bool bHasBuildingAlternatives,
			const FSelectionHistory& InitialHistory,
			TArray<FSearchNode>& OutNodes,
			TArray<int32>& OutTerminalNodes)
		{
			const int32 MaximumUnits = FMath::Max(0, FMath::FloorToInt32(SpanLength / PackingResolution));
			TArray<TArray<int32>> Buckets;
			Buckets.SetNum(MaximumUnits + 1);
			FSearchNode InitialNode;
			InitialNode.History = InitialHistory;
			OutNodes.Add(MoveTemp(InitialNode));
			Buckets[0].Add(0);

			for (int32 Unit = 0; Unit <= MaximumUnits; ++Unit)
			{
				const TArray<int32> SourceNodes = Buckets[Unit];
				for (const int32 SourceNodeIndex : SourceNodes)
				{
					// InsertState can reallocate OutNodes while expanding this source.
					// Keep a stable snapshot for the complete variant loop.
					const FSearchNode Source = OutNodes[SourceNodeIndex];
					if (Source.PlacementCount >= MaximumPlacementCount)
					{
						continue;
					}
					for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
					{
						const FModuleVariant& Variant = Variants[VariantIndex];
						if (Source.UsedWidth + Variant.Width > SpanLength + UE_DOUBLE_KINDA_SMALL_NUMBER
							|| Source.PlacementCount + Variant.Elements.Num() > MaximumPlacementCount)
						{
							continue;
						}
						FSearchNode Candidate;
						Candidate.PreviousNode = SourceNodeIndex;
						Candidate.VariantIndex = VariantIndex;
						Candidate.PlacementCount = Source.PlacementCount + Variant.Elements.Num();
						Candidate.UsedWidth = Source.UsedWidth + Variant.Width;
						Candidate.Score = Source.Score;
						Candidate.History = Source.History;
						Candidate.Score += ScoreTransition(
							Variant,
							Catalog,
							Seed,
							SelectionIndex + Source.PlacementCount,
							SpanStart + Source.UsedWidth,
							VarietyStrength,
							bHasBuildingAlternatives,
							Candidate.History);
						const int32 TargetUnit = FMath::Clamp(
							FMath::CeilToInt32(Candidate.UsedWidth / PackingResolution),
							0,
							MaximumUnits);
						InsertState(MoveTemp(Candidate), TargetUnit, OutNodes, Buckets);
					}
				}
			}

			for (const TArray<int32>& Bucket : Buckets)
			{
				OutTerminalNodes.Append(Bucket);
			}
			OutTerminalNodes.Remove(0);
			OutTerminalNodes.Sort([&OutNodes](const int32 A, const int32 B)
			{
				if (!FMath::IsNearlyEqual(OutNodes[A].UsedWidth, OutNodes[B].UsedWidth, 0.1))
				{
					return OutNodes[A].UsedWidth > OutNodes[B].UsedWidth;
				}
				return OutNodes[A].Score > OutNodes[B].Score;
			});
			if (OutTerminalNodes.Num() > MaximumLayoutCandidates)
			{
				OutTerminalNodes.SetNum(MaximumLayoutCandidates, EAllowShrinking::No);
			}
		}

		void ReconstructSequence(
			const int32 TerminalNode,
			const TArray<FSearchNode>& Nodes,
			TArray<int32>& OutVariantIndices)
		{
			int32 NodeIndex = TerminalNode;
			while (NodeIndex != INDEX_NONE && Nodes[NodeIndex].VariantIndex != INDEX_NONE)
			{
				OutVariantIndices.Add(Nodes[NodeIndex].VariantIndex);
				NodeIndex = Nodes[NodeIndex].PreviousNode;
			}
			Algo::Reverse(OutVariantIndices);
		}

		bool FindClearPlacement(
			const FDeepLevelBuildingLinePath& Path,
			const FElementVariant& Element,
			const double InitialCenter,
			const double MaximumCenter,
			const TArray<FClearanceShape>& ExistingShapes,
			FResolvedElement& OutElement)
		{
			auto Evaluate = [&Path, &Element, &ExistingShapes](const double Distance, FResolvedElement& Result)
			{
				if (!Path.Sample(Distance, Result.PathSample))
				{
					return false;
				}
				Result.BuildingIndex = Element.BuildingIndex;
				Result.Face = Element.Face;
				Result.ExposedFaceMask = Element.ExposedFaceMask;
				Result.Distance = Distance;
				Result.CoverageStart = Distance - Element.HalfWidth;
				Result.CoverageEnd = Distance + Element.HalfWidth;
				Result.Shape = FClearance::MakeShape(
					Result.PathSample,
					Element.HalfWidth,
					Element.HalfDepth,
					Element.HalfHeight);
				return !FClearance::IntersectsAny(Result.Shape, ExistingShapes);
			};

			if (Evaluate(InitialCenter, OutElement))
			{
				return true;
			}
			const double Step = FMath::Max(FMath::Max(Element.HalfWidth, Element.HalfDepth) * 0.025, 5.0);
			double BlockedDistance = InitialCenter;
			for (double ClearDistance = InitialCenter + Step; ClearDistance <= MaximumCenter; ClearDistance += Step)
			{
				FResolvedElement Candidate;
				if (!Evaluate(ClearDistance, Candidate))
				{
					BlockedDistance = ClearDistance;
					continue;
				}
				for (int32 Iteration = 0; Iteration < 10; ++Iteration)
				{
					const double Middle = (BlockedDistance + ClearDistance) * 0.5;
					FResolvedElement MiddleCandidate;
					if (Evaluate(Middle, MiddleCandidate))
					{
						ClearDistance = Middle;
						Candidate = MoveTemp(MiddleCandidate);
					}
					else
					{
						BlockedDistance = Middle;
					}
				}
				OutElement = MoveTemp(Candidate);
				return true;
			}
			return false;
		}

		bool LayoutSequence(
			const FDeepLevelBuildingLinePath& Path,
			const double SpanStart,
			const double SpanEnd,
			const bool bDistributeSlack,
			const TArray<int32>& VariantIndices,
			const TArray<FModuleVariant>& Variants,
			const TArray<FClearanceShape>& BaseShapes,
			TArray<FResolvedElement>& OutElements,
			TArray<FClearanceShape>& OutShapes)
		{
			double UsedWidth = 0.0;
			int32 ElementCount = 0;
			for (const int32 VariantIndex : VariantIndices)
			{
				UsedWidth += Variants[VariantIndex].Width;
				ElementCount += Variants[VariantIndex].Elements.Num();
			}
			const double Remainder = FMath::Max(0.0, SpanEnd - SpanStart - UsedWidth);
			const double InteriorGap = bDistributeSlack ? Remainder / FMath::Max(ElementCount + 1, 1) : 0.0;
			double Cursor = SpanStart + (bDistributeSlack ? InteriorGap : Remainder * 0.5);
			TArray<FClearanceShape> CandidateShapes = BaseShapes;

			for (const int32 VariantIndex : VariantIndices)
			{
				for (const FElementVariant& Element : Variants[VariantIndex].Elements)
				{
					FResolvedElement Resolved;
					if (!FindClearPlacement(
						Path,
						Element,
						Cursor + Element.HalfWidth,
						SpanEnd - Element.HalfWidth,
						CandidateShapes,
						Resolved))
					{
						return false;
					}
					Cursor = Resolved.CoverageEnd + InteriorGap;
					if (Cursor > SpanEnd + InteriorGap + UE_DOUBLE_KINDA_SMALL_NUMBER)
					{
						return false;
					}
					CandidateShapes.Add(Resolved.Shape);
					OutElements.Add(MoveTemp(Resolved));
				}
			}
			OutShapes = MoveTemp(CandidateShapes);
			return true;
		}
	}

	bool FSpanPacker::Plan(
		const FDeepLevelBuildingLinePath& Path,
		const double SpanStart,
		const double SpanEnd,
		const bool bDistributeSlack,
		const TArray<FModuleVariant>& Variants,
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const int32 Seed,
		const int32 SelectionIndex,
		const double VarietyStrength,
		const bool bHasBuildingAlternatives,
		const bool bCloseLoop,
		const FSelectionHistory& InitialHistory,
		TArray<FClearanceShape>& InOutShapes,
		TArray<FResolvedElement>& OutElements,
		FSelectionHistory& OutHistory)
	{
		const double SpanLength = SpanEnd - SpanStart;
		if (SpanLength <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			OutHistory = InitialHistory;
			return true;
		}

		TArray<FSearchNode> Nodes;
		TArray<int32> TerminalNodes;
		BuildCandidates(
			SpanLength,
			SpanStart,
			Variants,
			Catalog,
			Seed,
			SelectionIndex,
			VarietyStrength,
			bHasBuildingAlternatives,
			InitialHistory,
			Nodes,
			TerminalNodes);
		if (bCloseLoop)
		{
			TerminalNodes.Sort([
				&Nodes,
				&Variants,
				&Catalog,
				RouteLength = Path.GetLength(),
				VarietyStrength,
				bHasBuildingAlternatives](const int32 A, const int32 B)
			{
				if (!FMath::IsNearlyEqual(Nodes[A].UsedWidth, Nodes[B].UsedWidth, 0.1))
				{
					return Nodes[A].UsedWidth > Nodes[B].UsedWidth;
				}
				const double AScore = Nodes[A].Score + ScoreCircularClosure(
					Nodes[A].History,
					Variants,
					Catalog,
					RouteLength,
					VarietyStrength,
					bHasBuildingAlternatives);
				const double BScore = Nodes[B].Score + ScoreCircularClosure(
					Nodes[B].History,
					Variants,
					Catalog,
					RouteLength,
					VarietyStrength,
					bHasBuildingAlternatives);
				return AScore > BScore;
			});
		}

		double MinimumModuleWidth = TNumericLimits<double>::Max();
		for (const FModuleVariant& Variant : Variants)
		{
			MinimumModuleWidth = FMath::Min(MinimumModuleWidth, Variant.Width);
		}
		if (TerminalNodes.IsEmpty())
		{
			OutHistory = InitialHistory;
			return SpanLength + UE_DOUBLE_KINDA_SMALL_NUMBER < MinimumModuleWidth;
		}

		for (const int32 TerminalNode : TerminalNodes)
		{
			TArray<int32> Sequence;
			ReconstructSequence(TerminalNode, Nodes, Sequence);
			TArray<FResolvedElement> CandidateElements;
			TArray<FClearanceShape> CandidateShapes;
			if (LayoutSequence(
				Path,
				SpanStart,
				SpanEnd,
				bDistributeSlack,
				Sequence,
				Variants,
				InOutShapes,
				CandidateElements,
				CandidateShapes))
			{
				OutElements = MoveTemp(CandidateElements);
				InOutShapes = MoveTemp(CandidateShapes);
				OutHistory = Nodes[TerminalNode].History;
				return true;
			}
		}
		return false;
	}
}
