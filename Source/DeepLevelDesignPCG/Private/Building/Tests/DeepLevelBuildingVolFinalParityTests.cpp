// Copyright <--\, Inc. All Rights Reserved.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Building/DeepLevelBuildingVolFinalSolver.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using namespace DeepLevelBuildingVolFinal;

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object)
	{
		return FVector(Object->GetNumberField(TEXT("x")), Object->GetNumberField(TEXT("y")), Object->GetNumberField(TEXT("z")));
	}

	FPlacement ReadPlacement(const TSharedPtr<FJsonObject>& Object)
	{
		FPlacement Result;
		Result.SourceIndex = Object->GetIntegerField(TEXT("sourceIndex"));
		Result.Center = ReadVector(Object->GetObjectField(TEXT("center")));
		const TSharedPtr<FJsonObject> Facade = Object->GetObjectField(TEXT("facade"));
		Result.Facade.Start = ReadVector(Facade->GetObjectField(TEXT("start")));
		Result.Facade.End = ReadVector(Facade->GetObjectField(TEXT("end")));
		const TSharedPtr<FJsonObject> Footprint = Object->GetObjectField(TEXT("footprint"));
		Result.Footprint.Center = ReadVector(Footprint->GetObjectField(TEXT("center")));
		Result.Footprint.Forward = ReadVector(Footprint->GetObjectField(TEXT("forward")));
		Result.Footprint.Right = ReadVector(Footprint->GetObjectField(TEXT("right")));
		Result.Footprint.HalfWidth = Footprint->GetNumberField(TEXT("halfWidth"));
		Result.Footprint.HalfDepth = Footprint->GetNumberField(TEXT("halfDepth"));
		Result.Footprint.MinZ = Footprint->GetNumberField(TEXT("minZ"));
		Result.Footprint.MaxZ = Footprint->GetNumberField(TEXT("maxZ"));
		const TSharedPtr<FJsonObject> Sample = Object->GetObjectField(TEXT("pathSample"));
		Result.PathSample.Location = ReadVector(Sample->GetObjectField(TEXT("location")));
		Result.PathSample.Forward = ReadVector(Sample->GetObjectField(TEXT("forward")));
		Result.PathSample.Right = ReadVector(Sample->GetObjectField(TEXT("right")));
		Result.Distance = Object->GetNumberField(TEXT("distance"));
		Result.CoverageStart = Object->GetNumberField(TEXT("coverageStart"));
		Result.CoverageEnd = Object->GetNumberField(TEXT("coverageEnd"));
		return Result;
	}

	TArray<FPlacement> ReadPlacements(const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		TArray<FPlacement> Result;
		Result.Reserve(Values.Num());
		for (const TSharedPtr<FJsonValue>& Value : Values) { Result.Add(ReadPlacement(Value->AsObject())); }
		return Result;
	}

	bool NearlyEqualPlacement(const FPlacement& Actual, const FPlacement& Expected, const double Tolerance)
	{
		return Actual.SourceIndex == Expected.SourceIndex
			&& Actual.Center.Equals(Expected.Center, Tolerance)
			&& Actual.Footprint.Center.Equals(Expected.Footprint.Center, Tolerance)
			&& Actual.Facade.Start.Equals(Expected.Facade.Start, Tolerance)
			&& Actual.Facade.End.Equals(Expected.Facade.End, Tolerance)
			&& Actual.PathSample.Location.Equals(Expected.PathSample.Location, Tolerance)
			&& FMath::IsNearlyEqual(Actual.Distance, Expected.Distance, Tolerance)
			&& FMath::IsNearlyEqual(Actual.CoverageStart, Expected.CoverageStart, Tolerance)
			&& FMath::IsNearlyEqual(Actual.CoverageEnd, Expected.CoverageEnd, Tolerance);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDeepLevelBuildingVolFinalParityTest,
	"DeepLevelDesignPCG.Editor.BuildingLine.VolFinalParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDeepLevelBuildingVolFinalParityTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = FPaths::Combine(FPaths::ProjectPluginsDir(),
		TEXT("DeepLevelDesignPCG/Source/DeepLevelDesignPCG/Private/Building/Tests/Data/VolFinalParity.json"));
	FString Json;
	if (!TestTrue(TEXT("Vol.Final parity fixture loads"), FFileHelper::LoadFileToString(Json, *FixturePath))) { return false; }
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!TestTrue(TEXT("Vol.Final parity fixture parses"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())) { return false; }

	const TArray<TSharedPtr<FJsonValue>>& Cases = Root->GetArrayField(TEXT("cases"));
	TestEqual(TEXT("All captured closed frontages are covered"), Cases.Num(), 20);
	constexpr double Tolerance = 0.1;
	for (const TSharedPtr<FJsonValue>& CaseValue : Cases)
	{
		const TSharedPtr<FJsonObject> Case = CaseValue->AsObject();
		const FString Name = Case->GetStringField(TEXT("name"));
		TArray<FVector2D> Polygon;
		for (const TSharedPtr<FJsonValue>& Value : Case->GetArrayField(TEXT("polygon")))
		{
			const FVector Point = ReadVector(Value->AsObject());
			Polygon.Add(FVector2D(Point));
		}
		const TArray<FPlacement> Source = ReadPlacements(Case->GetArrayField(TEXT("source")));
		const FResult Actual = Solve(Polygon, Source);
		const TArray<TSharedPtr<FJsonValue>>& ExpectedPhases = Case->GetArrayField(TEXT("phases"));
		if (!TestEqual(*FString::Printf(TEXT("%s phase count"), *Name), Actual.Phases.Num(), ExpectedPhases.Num())) { continue; }
		for (int32 PhaseIndex = 0; PhaseIndex < ExpectedPhases.Num(); ++PhaseIndex)
		{
			const TSharedPtr<FJsonObject> ExpectedPhase = ExpectedPhases[PhaseIndex]->AsObject();
			const FString ExpectedId = ExpectedPhase->GetStringField(TEXT("id"));
			if (!TestEqual(*FString::Printf(TEXT("%s phase %d id"), *Name, PhaseIndex + 1), Actual.Phases[PhaseIndex].Id.ToString(), ExpectedId)) { break; }
			const TArray<FPlacement> ExpectedPlacements = ReadPlacements(ExpectedPhase->GetArrayField(TEXT("placements")));
			if (!TestEqual(*FString::Printf(TEXT("%s phase %s placement count"), *Name, *ExpectedId), Actual.PhasePlacements[PhaseIndex].Num(), ExpectedPlacements.Num())) { break; }
			for (int32 PlacementIndex = 0; PlacementIndex < ExpectedPlacements.Num(); ++PlacementIndex)
			{
				if (!NearlyEqualPlacement(Actual.PhasePlacements[PhaseIndex][PlacementIndex], ExpectedPlacements[PlacementIndex], Tolerance))
				{
					AddError(FString::Printf(TEXT("%s phase %s placement %d differs: actual=(%.6f, %.6f), JS=(%.6f, %.6f)"),
						*Name, *ExpectedId, PlacementIndex,
						Actual.PhasePlacements[PhaseIndex][PlacementIndex].Center.X, Actual.PhasePlacements[PhaseIndex][PlacementIndex].Center.Y,
						ExpectedPlacements[PlacementIndex].Center.X, ExpectedPlacements[PlacementIndex].Center.Y));
					break;
				}
			}
		}
	}
	return true;
}

#endif
