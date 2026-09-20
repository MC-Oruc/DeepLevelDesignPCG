// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DeepLevelBuildingVolFinal
{
	struct FFootprint
	{
		FVector Center = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		double HalfWidth = 0.0;
		double HalfDepth = 0.0;
		double MinZ = 0.0;
		double MaxZ = 0.0;
	};

	struct FFacade
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	struct FPathSample
	{
		FVector Location = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
	};

	struct FPlacement
	{
		FVector Center = FVector::ZeroVector;
		FFacade Facade;
		FFootprint Footprint;
		FPathSample PathSample;
		double Distance = 0.0;
		double CoverageStart = 0.0;
		double CoverageEnd = 0.0;
		int32 SourceIndex = INDEX_NONE;
	};

	struct FPhase
	{
		FName Id;
		int32 BuildingsMovedCount = 0;
		double TotalShiftCm = 0.0;
		int32 ProtectedBuildingCount = 0;
		int32 AcceptedMoveCount = 0;
	};

	struct FResult
	{
		TArray<FPlacement> Placements;
		TArray<FPhase> Phases;
		TArray<TArray<FPlacement>> PhasePlacements;
		bool bSolved = false;
		int32 BuildingsMovedCount = 0;
		double TotalShiftCm = 0.0;
	};

	void GetShapeCorners(const FFootprint& Footprint, TStaticArray<FVector2D, 4>& OutCorners);
	double ProjectRadius(const FFootprint& Footprint, const FVector2D& Axis);
	bool FootprintsOverlap(const FFootprint& A, const FFootprint& B);
	bool FootprintContained(const FFootprint& Footprint, const TArray<FVector2D>& Polygon);
	FPlacement Translate(const FPlacement& Placement, double DeltaX, double DeltaY);
	TArray<FPlacement> AlignQuadGreedy(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		double MaximumShift = 15000.0,
		int32 MaximumPasses = 8);
	TArray<FPlacement> SolveV1(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		int32 Iterations = 240);
	TArray<FPlacement> SolveV2(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		int32 Iterations = 360);
	TArray<FPlacement> SolveV3(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		int32 Iterations = 300,
		double StepScale = 0.055);
	TArray<FPlacement> SolveV7(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		bool bFine);
	TArray<FPlacement> SolveV9Stage(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		int32 StageIndex);
	FResult Solve(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source);
}
