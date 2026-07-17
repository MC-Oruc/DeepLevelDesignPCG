// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingPCG.h"
#include "DeepLevelDesignPCGModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DeepLevelBuildingPCG)

// ---- DeepLevelPCGBuildingLineActor ----


#include "PCGComponent.h"

ADeepLevelPCGBuildingLineActor::ADeepLevelPCGBuildingLineActor()
{
	PrimaryActorTick.bCanEverTick = false;

	BuildingLine = CreateDefaultSubobject<UDeepLevelBuildingLineSplineComponent>(TEXT("BuildingLine"));
	SetRootComponent(BuildingLine);
	BuildingLine->SetClosedLoop(false);

	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
}

// ---- DeepLevelBuildingLineSplineComponent ----


#if WITH_EDITOR
void UDeepLevelBuildingLineSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName()
		!= GET_MEMBER_NAME_CHECKED(UDeepLevelBuildingLineSplineComponent, CornerPlacementMask))
	{
		return;
	}

	if (const AActor* Owner = GetOwner())
	{
		if (UPCGComponent* PCGComponent = Owner->FindComponentByClass<UPCGComponent>())
		{
			PCGComponent->NotifyPropertiesChangedFromBlueprint();
		}
	}
}
#endif

// ---- DeepLevelBuildingLinePCGSettings ----


#include "PCGContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSplineData.h"
#include "Metadata/PCGMetadata.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingLinePCGSettings"

namespace DeepLevelBuildingLinePCG
{
	class FElement final : public IPCGElement
	{
	protected:
		virtual bool ExecuteInternal(FPCGContext* Context) const override;
		virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
		virtual bool IsCacheable(const UPCGSettings* Settings) const override { return false; }
	};

	void ReportGenerationError(const FText& Message, const FPCGContext* Context)
	{
		PCGLog::LogErrorOnGraph(Message, Context);

#if WITH_EDITOR
		FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().Broadcast(
			LOCTEXT("BuildingLineSystemName", "Building Line"),
			Message);
#endif
	}

	const FDeepLevelBuildingPlacementDefinition* FindDefinition(
		const UDeepLevelBuildingPlacementCatalog& Catalog,
		const TSoftClassPtr<AActor>& BuildingClass)
	{
		return Catalog.Buildings.FindByPredicate([&BuildingClass](const FDeepLevelBuildingPlacementDefinition& Definition)
		{
			return Definition.BuildingClass.ToSoftObjectPath() == BuildingClass.ToSoftObjectPath();
		});
	}

}

#if WITH_EDITOR
FName UDeepLevelBuildingLinePCGSettings::GetDefaultNodeName() const
{
	return TEXT("DeepLevelBuildingLine");
}

FText UDeepLevelBuildingLinePCGSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "DeepLevel Building Line");
}

FText UDeepLevelBuildingLinePCGSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Packs catalog-defined Packed Level Actors along an open or closed spline. Buildings are placed on spline-right.");
}
#endif

TArray<FPCGPinProperties> UDeepLevelBuildingLinePCGSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& Input = Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Spline);
	Input.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UDeepLevelBuildingLinePCGSettings::OutputPinProperties() const
{
	return DefaultPointOutputPinProperties();
}

FPCGElementPtr UDeepLevelBuildingLinePCGSettings::CreateElement() const
{
	return MakeShared<DeepLevelBuildingLinePCG::FElement>();
}

bool DeepLevelBuildingLinePCG::FElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UDeepLevelBuildingLinePCGSettings* Settings = Context->GetInputSettings<UDeepLevelBuildingLinePCGSettings>();
	check(Settings);

	UDeepLevelBuildingPlacementCatalog* Catalog = Settings->Catalog.LoadSynchronous();
	if (!Catalog)
	{
		DeepLevelBuildingLinePCG::ReportGenerationError(LOCTEXT("MissingCatalog", "DeepLevel Building Line has no valid catalog."), Context);
		return true;
	}

	FText ValidationError;
	if (!Catalog->ValidateForGeneration(ValidationError))
	{
		DeepLevelBuildingLinePCG::ReportGenerationError(ValidationError, Context);
		return true;
	}

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGSplineData* Spline = Cast<const UPCGSplineData>(Input.Data);
		if (!Spline)
		{
			DeepLevelBuildingLinePCG::ReportGenerationError(LOCTEXT("InvalidSplineInput", "DeepLevel Building Line input is not spline data."), Context);
			continue;
		}

		FDeepLevelBuildingLinePath Path;
		if (!FDeepLevelBuildingLinePath::Build(*Spline, Path, ValidationError))
		{
			DeepLevelBuildingLinePCG::ReportGenerationError(ValidationError, Context);
			continue;
		}
		EDeepLevelCornerPlacementFlags CornerPlacement;
		if (!FDeepLevelBuildingLineCornerPolicy::Resolve(
			*Spline,
			Settings->CornerPlacementMask,
			CornerPlacement,
			ValidationError))
		{
			DeepLevelBuildingLinePCG::ReportGenerationError(ValidationError, Context);
			continue;
		}

		FDeepLevelBuildingLinePlan Plan;
		if (!FDeepLevelBuildingLinePlanner::BuildPlan(
			*Catalog,
			Path,
			Settings->RandomSeed,
			Settings->VarietyStrength,
			Settings->CornerPreference,
			CornerPlacement,
			Plan,
			ValidationError))
		{
			DeepLevelBuildingLinePCG::ReportGenerationError(ValidationError, Context);
			continue;
		}

		UPCGPointData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
		OutputData->InitializeFromData(Spline);
		FPCGMetadataAttribute<FSoftClassPath>* ActorClassAttribute = OutputData->MutableMetadata()->CreateAttribute<FSoftClassPath>(
			Settings->ActorClassAttribute,
			FSoftClassPath(),
			false,
			false);
		if (!ActorClassAttribute)
		{
			DeepLevelBuildingLinePCG::ReportGenerationError(LOCTEXT("ActorClassAttributeFailure", "Could not create the ActorClass output attribute."), Context);
			continue;
		}

		TArray<FPCGPoint>& Points = OutputData->GetMutablePoints();
		Points.Reserve(Plan.Placements.Num());
		for (const FDeepLevelBuildingLinePlacement& PlannedPlacement : Plan.Placements)
		{
			const FDeepLevelBuildingPlacementDefinition* Definition = DeepLevelBuildingLinePCG::FindDefinition(*Catalog, PlannedPlacement.BuildingClass);
			check(Definition);

			FPCGPoint& Point = Points.Emplace_GetRef();
			Point.Transform = FDeepLevelBuildingPlacementGeometry::BuildActorTransform(
				*Definition,
				PlannedPlacement.StreetFace,
				PlannedPlacement.PathSample.Location,
				PlannedPlacement.PathSample.Forward,
				PlannedPlacement.PathSample.Right);
			Point.Density = 1.0f;
			Point.Seed = HashCombineFast(Settings->RandomSeed, Points.Num() - 1);
			Point.MetadataEntry = OutputData->MutableMetadata()->AddEntry();
			ActorClassAttribute->SetValue(Point.MetadataEntry, FSoftClassPath(PlannedPlacement.BuildingClass.ToSoftObjectPath().ToString()));
		}

		Context->OutputData.TaggedData.Emplace_GetRef(Input).Data = OutputData;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingLineCornerPolicy ----


#define LOCTEXT_NAMESPACE "DeepLevelBuildingLineCornerPolicy"

namespace
{
	constexpr int32 ValidCornerPlacementMask = static_cast<int32>(EDeepLevelCornerPlacementFlags::All);

	bool ValidateMask(const int32 Mask, FText& OutError)
	{
		if (Mask < 0 || (Mask & ~ValidCornerPlacementMask) != 0)
		{
			OutError = FText::Format(
				LOCTEXT("InvalidCornerPlacementMask", "Corner placement mask must be between 0 and {0}."),
				FText::AsNumber(ValidCornerPlacementMask));
			return false;
		}
		return true;
	}
}

bool FDeepLevelBuildingLineCornerPolicy::Resolve(
	const UPCGSplineData& Spline,
	const int32 NodeMask,
	EDeepLevelCornerPlacementFlags& OutFlags,
	FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!ValidateMask(NodeMask, OutError))
	{
		return false;
	}

	int32 ResolvedMask = NodeMask;
	const UPCGMetadata* Metadata = Spline.ConstMetadata();
	if (Metadata && Metadata->GetConstAttribute(DeepLevelBuildingLineCornerPlacement::MetadataAttributeName))
	{
		const FPCGMetadataAttribute<int32>* Attribute = Metadata->GetConstTypedAttribute<int32>(
			DeepLevelBuildingLineCornerPlacement::MetadataAttributeName);
		if (!Attribute)
		{
			OutError = LOCTEXT(
				"InvalidCornerPlacementAttributeType",
				"Spline metadata attribute 'DeepLevelCornerPlacement' must be an integer bitmask.");
			return false;
		}
		ResolvedMask = Attribute->GetValue(PCGDefaultValueKey);
		if (!ValidateMask(ResolvedMask, OutError))
		{
			return false;
		}
	}

	OutFlags = static_cast<EDeepLevelCornerPlacementFlags>(ResolvedMask);
	return true;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingLinePath ----


#include "Data/PCGPolyLineData.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingLinePath"

namespace
{
	constexpr double DirectionSampleFractions[] = {0.0, 0.25, 0.5, 0.75, 1.0};

	bool ResolveHorizontalFrame(const FTransform& Transform, FVector& OutForward, FVector& OutRight)
	{
		OutForward = Transform.GetUnitAxis(EAxis::X);
		OutForward.Z = 0.0;
		if (!OutForward.Normalize())
		{
			return false;
		}

		OutRight = FVector::CrossProduct(FVector::UpVector, OutForward);
		return OutRight.Normalize();
	}

	double Cross2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const FVector2D AB = B - A;
		const FVector2D AC = C - A;
		return AB.X * AC.Y - AB.Y * AC.X;
	}

	bool IsPointOnSegment(const FVector2D& Point, const FVector2D& Start, const FVector2D& End)
	{
		constexpr double Tolerance = 0.01;
		return FMath::Abs(Cross2D(Start, End, Point)) <= Tolerance
			&& Point.X >= FMath::Min(Start.X, End.X) - Tolerance
			&& Point.X <= FMath::Max(Start.X, End.X) + Tolerance
			&& Point.Y >= FMath::Min(Start.Y, End.Y) - Tolerance
			&& Point.Y <= FMath::Max(Start.Y, End.Y) + Tolerance;
	}

	bool SegmentsIntersect(
		const FVector2D& AStart,
		const FVector2D& AEnd,
		const FVector2D& BStart,
		const FVector2D& BEnd)
	{
		const double ABStart = Cross2D(AStart, AEnd, BStart);
		const double ABEnd = Cross2D(AStart, AEnd, BEnd);
		const double BAStart = Cross2D(BStart, BEnd, AStart);
		const double BAEnd = Cross2D(BStart, BEnd, AEnd);
		if ((ABStart > 0.0 && ABEnd < 0.0 || ABStart < 0.0 && ABEnd > 0.0)
			&& (BAStart > 0.0 && BAEnd < 0.0 || BAStart < 0.0 && BAEnd > 0.0))
		{
			return true;
		}
		return IsPointOnSegment(BStart, AStart, AEnd)
			|| IsPointOnSegment(BEnd, AStart, AEnd)
			|| IsPointOnSegment(AStart, BStart, BEnd)
			|| IsPointOnSegment(AEnd, BStart, BEnd);
	}

	bool HasSelfIntersection(const UPCGPolyLineData& Data, const TArray<double>& SegmentLengths)
	{
		constexpr double SampleSpacing = 100.0;
		constexpr int32 MaximumSamplesPerSegment = 64;
		TArray<FVector2D> Samples;
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentLengths.Num(); ++SegmentIndex)
		{
			const int32 SampleCount = FMath::Clamp(
				FMath::CeilToInt32(SegmentLengths[SegmentIndex] / SampleSpacing),
				1,
				MaximumSamplesPerSegment);
			for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
			{
				const double Distance = SegmentLengths[SegmentIndex] * SampleIndex / SampleCount;
				Samples.Add(FVector2D(Data.GetTransformAtDistance(SegmentIndex, Distance).GetLocation()));
			}
		}
		const FVector2D FirstSample = Samples[0];
		Samples.Add(FirstSample);

		const int32 EdgeCount = Samples.Num() - 1;
		for (int32 AIndex = 0; AIndex < EdgeCount; ++AIndex)
		{
			for (int32 BIndex = AIndex + 1; BIndex < EdgeCount; ++BIndex)
			{
				const bool bAdjacent = BIndex == AIndex + 1
					|| (AIndex == 0 && BIndex == EdgeCount - 1);
				if (!bAdjacent && SegmentsIntersect(
					Samples[AIndex],
					Samples[AIndex + 1],
					Samples[BIndex],
					Samples[BIndex + 1]))
				{
					return true;
				}
			}
		}
		return false;
	}
}

bool FDeepLevelBuildingLinePath::Build(const UPCGPolyLineData& InData, FDeepLevelBuildingLinePath& OutPath, FText& OutError)
{
	OutPath = {};
	OutError = FText::GetEmpty();

	const int32 SegmentCount = InData.GetNumSegments();
	const bool bClosed = InData.IsClosed();
	const int32 MinimumVertexCount = bClosed ? 3 : 2;
	if (SegmentCount < 1 || InData.GetNumVertices() < MinimumVertexCount)
	{
		OutError = bClosed
			? LOCTEXT("InsufficientClosedSplinePoints", "A closed Building Line requires at least three spline control points.")
			: LOCTEXT("InsufficientSplinePoints", "Building Line requires at least two spline control points.");
		return false;
	}

	OutPath.Data = &InData;
	OutPath.bClosed = bClosed;
	OutPath.SegmentStartDistances.Reserve(SegmentCount);
	OutPath.SegmentLengths.Reserve(SegmentCount);
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const double SegmentLength = InData.GetSegmentLength(SegmentIndex);
		if (!FMath::IsFinite(SegmentLength) || SegmentLength <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutError = FText::Format(
				LOCTEXT("DegenerateSplineSegment", "Building Line spline segment {0} has zero or invalid length."),
				FText::AsNumber(SegmentIndex));
			return false;
		}

		for (const double Fraction : DirectionSampleFractions)
		{
			FVector Forward;
			FVector Right;
			if (!ResolveHorizontalFrame(InData.GetTransformAtDistance(SegmentIndex, SegmentLength * Fraction), Forward, Right))
			{
				OutError = FText::Format(
					LOCTEXT("VerticalSplineSegment", "Building Line spline segment {0} has no horizontal direction."),
					FText::AsNumber(SegmentIndex));
				return false;
			}
		}

		OutPath.SegmentStartDistances.Add(OutPath.TotalLength);
		OutPath.SegmentLengths.Add(SegmentLength);
		OutPath.TotalLength += SegmentLength;
	}
	if (bClosed && HasSelfIntersection(InData, OutPath.SegmentLengths))
	{
		OutPath = {};
		OutError = LOCTEXT(
			"SelfIntersectingClosedSpline",
			"A closed Building Line cannot intersect itself; split it into separate non-intersecting splines.");
		return false;
	}

	return true;
}

bool FDeepLevelBuildingLinePath::Sample(const double Distance, FDeepLevelBuildingLinePathSample& OutSample) const
{
	OutSample = {};
	if (!Data || SegmentLengths.IsEmpty() || !FMath::IsFinite(Distance))
	{
		return false;
	}

	double ResolvedDistance = FMath::Clamp(Distance, 0.0, TotalLength);
	if (bClosed)
	{
		ResolvedDistance = FMath::Fmod(Distance, TotalLength);
		if (ResolvedDistance < 0.0)
		{
			ResolvedDistance += TotalLength;
		}
	}
	int32 SegmentIndex = SegmentLengths.Num() - 1;
	for (int32 Index = 0; Index < SegmentLengths.Num() - 1; ++Index)
	{
		if (ResolvedDistance < SegmentStartDistances[Index + 1])
		{
			SegmentIndex = Index;
			break;
		}
	}

	const double LocalDistance = FMath::Clamp(
		ResolvedDistance - SegmentStartDistances[SegmentIndex],
		0.0,
		SegmentLengths[SegmentIndex]);
	const FTransform Transform = Data->GetTransformAtDistance(SegmentIndex, LocalDistance);
	OutSample.Location = Transform.GetLocation();
	return ResolveHorizontalFrame(Transform, OutSample.Forward, OutSample.Right);
}

double FDeepLevelBuildingLinePath::GetTurnAngleDegrees(const double StartDistance, const double EndDistance) const
{
	FDeepLevelBuildingLinePathSample StartSample;
	FDeepLevelBuildingLinePathSample EndSample;
	if (!Sample(StartDistance, StartSample) || !Sample(EndDistance, EndSample))
	{
		return 0.0;
	}

	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(StartSample.Forward, EndSample.Forward),
		-1.0,
		1.0)));
}

void FDeepLevelBuildingLinePath::GetHardCornerDistances(
	const double MinimumAngleDegrees,
	TArray<double>& OutDistances) const
{
	OutDistances.Reset();
	const int32 FirstCornerSegment = bClosed ? 0 : 1;
	for (int32 SegmentIndex = FirstCornerSegment; SegmentIndex < SegmentStartDistances.Num(); ++SegmentIndex)
	{
		const int32 PreviousSegmentIndex = SegmentIndex == 0 ? SegmentLengths.Num() - 1 : SegmentIndex - 1;
		const double SampleOffset = FMath::Max(
			FMath::Min(SegmentLengths[PreviousSegmentIndex], SegmentLengths[SegmentIndex]) * 1.e-4,
			1.0);
		const double CornerDistance = SegmentStartDistances[SegmentIndex];
		if (GetTurnAngleDegrees(CornerDistance - SampleOffset, CornerDistance + SampleOffset) >= MinimumAngleDegrees)
		{
			OutDistances.Add(CornerDistance);
		}
	}
}

bool FDeepLevelBuildingLinePath::IsStraight(const double AngleToleranceDegrees) const
{
	FDeepLevelBuildingLinePathSample Reference;
	if (!Sample(0.0, Reference))
	{
		return false;
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentLengths.Num(); ++SegmentIndex)
	{
		for (const double Fraction : DirectionSampleFractions)
		{
			FVector Forward;
			FVector Right;
			if (!ResolveHorizontalFrame(
				Data->GetTransformAtDistance(SegmentIndex, SegmentLengths[SegmentIndex] * Fraction),
				Forward,
				Right))
			{
				return false;
			}

			const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(Reference.Forward, Forward),
				-1.0,
				1.0)));
			if (Angle > AngleToleranceDegrees)
			{
				return false;
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

// ---- DeepLevelBuildingLinePCGDataInterop ----



#include "Algo/Transform.h"
#include "Helpers/PCGHelpers.h"

bool DeepLevelBuildingLinePCGDataInterop::GetDataFromComponent(
	FPCGContext* Context,
	const FPCGGetDataFunctionRegistryParams& Params,
	UActorComponent* Component,
	FPCGGetDataFunctionRegistryOutput& Output)
{
	UDeepLevelBuildingLineSplineComponent* BuildingLine = Cast<UDeepLevelBuildingLineSplineComponent>(Component);
	if (!BuildingLine || !(Params.DataTypeFilter & EPCGDataType::Spline))
	{
		return false;
	}
	if (Params.bIgnorePCGGeneratedComponents && BuildingLine->ComponentTags.Contains(PCGHelpers::DefaultPCGTag))
	{
		return true;
	}
	if (BuildingLine->GetNumberOfSplinePoints() <= 0)
	{
		return true;
	}

	UPCGSplineData* SplineData = FPCGContext::NewObject_AnyThread<UPCGSplineData>(Context);
	SplineData->Initialize(BuildingLine);
	SplineData->MutableMetadata()->CreateAttribute<int32>(
		DeepLevelBuildingLineCornerPlacement::MetadataAttributeName,
		BuildingLine->CornerPlacementMask,
		false,
		false);

	FPCGTaggedData& TaggedData = Output.Collection.TaggedData.Emplace_GetRef();
	TaggedData.Data = SplineData;
	auto NameTagToString = [](const FName& Tag) { return Tag.ToString(); };
	Algo::Transform(BuildingLine->ComponentTags, TaggedData.Tags, NameTagToString);
	if (Params.bAddActorTags && BuildingLine->GetOwner())
	{
		TSet<FString> ActorTags;
		Algo::Transform(BuildingLine->GetOwner()->Tags, ActorTags, NameTagToString);
		TaggedData.Tags.Append(ActorTags);
	}
	return true;
}
