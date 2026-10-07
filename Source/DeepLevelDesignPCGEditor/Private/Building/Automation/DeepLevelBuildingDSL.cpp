// Copyright <--\, Inc. All Rights Reserved.
#include "Building/Automation/DeepLevelBuildingDSL.h"
#include "Building/DeepLevelBuildingCatalogAuthoring.h"
#include "Building/DeepLevelBuildingLayoutAuthoring.h"
#include "Building/DeepLevelBuildingEditor.h"
#include "Automation/DeepLevelPCGRequest.h"
#include "PackedLevelActor/PackedLevelActor.h"
#include "Engine/World.h"
#include "PCGComponent.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DeepLevelBuildingDSL"
using namespace DeepLevelPCGRequest;

namespace DeepLevelBuildingDSLInternal
{
	FString RuleName(EDeepLevelStreetExposureRule Rule)
	{
		switch (Rule)
		{
		case EDeepLevelStreetExposureRule::Required: return TEXT("required");
		case EDeepLevelStreetExposureRule::Preferred: return TEXT("preferred");
		case EDeepLevelStreetExposureRule::Forbidden: return TEXT("forbidden");
		default: return TEXT("neutral");
		}
	}
	bool Exposure(const FJsonObject& Op, FDeepLevelBuildingFaceExposureRules& Rules, FString& Error)
	{
		if (!Op.HasField(TEXT("exposure"))) { return true; }
		const TSharedPtr<FJsonObject>* J;
		if (!Op.TryGetObjectField(TEXT("exposure"), J) || !DeepLevelPCGRequest::Fields(**J, {TEXT("positiveX"), TEXT("negativeX"), TEXT("positiveY"), TEXT("negativeY")}, Error))
		{ if (Error.IsEmpty()) { Error = TEXT("exposure must be an object."); } return false; }
		auto ReadRule = [&](const TCHAR* Key, EDeepLevelStreetExposureRule& Rule)
		{
			FString Name = RuleName(Rule);
			if (!DeepLevelPCGRequest::String(**J, Key, Name, Error)) { return false; }
			if (Name == TEXT("required")) { Rule = EDeepLevelStreetExposureRule::Required; }
			else if (Name == TEXT("preferred")) { Rule = EDeepLevelStreetExposureRule::Preferred; }
			else if (Name == TEXT("neutral")) { Rule = EDeepLevelStreetExposureRule::Neutral; }
			else if (Name == TEXT("forbidden")) { Rule = EDeepLevelStreetExposureRule::Forbidden; }
			else { Error = TEXT("Unknown exposure rule: ") + Name; return false; }
			return true;
		};
		return ReadRule(TEXT("positiveX"), Rules.PositiveX) && ReadRule(TEXT("negativeX"), Rules.NegativeX)
			&& ReadRule(TEXT("positiveY"), Rules.PositiveY) && ReadRule(TEXT("negativeY"), Rules.NegativeY);
	}
	TSharedRef<FJsonObject> DefinitionState(const FDeepLevelBuildingPlacementDefinition& D)
	{
		auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("class"), D.BuildingClass.ToString());
		J->SetArrayField(TEXT("center"), DeepLevelPCGRequest::VectorJson(D.PlacementVolume.Center));
		const auto R = D.PlacementVolume.Rotation;
		J->SetArrayField(TEXT("rotation"), DeepLevelPCGRequest::VectorJson(FVector(R.Pitch, R.Yaw, R.Roll))); J->SetArrayField(TEXT("extent"), DeepLevelPCGRequest::VectorJson(D.PlacementVolume.Extent));
		J->SetNumberField(TEXT("weight"), D.SelectionWeight); J->SetBoolField(TEXT("calibrated"), D.bCalibrated);
		auto E = MakeShared<FJsonObject>(); const auto& Rules = D.PlacementVolume.Exposure;
		E->SetStringField(TEXT("positiveX"), RuleName(Rules.PositiveX)); E->SetStringField(TEXT("negativeX"), RuleName(Rules.NegativeX));
		E->SetStringField(TEXT("positiveY"), RuleName(Rules.PositiveY)); E->SetStringField(TEXT("negativeY"), RuleName(Rules.NegativeY));
		J->SetObjectField(TEXT("exposure"), E); return J;
	}
	bool CatalogPlan(UDeepLevelBuildingPlacementCatalog& Catalog, const TSharedRef<FJsonObject>& Root,
		const TArray<TSharedPtr<FJsonValue>>& Ops, const FOptions& Options, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
	{
		auto Staged = TStrongObjectPtr<UDeepLevelBuildingPlacementCatalog>(NewObject<UDeepLevelBuildingPlacementCatalog>());
		Staged->Buildings = Catalog.Buildings; Staged->Presets = Catalog.Presets;
		FString Error;
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			Out.Report->SetNumberField(TEXT("operation"), Index);
			const auto Op = DeepLevelPCGRequest::Object(*Ops[Index]); FString Name;
			if (!Op || !DeepLevelPCGRequest::String(*Op, TEXT("op"), Name, Error, true)) { Error = TEXT("Operation must have op."); break; }
			if (Name.StartsWith(TEXT("building.")))
			{
				FString Class;
				if (!DeepLevelPCGRequest::String(*Op, TEXT("class"), Class, Error, true)) { break; }
				int32 Item = Staged->Buildings.IndexOfByPredicate([&](const auto& D) { return D.BuildingClass.ToString() == Class; });
				if (Name == TEXT("building.add"))
				{
					if (Item != INDEX_NONE) { Error = TEXT("Building class already belongs to this catalog."); break; }
					Item = Staged->Buildings.AddDefaulted(); Staged->Buildings[Item].BuildingClass = TSoftClassPtr<AActor>(FSoftObjectPath(Class));
				}
				else if (Item == INDEX_NONE) { Error = TEXT("Building class is not in this catalog."); break; }
				if (Name == TEXT("building.remove"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("class")}, Error)) { break; }
					Staged->Buildings.RemoveAt(Item); continue;
				}
				auto& D = Staged->Buildings[Item];
				UClass* Loaded = D.BuildingClass.LoadSynchronous();
				if (!Loaded || !Loaded->IsChildOf(APackedLevelActor::StaticClass())) { Error = TEXT("Building requires a PackedLevelActor class path."); break; }
				if (Name == TEXT("building.autoFit"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("class")}, Error)) { break; }
					if (!DeepLevelBuildingCatalogAuthoring::AutoFit(Loaded, D.PlacementVolume.Rotation, D.PlacementVolume.Center, D.PlacementVolume.Extent))
					{ Error = TEXT("Building AutoFit found no registered primitive bounds."); break; }
					D.bCalibrated = true; continue;
				}
				if (Name != TEXT("building.add") && Name != TEXT("building.update")) { Error = TEXT("Unknown building catalog operation."); break; }
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("class"), TEXT("center"), TEXT("rotation"), TEXT("extent"), TEXT("exposure"), TEXT("weight"), TEXT("calibrated")}, Error)) { break; }
				FVector Rotation(D.PlacementVolume.Rotation.Pitch, D.PlacementVolume.Rotation.Yaw, D.PlacementVolume.Rotation.Roll);
				if (!DeepLevelPCGRequest::Vector(*Op, TEXT("center"), D.PlacementVolume.Center, Error) || !DeepLevelPCGRequest::Vector(*Op, TEXT("rotation"), Rotation, Error)
					|| !DeepLevelPCGRequest::Vector(*Op, TEXT("extent"), D.PlacementVolume.Extent, Error) || !Exposure(*Op, D.PlacementVolume.Exposure, Error)
					|| !DeepLevelPCGRequest::Number(*Op, TEXT("weight"), D.SelectionWeight, 0, 1e9, Error) || !DeepLevelPCGRequest::Bool(*Op, TEXT("calibrated"), D.bCalibrated, Error)) { break; }
				D.PlacementVolume.Rotation = FRotator(Rotation.X, Rotation.Y, Rotation.Z);
				if (D.PlacementVolume.Extent.GetMin() < 1) { Error = TEXT("Building half extents must be >=1cm."); break; }
				continue;
			}
			if (Name.StartsWith(TEXT("preset.")))
			{
				FString Preset;
				if (!DeepLevelPCGRequest::String(*Op, TEXT("preset"), Preset, Error, true)) { break; }
				int32 Item = Staged->Presets.IndexOfByPredicate([&](const auto& P) { return P.Name == FName(*Preset); });
				if (Name == TEXT("preset.add"))
				{
					if (Item != INDEX_NONE || FName(*Preset).IsNone()) { Error = TEXT("Preset requires a unique non-None name."); break; }
					Item = Staged->Presets.AddDefaulted(); Staged->Presets[Item].Name = FName(*Preset);
				}
				else if (Item == INDEX_NONE) { Error = TEXT("Preset is not in this catalog."); break; }
				if (Name == TEXT("preset.remove"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("preset")}, Error)) { break; }
					Staged->Presets.RemoveAt(Item); continue;
				}
				if (Name == TEXT("preset.duplicate"))
				{
					FString NewName;
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("preset"), TEXT("name")}, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("name"), NewName, Error, true)) { break; }
					auto Copy = Staged->Presets[Item]; Copy.Name = FName(*NewName); Staged->Presets.Add(Copy); continue;
				}
				if (Name != TEXT("preset.add") && Name != TEXT("preset.update")) { Error = TEXT("Unknown preset operation."); break; }
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("preset"), TEXT("name"), TEXT("weight"), TEXT("buildings")}, Error)) { break; }
				auto& P = Staged->Presets[Item]; FString NewName = P.Name.ToString();
				if (!DeepLevelPCGRequest::String(*Op, TEXT("name"), NewName, Error) || !DeepLevelPCGRequest::Number(*Op, TEXT("weight"), P.SelectionWeight, 0, 1e9, Error)) { break; }
				P.Name = FName(*NewName);
				if (Op->HasField(TEXT("buildings")))
				{
					const TArray<TSharedPtr<FJsonValue>>* Sequence;
					if (!Op->TryGetArrayField(TEXT("buildings"), Sequence) || Sequence->Num() > 4096) { Error = TEXT("Preset buildings requires an ordered array of up to 4096 class paths."); break; }
					P.Buildings.Reset();
					for (const auto& Value : *Sequence)
					{
						FString Class;
						if (!Value->TryGetString(Class) || Class.IsEmpty()) { Error = TEXT("Preset class paths must be nonempty strings."); break; }
						P.Buildings.Add(TSoftClassPtr<AActor>(FSoftObjectPath(Class)));
					}
					if (!Error.IsEmpty()) { break; }
				}
				continue;
			}
			Error = TEXT("Unsupported Building catalog operation: ") + Name; break;
		}
		TSet<FName> Names;
		for (const auto& P : Staged->Presets)
		{
			if (P.Name.IsNone() || Names.Contains(P.Name)) { Error = TEXT("Preset names must be unique and non-None."); break; }
			Names.Add(P.Name);
			for (const auto& Class : P.Buildings)
			{
				if (!Staged->Buildings.ContainsByPredicate([&](const auto& D) { return D.BuildingClass == Class; })) { Error = TEXT("Preset references a building outside the staged catalog; edit the preset before removing its building."); break; }
			}
		}
		if (!Error.IsEmpty()) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		Out.Report->RemoveField(TEXT("operation"));
		if (Root->HasField(TEXT("preview")) && !Options.bCapture) { Out.Report->SetStringField(TEXT("error"), TEXT("preview requires capture=true.")); return false; }
		int32 PreviewBuilding = INDEX_NONE, PreviewPreset = INDEX_NONE, PreviewSeed = 1337;
		if (Root->HasField(TEXT("preview")))
		{
			const TSharedPtr<FJsonObject>* Preview;
			FString Class, Preset;
			if (!Root->TryGetObjectField(TEXT("preview"), Preview)) { Error = TEXT("preview must be an object."); }
			else if (!DeepLevelPCGRequest::Fields(**Preview, {TEXT("class"), TEXT("preset"), TEXT("seed")}, Error)
				|| !DeepLevelPCGRequest::String(**Preview, TEXT("class"), Class, Error) || !DeepLevelPCGRequest::String(**Preview, TEXT("preset"), Preset, Error)
				|| !DeepLevelPCGRequest::Integer(**Preview, TEXT("seed"), PreviewSeed, MIN_int32, MAX_int32, Error)) {}
			else
			{
				if (Class.IsEmpty() == Preset.IsEmpty()) { Error = TEXT("preview requires exactly one class or preset."); }
				PreviewBuilding = Staged->Buildings.IndexOfByPredicate([&](const auto& D) { return D.BuildingClass.ToString() == Class; });
				PreviewPreset = Staged->Presets.IndexOfByPredicate([&](const auto& P) { return P.Name == FName(*Preset); });
				if (PreviewBuilding == INDEX_NONE && PreviewPreset == INDEX_NONE) { Error = TEXT("Preview item does not belong to the staged catalog."); }
			}
		}
		else if (Options.bCapture) { Error = TEXT("Catalog capture requires preview.class or preview.preset."); }
		if (!Error.IsEmpty()) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		Plan.Target.Reset(&Catalog);
		Plan.Apply = [Target = &Catalog, Staged, Options, bChanged = !Ops.IsEmpty()](FDeepLevelPCGAutomationResult& R)
		{
			if (Options.bDryRun || !bChanged) { return; }
			DeepLevelBuildingCatalogAuthoring::Edit(*Target, LOCTEXT("CatalogBatch", "Edit Building Catalog"), [&]
			{ Target->Buildings = Staged->Buildings; Target->Presets = Staged->Presets; });
			R.Report->SetBoolField(TEXT("applied"), true);
		};
		Plan.Finish = [Target = &Catalog, Staged, Options, PreviewBuilding, PreviewPreset, PreviewSeed](FDeepLevelPCGAutomationResult& R)
		{
			FText Validation; R.Report->SetBoolField(TEXT("validForGeneration"), Staged->ValidateForGeneration(Validation)); R.Report->SetStringField(TEXT("validation"), Validation.ToString());
			if (Options.bIncludeState)
			{
				TArray<TSharedPtr<FJsonValue>> Buildings, Presets;
				for (const auto& D : Staged->Buildings) { Buildings.Add(MakeShared<FJsonValueObject>(DefinitionState(D))); }
				for (const auto& P : Staged->Presets)
				{
					auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("preset"), P.Name.ToString()); J->SetNumberField(TEXT("weight"), P.SelectionWeight);
					TArray<TSharedPtr<FJsonValue>> Classes; for (const auto& C : P.Buildings) { Classes.Add(MakeShared<FJsonValueString>(C.ToString())); }
					J->SetArrayField(TEXT("buildings"), Classes); Presets.Add(MakeShared<FJsonValueObject>(J));
				}
				R.Report->SetArrayField(TEXT("buildings"), Buildings); R.Report->SetArrayField(TEXT("presets"), Presets);
			}
			if (Options.bSave) { FString E; const bool bSaved = DeepLevelPCGRequest::Save(*Target, E); R.Report->SetBoolField(TEXT("saved"), bSaved); if (!bSaved) { R.Report->SetStringField(TEXT("saveError"), E); } }
			if (Options.bCapture)
			{
				const auto View = SNew(SDeepLevelBuildingCatalogPreviewViewport);
				if (PreviewBuilding != INDEX_NONE) { View->PreviewBuilding(*Staged, PreviewBuilding); }
				else { View->PreviewPreset(*Staged, PreviewPreset, PreviewSeed); }
				DeepLevelPCGRequest::CapturePreview(View, Options, R);
			}
		};
		return true;
	}

	bool LayoutPlan(AActor& Actor, const TSharedRef<FJsonObject>& Root, const TArray<TSharedPtr<FJsonValue>>& Ops,
		const FOptions& Options, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
	{
		using namespace DeepLevelBuildingLayoutAuthoring;
		auto* Line = Cast<ADeepLevelPCGBuildingLineActor>(&Actor);
		auto* Side = Cast<ADeepLevelPCGRoadsideBuildingActor>(&Actor);
		UPCGComponent* Component = Line ? Line->PCGComponent.Get() : Side->PCGComponent.Get();
		auto State = MakeShared<FState>(Read(Actor));
		FString Error; bool bGenerate = false, bAlign = false, bRebuild = false, bControl = false;
		if (!DeepLevelPCGRequest::Bool(*Root, TEXT("generate"), bGenerate, Error) || !DeepLevelPCGRequest::Bool(*Root, TEXT("align"), bAlign, Error)) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		if (bAlign && !Side) { Out.Report->SetStringField(TEXT("error"), TEXT("align is Roadside-only.")); return false; }
		TMap<FString, FGuid> Aliases;
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			Out.Report->SetNumberField(TEXT("operation"), Index);
			const auto Op = DeepLevelPCGRequest::Object(*Ops[Index]); FString Name;
			if (!Op || !DeepLevelPCGRequest::String(*Op, TEXT("op"), Name, Error, true)) { Error = TEXT("Operation must have op."); break; }
			if (Name == TEXT("generation.cancel") || Name == TEXT("generation.cleanup"))
			{
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op")}, Error)) { break; }
				if (Ops.Num() != 1 || bGenerate || bAlign || Options.bDryRun || Options.bCapture || Options.bSave) { Error = TEXT("Control operations must be standalone without save/capture/dryRun/generate/align."); break; }
				bControl = true;
				Plan.Apply = [Weak = TWeakObjectPtr<UPCGComponent>(Component), Name](FDeepLevelPCGAutomationResult& R)
				{
					if (!Weak.IsValid()) { R.bSuccess = false; R.Report->SetStringField(TEXT("error"), TEXT("Missing PCG component.")); return; }
					if (Name == TEXT("generation.cancel")) { Weak->CancelGeneration(); } else { Weak->CleanupLocalImmediate(true); }
					R.Report->SetBoolField(TEXT("applied"), true);
				};
				continue;
			}
			if (Name == TEXT("layout.configure"))
			{
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("city"), TEXT("catalog"), TEXT("seed"), TEXT("variety"), TEXT("cornerPreference"), TEXT("cornerMask"),
					TEXT("setback"), TEXT("depthTolerance"), TEXT("boundaryMargin")}, Error)) { break; }
				if (Line && (Op->HasField(TEXT("setback")) || Op->HasField(TEXT("depthTolerance")) || Op->HasField(TEXT("boundaryMargin")))) { Error = TEXT("Frontage settings are Roadside-only."); break; }
				FString City = State->City.IsValid() ? State->City->GetPathName() : FString(), Catalog = State->Catalog.ToString();
				if (!DeepLevelPCGRequest::String(*Op, TEXT("city"), City, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("catalog"), Catalog, Error)
					|| !DeepLevelPCGRequest::Integer(*Op, TEXT("seed"), State->Seed, MIN_int32, MAX_int32, Error) || !DeepLevelPCGRequest::Integer(*Op, TEXT("cornerMask"), State->CornerMask, 0, 3, Error)
					|| !DeepLevelPCGRequest::Number(*Op, TEXT("variety"), State->Variety, 0, 1, Error) || !DeepLevelPCGRequest::Number(*Op, TEXT("cornerPreference"), State->CornerPreference, 0, 1, Error)
					|| !DeepLevelPCGRequest::Number(*Op, TEXT("setback"), State->Setback, -1e6, 1e6, Error) || !DeepLevelPCGRequest::Number(*Op, TEXT("depthTolerance"), State->DepthTolerance, 0, 1e6, Error)
					|| !DeepLevelPCGRequest::Number(*Op, TEXT("boundaryMargin"), State->BoundaryMargin, 0, 1e6, Error)) { break; }
				State->City = FindObject<ADeepLevelCityLayoutActor>(nullptr, *City); State->Catalog = TSoftObjectPtr<UDeepLevelBuildingPlacementCatalog>(FSoftObjectPath(Catalog));
				if (!State->City.IsValid() || State->City->GetWorld() != Actor.GetWorld() || !State->Catalog.LoadSynchronous()) { Error = TEXT("Invalid City or Building catalog."); break; }
				continue;
			}
			if (Name == TEXT("line.update"))
			{
				if (!Line) { Error = TEXT("line.update requires a BuildingLine actor."); break; }
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("points"), TEXT("closed")}, Error) || !DeepLevelPCGRequest::Points(*Op, State->LinePoints, Error)
					|| !DeepLevelPCGRequest::Bool(*Op, TEXT("closed"), State->bClosed, Error)) { break; }
				if (State->bClosed && State->LinePoints.Num() < 3) { Error = TEXT("Closed line requires at least three points."); break; }
				State->bLineChanged = true;
				continue;
			}
			if (Name == TEXT("frontage.generate"))
			{
				if (!Side || Index != Ops.Num()-1 || !DeepLevelPCGRequest::Fields(*Op, {TEXT("op")}, Error)) { if (Error.IsEmpty()) { Error = TEXT("frontage.generate must be the last Roadside authoring command; inspect new IDs in its response."); } break; }
				bRebuild = true; continue;
			}
			if (Name.StartsWith(TEXT("frontage.")))
			{
				if (!Side) { Error = TEXT("Frontages require a Roadside actor."); break; }
				FString Id, Alias;
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("frontage"), TEXT("points"), TEXT("closed"), TEXT("excluded"), TEXT("as")}, Error)
					|| !DeepLevelPCGRequest::String(*Op, TEXT("frontage"), Id, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("as"), Alias, Error)) { break; }
				FGuid Guid;
				if (Id.StartsWith(TEXT("$"))) { const auto* Found = Aliases.Find(Id.Mid(1)); if (!Found) { Error = TEXT("Unknown frontage alias."); break; } Guid = *Found; }
				else if (!Id.IsEmpty() && !FGuid::Parse(Id, Guid)) { Error = TEXT("Frontage identity must be a GUID."); break; }
				int32 Item = State->Frontages.IndexOfByPredicate([&](const FFrontage& F) { return F.Id == Guid; });
				if (Name == TEXT("frontage.add") || Name == TEXT("frontage.replace"))
				{
					FFrontage Value;
					if (Name == TEXT("frontage.replace"))
					{
						if (Item == INDEX_NONE || State->Frontages[Item].Kind != EDeepLevelRoadsideFrontageKind::Automatic)
						{ Error = TEXT("replace requires an existing automatic frontage GUID."); break; }
						Value = State->Frontages[Item]; Value.ReplacedId = Guid; Value.Source.Reset(); Value.Kind = EDeepLevelRoadsideFrontageKind::Replace; Value.bExcluded = false;
						if (State->Frontages.ContainsByPredicate([Guid](const FFrontage& F) { return F.ReplacedId == Guid; })) { Error = TEXT("Automatic frontage already has a replacement."); break; }
					}
					else if (!Id.IsEmpty()) { Error = TEXT("frontage.add does not accept an existing frontage ID."); break; }
					if (!Alias.IsEmpty() && (Aliases.Contains(Alias) || Alias.StartsWith(TEXT("$")))) { Error = TEXT("Alias must be unique and omit $."); break; }
					Value.Id = FGuid::NewGuid(); Item = State->Frontages.Add(Value);
					if (!Alias.IsEmpty()) { Aliases.Add(Alias, Value.Id); }
				}
				else if (Item == INDEX_NONE) { Error = TEXT("Frontage does not belong to this actor."); break; }
				auto& F = State->Frontages[Item];
				if (Name == TEXT("frontage.exclude"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("frontage"), TEXT("excluded")}, Error)) { break; }
					if (F.Kind != EDeepLevelRoadsideFrontageKind::Automatic || !Op->HasField(TEXT("excluded")) || !DeepLevelPCGRequest::Bool(*Op, TEXT("excluded"), F.bExcluded, Error))
					{ if (Error.IsEmpty()) { Error = TEXT("exclude requires an automatic frontage and excluded boolean."); } break; }
					bRebuild = true; continue;
				}
				if (F.Kind == EDeepLevelRoadsideFrontageKind::Automatic) { Error = TEXT("Automatic geometry is read-only; create a replacement or exclude it."); break; }
				if (Name == TEXT("frontage.remove"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("frontage")}, Error)) { break; }
					State->Frontages.RemoveAt(Item); bRebuild = true; continue;
				}
				if (Name != TEXT("frontage.add") && Name != TEXT("frontage.replace") && Name != TEXT("frontage.update")) { Error = TEXT("Unknown frontage operation."); break; }
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("frontage"), TEXT("points"), TEXT("closed"), TEXT("as")}, Error)) { break; }
				if (Name == TEXT("frontage.update") && Op->HasField(TEXT("as"))) { Error = TEXT("as is supported on add/replace only."); break; }
				if (F.Points.IsEmpty() && !Op->HasField(TEXT("points"))) { Error = TEXT("frontage.add requires points."); break; }
				if ((Op->HasField(TEXT("points")) && !DeepLevelPCGRequest::Points(*Op, F.Points, Error)) || !DeepLevelPCGRequest::Bool(*Op, TEXT("closed"), F.bClosed, Error)) { break; }
				if (F.bClosed && F.Points.Num() < 3) { Error = TEXT("Closed frontage requires three points."); break; }
				F.bGeometryChanged |= Op->HasField(TEXT("points")) || Op->HasField(TEXT("closed"));
				bRebuild = true; continue;
			}
			Error = TEXT("Unsupported Building layout operation: ") + Name; break;
		}
		if (!Error.IsEmpty()) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		Out.Report->RemoveField(TEXT("operation"));
		const bool bChanged = !Ops.IsEmpty() && !bControl;
		if (Options.bDryRun && Options.bCapture) { Out.Report->SetStringField(TEXT("error"), TEXT("Actor dryRun cannot capture staged generation.")); return false; }
		if (Options.bCapture && bChanged && !bGenerate) { Out.Report->SetStringField(TEXT("error"), TEXT("Capturing edited building output requires generate=true.")); return false; }
		if (bAlign && !bGenerate && (bChanged || !Side->bOutputCurrent)) { Out.Report->SetStringField(TEXT("error"), TEXT("align requires current verified output or generate=true.")); return false; }
		if (!Component || ((bChanged || bGenerate || bAlign || (bControl && Ops[0]->AsObject()->GetStringField(TEXT("op")) == TEXT("generation.cleanup"))) && Component->IsGenerating()))
		{ Out.Report->SetStringField(TEXT("error"), TEXT("Missing or busy PCG component; inspect or cancel first.")); return false; }
		if (bGenerate || bRebuild)
		{
			FText Validation; FDeepLevelCityGrid Grid;
			if (!State->City.IsValid() || !State->City->ResolveGrid(Grid, Validation) || (bGenerate && (!Component->GetGraph()
				|| !State->Catalog.LoadSynchronous() || !State->Catalog.Get()->ValidateForGeneration(Validation))))
			{ Out.Report->SetStringField(TEXT("error"), TEXT("Requires City grid and, for generation, graph/calibrated catalog: ") + Validation.ToString()); return false; }
		}
		Plan.Target.Reset(&Actor); Plan.Component = Component; Plan.bRequiresAsync = bGenerate && !Options.bDryRun; Plan.TimeoutSeconds = Options.TimeoutSeconds;
		if (!bControl)
		{
			Plan.Apply = [Target = &Actor, State, bChanged, bRebuild, bGenerate, Side, Options, Aliases](FDeepLevelPCGAutomationResult& R)
			{
				if (bChanged && !Options.bDryRun)
				{
					const FScopedTransaction Transaction(LOCTEXT("LayoutBatch", "Edit Building Layout"));
					Apply(*Target, *State, bRebuild && !bGenerate); R.Report->SetBoolField(TEXT("applied"), true);
					if (Side && bRebuild && !bGenerate && !Side->LastGenerationError.IsEmpty()) { R.bSuccess = false; R.Report->SetStringField(TEXT("generationError"), Side->LastGenerationError.ToString()); }
				}
				auto Ids = MakeShared<FJsonObject>(); for (const auto& Pair : Aliases) { Ids->SetStringField(Pair.Key, Pair.Value.ToString(EGuidFormats::DigitsWithHyphens)); }
				R.Report->SetObjectField(TEXT("ids"), Ids);
			};
		}
		if (bGenerate && !Options.bDryRun) { Plan.Generate = [Line, Side]() { if (Line) { Line->GenerateBuildings(); } else { Side->GenerateBuildings(); } }; }
		Plan.Finish = [Target = &Actor, Line, Side, Component, State, Options, bGenerate, bAlign](FDeepLevelPCGAutomationResult& R)
		{
			if (bGenerate && !Options.bDryRun && R.bSuccess && !(Line ? Line->bOutputCurrent : Side->bOutputCurrent))
			{ R.bSuccess = false; R.Report->SetBoolField(TEXT("generated"), false); R.Report->SetStringField(TEXT("generationError"), TEXT("PCG output failed owner verification.")); }
			const FText& GenerationError = Line ? Line->LastGenerationError : Side->LastGenerationError;
			R.Report->SetStringField(TEXT("lastGenerationError"), GenerationError.ToString());
			if (bAlign && !Options.bDryRun && R.bSuccess)
			{
				const FScopedTransaction Transaction(LOCTEXT("FinalAlignment", "Align Building Volumes")); Side->AlignBuildingsVolFinal();
				const bool bAligned = Side->LastGenerationError.IsEmpty() && Side->bOutputCurrent;
				R.Report->SetBoolField(TEXT("aligned"), bAligned);
				if (!bAligned) { R.bSuccess = false; R.Report->SetStringField(TEXT("alignmentError"), Side->LastGenerationError.ToString()); }
			}
			R.Report->SetBoolField(TEXT("generating"), Component->IsGenerating()); R.Report->SetBoolField(TEXT("outputCurrent"), Line ? Line->bOutputCurrent : Side->bOutputCurrent);
			R.Report->SetNumberField(TEXT("rejectedPlacements"), Line ? Line->RejectedPlacementCount : Side->RejectedPlacementCount);
			if (Options.bIncludeState)
			{
				const FState Current = Options.bDryRun ? *State : Read(*Target);
				R.Report->SetStringField(TEXT("city"), Current.City.IsValid() ? Current.City->GetPathName() : FString()); R.Report->SetStringField(TEXT("catalog"), Current.Catalog.ToString());
				R.Report->SetNumberField(TEXT("seed"), Current.Seed); R.Report->SetNumberField(TEXT("variety"), Current.Variety); R.Report->SetNumberField(TEXT("cornerPreference"), Current.CornerPreference);
				R.Report->SetNumberField(TEXT("cornerMask"), Current.CornerMask);
				if (Line)
				{
					TArray<TSharedPtr<FJsonValue>> Points; for (const auto& P : Current.LinePoints) { Points.Add(MakeShared<FJsonValueArray>(DeepLevelPCGRequest::VectorJson(P))); }
					R.Report->SetArrayField(TEXT("points"), Points); R.Report->SetBoolField(TEXT("closed"), Current.bClosed);
				}
				else
				{
					R.Report->SetNumberField(TEXT("setback"), Current.Setback); R.Report->SetNumberField(TEXT("depthTolerance"), Current.DepthTolerance); R.Report->SetNumberField(TEXT("boundaryMargin"), Current.BoundaryMargin);
					R.Report->SetNumberField(TEXT("buildingPlacements"), Side->BuildingPlacementCount); R.Report->SetNumberField(TEXT("alignmentMovedPlacements"), Side->FinalClosureMovedPlacementCount);
					TArray<TSharedPtr<FJsonValue>> Frontages;
					for (const auto& F : Current.Frontages)
					{
						auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), F.Id.ToString(EGuidFormats::DigitsWithHyphens)); J->SetStringField(TEXT("replacedId"), F.ReplacedId.ToString(EGuidFormats::DigitsWithHyphens));
						J->SetStringField(TEXT("kind"), F.Kind == EDeepLevelRoadsideFrontageKind::Automatic ? TEXT("automatic") : F.Kind == EDeepLevelRoadsideFrontageKind::Replace ? TEXT("replace") : TEXT("add"));
						J->SetBoolField(TEXT("excluded"), F.bExcluded); J->SetBoolField(TEXT("closed"), F.bClosed);
						TArray<TSharedPtr<FJsonValue>> Points; for (const auto& P : F.Points) { Points.Add(MakeShared<FJsonValueArray>(DeepLevelPCGRequest::VectorJson(P))); }
						J->SetArrayField(TEXT("points"), Points); Frontages.Add(MakeShared<FJsonValueObject>(J));
					}
					R.Report->SetArrayField(TEXT("frontages"), Frontages);
				}
				// Published placement records are obtained through the owner's City fragment contract.
				const auto* Provider = Cast<IDeepLevelCityLayoutProvider>(Target);
				FDeepLevelCityGrid Grid; FDeepLevelCityLayoutFragment Fragment; FText Error;
				if (Current.City.IsValid() && Current.City->ResolveGrid(Grid, Error) && Provider && Provider->BuildCityLayoutFragment(Grid, Fragment, Error))
				{
					TArray<TSharedPtr<FJsonValue>> Buildings;
					for (const auto& B : Fragment.Buildings)
					{
						auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), B.StableId.ToString(EGuidFormats::DigitsWithHyphens)); J->SetStringField(TEXT("class"), B.BuildingClass.ToString()); J->SetArrayField(TEXT("location"), DeepLevelPCGRequest::VectorJson(B.Transform.GetLocation()));
						J->SetArrayField(TEXT("scale"), DeepLevelPCGRequest::VectorJson(B.Transform.GetScale3D())); const auto Rot = B.Transform.Rotator();
						J->SetArrayField(TEXT("rotation"), DeepLevelPCGRequest::VectorJson(FVector(Rot.Pitch, Rot.Yaw, Rot.Roll))); Buildings.Add(MakeShared<FJsonValueObject>(J));
					}
					R.Report->SetArrayField(TEXT("placements"), Buildings);
				}
			}
			if (R.bSuccess && Options.bSave) { FString E; const bool bSaved = DeepLevelPCGRequest::Save(*Target, E); R.Report->SetBoolField(TEXT("saved"), bSaved); if (!bSaved) { R.Report->SetStringField(TEXT("saveError"), E); } }
			if (R.bSuccess && Options.bCapture) { DeepLevelPCGRequest::CaptureWorld(*Target, *Component, Options, R); }
		};
		return true;
	}
}
FString DeepLevelBuildingDSL::Describe()
{
	return TEXT(R"JSON({"target":"loaded editor BuildingLine/Roadside actor path OR BuildingPlacementCatalog asset path","operations":{"layout.configure":["city","catalog","seed","variety: 0..1","cornerPreference: 0..1","cornerMask: 0..3","setback (roadside)","depthTolerance (roadside)","boundaryMargin (roadside)"],"line.update":["points","closed?"],"frontage.generate":[],"frontage.add":["points","closed?","as?"],"frontage.replace":["frontage GUID","points?","closed?","as?"],"frontage.update":["frontage GUID or $alias","points?","closed?"],"frontage.exclude":["frontage GUID","excluded"],"frontage.remove":["manual frontage GUID"],"building.add":["class (PackedLevelActor)","center","rotation","extent","exposure","weight","calibrated"],"building.update":["class","same fields as add"],"building.autoFit":["class"],"building.remove":["class"],"preset.add":["preset (unique name)","weight","buildings (ordered class array)"],"preset.update":["preset","name?","weight?","buildings? (replaces/reorders sequence)"],"preset.duplicate":["preset","name"],"preset.remove":["preset"],"generation.cancel":[],"generation.cleanup":[]},"generate":"actor-only boolean; waits for verified PCG output; Roadside owner regenerates frontages before buildings","align":"Roadside-only boolean; after generation or on current verified output; separate Undo transaction","preview":"catalog only: {class:path} OR {preset:name,seed:1337}","coordinates":"world centimeters for line/frontages; catalog OBB center/extent class-local, half extents >=1cm, rotations [pitch,yaw,roll]","exposure":"object with positiveX/negativeX/positiveY/negativeY: required|preferred|neutral|forbidden","frontages":"automatic geometry read-only; manual add/replace and exclusion; frontage.generate must be last authoring operation","catalog":"partial authoring allowed; dangling preset references rejected; validForGeneration reports readiness","control":"cancel/cleanup standalone; cleanup refuses busy generation","capture":"isolated catalog preview or fresh editor-world viewport; no PIE or user camera changes","dryRun":"validates authoring, no PCG/final alignment; actor staged capture unsupported"})JSON");
}
bool DeepLevelBuildingDSL::Prepare(const TSharedRef<FJsonObject>& Root, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
{
	FString Error, Target; FOptions Options;
	if (!DeepLevelPCGRequest::Fields(*Root, {TEXT("version"), TEXT("domain"), TEXT("target"), TEXT("operations"), TEXT("generate"), TEXT("align"), TEXT("preview"), TEXT("dryRun"), TEXT("save"), TEXT("capture"), TEXT("includeState"), TEXT("camera"), TEXT("captureSize"), TEXT("timeoutSeconds")}, Error)
		|| !DeepLevelPCGRequest::String(*Root, TEXT("target"), Target, Error, true) || !Options.Read(*Root, Error)) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
	const TArray<TSharedPtr<FJsonValue>>* Ops;
	if (!Root->TryGetArrayField(TEXT("operations"), Ops) || Ops->Num() > 256) { Out.Report->SetStringField(TEXT("error"), TEXT("operations requires an array of at most 256 commands.")); return false; }
	Out.Report->SetStringField(TEXT("target"), Target);
	AActor* Actor = FindObject<AActor>(nullptr, *Target);
	if (Actor && (Actor->IsA<ADeepLevelPCGBuildingLineActor>() || Actor->IsA<ADeepLevelPCGRoadsideBuildingActor>()))
	{
		if (!Actor->GetWorld() || Actor->GetWorld()->IsGameWorld() || Actor->IsTemplate() || Root->HasField(TEXT("preview")))
		{ Out.Report->SetStringField(TEXT("error"), TEXT("Requires an editor actor instance; preview is catalog-only.")); return false; }
		return DeepLevelBuildingDSLInternal::LayoutPlan(*Actor, Root, *Ops, Options, Plan, Out);
	}
	if (Root->HasField(TEXT("generate")) || Root->HasField(TEXT("align"))) { Out.Report->SetStringField(TEXT("error"), TEXT("generate/align are actor-only.")); return false; }
	if (auto* Catalog = LoadObject<UDeepLevelBuildingPlacementCatalog>(nullptr, *Target)) { return DeepLevelBuildingDSLInternal::CatalogPlan(*Catalog, Root, *Ops, Options, Plan, Out); }
	Out.Report->SetStringField(TEXT("error"), TEXT("Target is not a loaded Building actor or BuildingPlacementCatalog.")); return false;
}
#undef LOCTEXT_NAMESPACE
