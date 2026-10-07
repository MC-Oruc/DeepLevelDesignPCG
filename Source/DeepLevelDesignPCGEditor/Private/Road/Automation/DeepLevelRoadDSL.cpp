// Copyright <--\, Inc. All Rights Reserved.
#include "Road/Automation/DeepLevelRoadDSL.h"
#include "Road/DeepLevelRoadCatalogAuthoring.h"
#include "Road/DeepLevelRoadNetworkAuthoring.h"
#include "Road/DeepLevelRoadEditor.h"
#include "Automation/DeepLevelPCGRequest.h"
#include "PCGComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DeepLevelRoadDSL"
using namespace DeepLevelPCGRequest;

namespace DeepLevelRoadDSLInternal
{
	TSharedRef<FJsonObject> TileState(const FDeepLevelRoadTileDefinition& Tile, int32 Index)
	{
		auto J = MakeShared<FJsonObject>(); J->SetNumberField(TEXT("tile"), Index);
		J->SetStringField(TEXT("mesh"), Tile.TileMesh.ToString()); J->SetStringField(TEXT("material"), Tile.TileMaterialOverride.ToString());
		J->SetArrayField(TEXT("center"), DeepLevelPCGRequest::VectorJson(Tile.PlacementVolume.Center));
		const auto R = Tile.PlacementVolume.Rotation;
		J->SetArrayField(TEXT("rotation"), DeepLevelPCGRequest::VectorJson(FVector(R.Pitch, R.Yaw, R.Roll)));
		J->SetArrayField(TEXT("extent"), DeepLevelPCGRequest::VectorJson(Tile.PlacementVolume.Extent));
		J->SetNumberField(TEXT("connections"), Tile.ConnectionMask); J->SetNumberField(TEXT("junction"), Tile.ApproachJunctionDirectionMask);
		J->SetNumberField(TEXT("weight"), Tile.SelectionWeight); J->SetBoolField(TEXT("calibrated"), Tile.bCalibrated);
		return J;
	}
	bool EditTile(const FJsonObject& Op, FDeepLevelRoadTileDefinition& Tile, FString& Error)
	{
		FString Mesh = Tile.TileMesh.ToString(), Material = Tile.TileMaterialOverride.ToString();
		FVector R(Tile.PlacementVolume.Rotation.Pitch, Tile.PlacementVolume.Rotation.Yaw, Tile.PlacementVolume.Rotation.Roll);
		if (!DeepLevelPCGRequest::String(Op, TEXT("mesh"), Mesh, Error) || !DeepLevelPCGRequest::String(Op, TEXT("material"), Material, Error)
			|| !DeepLevelPCGRequest::Vector(Op, TEXT("center"), Tile.PlacementVolume.Center, Error) || !DeepLevelPCGRequest::Vector(Op, TEXT("rotation"), R, Error)
			|| !DeepLevelPCGRequest::Vector(Op, TEXT("extent"), Tile.PlacementVolume.Extent, Error)
			|| !DeepLevelPCGRequest::Integer(Op, TEXT("connections"), Tile.ConnectionMask, 0, 15, Error)
			|| !DeepLevelPCGRequest::Integer(Op, TEXT("junction"), Tile.ApproachJunctionDirectionMask, 0, 15, Error)
			|| !DeepLevelPCGRequest::Number(Op, TEXT("weight"), Tile.SelectionWeight, 0.01, 1e9, Error)
			|| !DeepLevelPCGRequest::Bool(Op, TEXT("calibrated"), Tile.bCalibrated, Error)) { return false; }
		Tile.TileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Mesh));
		Tile.TileMaterialOverride = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Material));
		Tile.PlacementVolume.Rotation = FRotator(R.X, R.Y, R.Z);
		if (!Tile.TileMesh.LoadSynchronous() || (!Material.IsEmpty() && !Tile.TileMaterialOverride.LoadSynchronous()))
		{ Error = TEXT("Tile requires a valid mesh and optional material."); return false; }
		const int32 Junction = Tile.ApproachJunctionDirectionMask;
		if ((Junction && (FMath::CountBits64(Junction) != 1 || (Junction & Tile.ConnectionMask) != Junction))
			|| Tile.PlacementVolume.Extent.GetMin() < 1 || !FMath::IsNearlyEqual(Tile.PlacementVolume.Extent.X, Tile.PlacementVolume.Extent.Y))
		{ Error = TEXT("Tile requires square XY extents >=1; junction must be one connected edge."); return false; }
		return true;
	}
	bool CatalogPlan(UDeepLevelRoadTileCatalog& Catalog, const TSharedRef<FJsonObject>& Root,
		const TArray<TSharedPtr<FJsonValue>>& Ops, const FOptions& Options, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
	{
		auto Staged = TStrongObjectPtr<UDeepLevelRoadTileCatalog>(NewObject<UDeepLevelRoadTileCatalog>());
		Staged->Tiles = Catalog.Tiles; Staged->SidewalkWidthInTiles = Catalog.SidewalkWidthInTiles;
		FString Error; int32 Preview = 0;
		if (!DeepLevelPCGRequest::Integer(*Root, TEXT("preview"), Preview, 0, MAX_int32, Error)) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			Out.Report->SetNumberField(TEXT("operation"), Index);
			const auto Op = DeepLevelPCGRequest::Object(*Ops[Index]); FString Name;
			if (!Op || !DeepLevelPCGRequest::String(*Op, TEXT("op"), Name, Error, true)) { Error = TEXT("Operation must be an object with op."); break; }
			if (Name == TEXT("catalog.configure"))
			{
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("sidewalkWidth")}, Error)
					|| !DeepLevelPCGRequest::Integer(*Op, TEXT("sidewalkWidth"), Staged->SidewalkWidthInTiles, 1, 8, Error)) { break; }
				continue;
			}
			int32 Tile = INDEX_NONE;
			if (Name == TEXT("tile.add"))
			{
				if (Op->HasField(TEXT("tile"))) { Error = TEXT("tile.add does not accept an existing tile index."); break; }
				Tile = Staged->Tiles.AddDefaulted();
			}
			else if (!Op->HasField(TEXT("tile")) || !DeepLevelPCGRequest::Integer(*Op, TEXT("tile"), Tile, 0, Staged->Tiles.Num() - 1, Error))
			{ if (Error.IsEmpty()) { Error = TEXT("Existing tile index is required."); } break; }
			if (Name == TEXT("tile.remove"))
			{
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("tile")}, Error)) { break; }
				Staged->Tiles.RemoveAt(Tile); continue;
			}
			if (Name == TEXT("tile.autoFit"))
			{
				double Size = Staged->Tiles[Tile].PlacementVolume.Extent.X * 2;
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("tile"), TEXT("tileSize")}, Error) || !DeepLevelPCGRequest::Number(*Op, TEXT("tileSize"), Size, 2, 1e6, Error)) { break; }
				auto& D = Staged->Tiles[Tile];
				if (!DeepLevelRoadCatalogAuthoring::AutoFit(D.TileMesh.LoadSynchronous(), Size, D.PlacementVolume.Center, D.PlacementVolume.Extent))
				{ Error = TEXT("Tile AutoFit failed."); break; }
				D.bCalibrated = true; continue;
			}
			if (Name != TEXT("tile.add") && Name != TEXT("tile.update")) { Error = TEXT("Unsupported Road catalog operation: ") + Name; break; }
			if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("tile"), TEXT("mesh"), TEXT("material"), TEXT("center"), TEXT("rotation"), TEXT("extent"),
				TEXT("connections"), TEXT("junction"), TEXT("weight"), TEXT("calibrated")}, Error) || !EditTile(*Op, Staged->Tiles[Tile], Error)) { break; }
		}
		if (!Error.IsEmpty()) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		Out.Report->RemoveField(TEXT("operation"));
		if (Root->HasField(TEXT("preview")) && !Options.bCapture) { Out.Report->SetStringField(TEXT("error"), TEXT("preview requires capture=true.")); return false; }
		if (Options.bCapture && !Staged->Tiles.IsValidIndex(Preview)) { Out.Report->SetStringField(TEXT("error"), TEXT("Capture requires a valid preview tile index.")); return false; }
		Plan.Target.Reset(&Catalog);
		Plan.Apply = [Target = &Catalog, Staged, Options, bChanged = !Ops.IsEmpty()](FDeepLevelPCGAutomationResult& R)
		{
			if (Options.bDryRun || !bChanged) { return; }
			DeepLevelRoadCatalogAuthoring::Edit(*Target, LOCTEXT("CatalogBatch", "Edit Road Catalog"), [&]
			{ Target->Tiles = Staged->Tiles; Target->SidewalkWidthInTiles = Staged->SidewalkWidthInTiles; });
			R.Report->SetBoolField(TEXT("applied"), true);
		};
		Plan.Finish = [Staged, Target = &Catalog, Preview, Options](FDeepLevelPCGAutomationResult& R)
		{
			FText Validation;
			R.Report->SetBoolField(TEXT("validForGeneration"), Staged->ValidateForGeneration(Validation));
			R.Report->SetStringField(TEXT("validation"), Validation.ToString());
			if (Options.bIncludeState)
			{
				TArray<TSharedPtr<FJsonValue>> Tiles;
				for (int32 Index = 0; Index < Staged->Tiles.Num(); ++Index) { Tiles.Add(MakeShared<FJsonValueObject>(TileState(Staged->Tiles[Index], Index))); }
				R.Report->SetArrayField(TEXT("tiles"), Tiles); R.Report->SetNumberField(TEXT("sidewalkWidth"), Staged->SidewalkWidthInTiles);
			}
			if (Options.bSave)
			{
				FString Error; const bool bSaved = DeepLevelPCGRequest::Save(*Target, Error); R.Report->SetBoolField(TEXT("saved"), bSaved);
				if (!bSaved) { R.Report->SetStringField(TEXT("saveError"), Error); }
			}
			if (Options.bCapture)
			{
				const auto View = SNew(SDeepLevelRoadTileCatalogPreviewViewport);
				const auto& D = Staged->Tiles[Preview]; View->PreviewTile(&D, D.PlacementVolume.Extent.X * 2);
				DeepLevelPCGRequest::CapturePreview(View, Options, R);
			}
		};
		return true;
	}
	bool NetworkPlan(ADeepLevelRoadNetworkActor& Actor, const TSharedRef<FJsonObject>& Root,
		const TArray<TSharedPtr<FJsonValue>>& Ops, const FOptions& Options, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
	{
		using namespace DeepLevelRoadNetworkAuthoring;
		auto State = MakeShared<FState>(Read(Actor));
		FString Error; bool bGenerate = false; bool bControl = false;
		if (!DeepLevelPCGRequest::Bool(*Root, TEXT("generate"), bGenerate, Error)) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		TMap<FString, FString> Aliases;
		for (int32 Index = 0; Index < Ops.Num(); ++Index)
		{
			Out.Report->SetNumberField(TEXT("operation"), Index);
			const auto Op = DeepLevelPCGRequest::Object(*Ops[Index]); FString Name;
			if (!Op || !DeepLevelPCGRequest::String(*Op, TEXT("op"), Name, Error, true)) { Error = TEXT("Operation must have op."); break; }
			if (Name == TEXT("generation.cancel") || Name == TEXT("generation.cleanup"))
			{
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op")}, Error)) { break; }
				if (Ops.Num() != 1 || bGenerate || Options.bDryRun || Options.bCapture || Options.bSave) { Error = TEXT("Control operations must be standalone, without dryRun/save/capture/generate."); break; }
				bControl = true;
				Plan.Apply = [Component = TWeakObjectPtr<UPCGComponent>(Actor.PCGComponent), Name](FDeepLevelPCGAutomationResult& R)
				{
					if (!Component.IsValid()) { R.bSuccess = false; R.Report->SetStringField(TEXT("error"), TEXT("Missing PCG component.")); return; }
					if (Name == TEXT("generation.cancel")) { Component->CancelGeneration(); }
					else { Component->CleanupLocalImmediate(true); }
					R.Report->SetBoolField(TEXT("applied"), true);
				};
				continue;
			}
			if (Name == TEXT("network.configure"))
			{
				FString City = State->City.IsValid() ? State->City->GetPathName() : FString();
				FString Catalog = State->Catalog.ToString(), Collision = State->CollisionProfile.ToString();
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("city"), TEXT("catalog"), TEXT("collisionProfile")}, Error)
					|| !DeepLevelPCGRequest::String(*Op, TEXT("city"), City, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("catalog"), Catalog, Error)
					|| !DeepLevelPCGRequest::String(*Op, TEXT("collisionProfile"), Collision, Error)) { break; }
				State->City = FindObject<ADeepLevelCityLayoutActor>(nullptr, *City); State->Catalog = TSoftObjectPtr<UDeepLevelRoadTileCatalog>(FSoftObjectPath(Catalog));
				State->CollisionProfile = FName(*Collision);
				FCollisionResponseTemplate Profile;
				if (!State->City.IsValid() || State->City->GetWorld() != Actor.GetWorld() || !State->Catalog.LoadSynchronous()
					|| !UCollisionProfile::Get()->GetProfileTemplate(State->CollisionProfile, Profile)) { Error = TEXT("Invalid City, catalog or collision profile."); break; }
				continue;
			}
			if (Name.StartsWith(TEXT("branch.")))
			{
				FString Id, Alias;
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("branch"), TEXT("points"), TEXT("snap"), TEXT("as")}, Error)
					|| !DeepLevelPCGRequest::String(*Op, TEXT("branch"), Id, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("as"), Alias, Error)) { break; }
				if (Id.StartsWith(TEXT("$"))) { const auto* Found = Aliases.Find(Id.Mid(1)); if (!Found) { Error = TEXT("Unknown branch alias."); break; } Id = *Found; }
				int32 BranchIndex = State->Branches.IndexOfByPredicate([&](const FBranch& B) { return B.Id == Id; });
				if (Name == TEXT("branch.add"))
				{
					if (!Id.IsEmpty() || (!Alias.IsEmpty() && (Alias.StartsWith(TEXT("$")) || Aliases.Contains(Alias)))) { Error = TEXT("New branch requires a unique optional alias and no branch ID."); break; }
					BranchIndex = State->Branches.AddDefaulted(); Id = FGuid::NewGuid().ToString(); State->Branches[BranchIndex].Id = Id;
					if (!Alias.IsEmpty()) { Aliases.Add(Alias, Id); }
				}
				else if (BranchIndex == INDEX_NONE) { Error = TEXT("Branch does not belong to the target network."); break; }
				if (Name == TEXT("branch.remove"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("branch")}, Error)) { break; }
					State->Branches.RemoveAt(BranchIndex); continue;
				}
				if (Name != TEXT("branch.add") && Name != TEXT("branch.update")) { Error = TEXT("Unknown branch operation."); break; }
				if (Name == TEXT("branch.update") && Op->HasField(TEXT("as"))) { Error = TEXT("as is supported only on branch.add."); break; }
				bool bSnap = false; auto& Points = State->Branches[BranchIndex].Points;
				State->Branches[BranchIndex].bGeometryChanged |= Op->HasField(TEXT("points")) || Op->HasField(TEXT("snap"));
				if (!Points.Num() && !Op->HasField(TEXT("points"))) { Error = TEXT("New branch requires points."); break; }
				if (!DeepLevelPCGRequest::Bool(*Op, TEXT("snap"), bSnap, Error) || (Op->HasField(TEXT("points")) && !DeepLevelPCGRequest::Points(*Op, Points, Error))) { break; }
				FDeepLevelCityGrid Grid; FText GridError;
				if (!State->City.IsValid() || !State->City->ResolveGrid(Grid, GridError)) { Error = TEXT("Branch editing requires a valid City grid."); break; }
				for (int32 P = 0; P < Points.Num(); ++P)
				{
					const FVector Snapped = FDeepLevelRoadSplineComponentVisualizer::SnapWorldToGrid(Points[P], Grid.Origin, Grid.TileSize);
					if (bSnap) { Points[P] = Snapped; }
					if (!Points[P].Equals(Snapped, 0.01)) { Error = TEXT("Road points must be on the City grid; use snap=true explicitly."); break; }
					if (P && (Points[P].Equals(Points[P-1]) || (!FMath::IsNearlyEqual(Points[P].X, Points[P-1].X) && !FMath::IsNearlyEqual(Points[P].Y, Points[P-1].Y))))
					{ Error = TEXT("Road segments must be nonzero and grid-axis aligned."); break; }
				}
				if (!Error.IsEmpty()) { break; }
				continue;
			}
			if (Name == TEXT("cell.set") || Name == TEXT("cell.clear"))
			{
				if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("x"), TEXT("y"), TEXT("mode"), TEXT("kind"), TEXT("mesh"), TEXT("material"), TEXT("overrideMaterial"),
					TEXT("offset"), TEXT("rotation"), TEXT("scale")}, Error)) { break; }
				FIntPoint Cell;
				if (!Op->HasField(TEXT("x")) || !Op->HasField(TEXT("y")) || !DeepLevelPCGRequest::Integer(*Op, TEXT("x"), Cell.X, -1000000, 1000000, Error)
					|| !DeepLevelPCGRequest::Integer(*Op, TEXT("y"), Cell.Y, -1000000, 1000000, Error)) { if (Error.IsEmpty()) { Error = TEXT("Cell requires x and y."); } break; }
				if (Name == TEXT("cell.clear"))
				{
					if (!DeepLevelPCGRequest::Fields(*Op, {TEXT("op"), TEXT("x"), TEXT("y")}, Error)) { break; }
					State->Overrides.RemoveAll([Cell](const auto& C) { return C.GridCell == Cell; }); continue;
				}
				auto* Existing = State->Overrides.FindByPredicate([Cell](const auto& C) { return C.GridCell == Cell; });
				FDeepLevelRoadCellOverride Value = Existing ? *Existing : FDeepLevelRoadCellOverride(); Value.GridCell = Cell;
				FString Mode = Value.Mode == EDeepLevelRoadCellOverrideMode::Add ? TEXT("add") : Value.Mode == EDeepLevelRoadCellOverrideMode::Remove ? TEXT("remove") : TEXT("modify");
				FString Kind = Value.AddedTileKind == EDeepLevelRoadTileKind::Road ? TEXT("road") : TEXT("sidewalk");
				FString Mesh = Value.ReplacementMesh.ToString(), Material = Value.MaterialOverride.ToString();
				FVector R(Value.RotationOffset.Pitch, Value.RotationOffset.Yaw, Value.RotationOffset.Roll);
				if (!DeepLevelPCGRequest::String(*Op, TEXT("mode"), Mode, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("kind"), Kind, Error) || !DeepLevelPCGRequest::String(*Op, TEXT("mesh"), Mesh, Error)
					|| !DeepLevelPCGRequest::String(*Op, TEXT("material"), Material, Error) || !DeepLevelPCGRequest::Bool(*Op, TEXT("overrideMaterial"), Value.bOverrideMaterial, Error)
					|| !DeepLevelPCGRequest::Vector(*Op, TEXT("offset"), Value.LocalOffset, Error) || !DeepLevelPCGRequest::Vector(*Op, TEXT("rotation"), R, Error)
					|| !DeepLevelPCGRequest::Vector(*Op, TEXT("scale"), Value.ScaleMultiplier, Error)) { break; }
				if ((Mode != TEXT("add") && Mode != TEXT("remove") && Mode != TEXT("modify")) || (Kind != TEXT("road") && Kind != TEXT("sidewalk")))
				{ Error = TEXT("Unknown cell mode/kind."); break; }
				Value.Mode = Mode == TEXT("add") ? EDeepLevelRoadCellOverrideMode::Add : Mode == TEXT("remove") ? EDeepLevelRoadCellOverrideMode::Remove : EDeepLevelRoadCellOverrideMode::Modify;
				Value.AddedTileKind = Kind == TEXT("road") ? EDeepLevelRoadTileKind::Road : EDeepLevelRoadTileKind::Sidewalk;
				Value.ReplacementMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Mesh)); Value.MaterialOverride = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Material));
				Value.RotationOffset = FRotator(R.X, R.Y, R.Z);
				if ((Value.Mode == EDeepLevelRoadCellOverrideMode::Add && Mesh.IsEmpty()) || (!Mesh.IsEmpty() && !Value.ReplacementMesh.LoadSynchronous())
					|| (!Material.IsEmpty() && !Value.MaterialOverride.LoadSynchronous()) || Value.ScaleMultiplier.GetMin() <= 0)
				{ Error = TEXT("Invalid cell mesh/material/scale; add requires a mesh."); break; }
				if (Existing) { *Existing = Value; } else { State->Overrides.Add(Value); }
				continue;
			}
			Error = TEXT("Unsupported Road network operation: ") + Name; break;
		}
		if (!Error.IsEmpty()) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
		Out.Report->RemoveField(TEXT("operation"));
		const bool bChanged = !Ops.IsEmpty() && !bControl;
		if (Options.bDryRun && Options.bCapture) { Out.Report->SetStringField(TEXT("error"), TEXT("Actor dryRun does not render staged geometry; capture a committed generation.")); return false; }
		if (Options.bCapture && bChanged && !bGenerate) { Out.Report->SetStringField(TEXT("error"), TEXT("Capturing edited network output requires generate=true.")); return false; }
		if (!Actor.PCGComponent || ((bChanged || bGenerate || (bControl && Ops[0]->AsObject()->GetStringField(TEXT("op")) == TEXT("generation.cleanup"))) && Actor.PCGComponent->IsGenerating()))
		{ Out.Report->SetStringField(TEXT("error"), TEXT("Missing or busy PCG component. Inspect or cancel first.")); return false; }
		if (bGenerate)
		{
			FText Validation; FDeepLevelCityGrid Grid;
			if (!Actor.PCGComponent->GetGraph() || !State->City.IsValid() || !State->City->ResolveGrid(Grid, Validation)
				|| !State->Catalog.LoadSynchronous() || !State->Catalog.Get()->ValidateForGeneration(Validation) || State->Branches.IsEmpty())
			{ Out.Report->SetStringField(TEXT("error"), TEXT("Generation requires graph, grid, branches and a valid catalog: ") + Validation.ToString()); return false; }
		}
		Plan.Target.Reset(&Actor); Plan.TimeoutSeconds = Options.TimeoutSeconds;
		Plan.bRequiresAsync = !Options.bDryRun && (bGenerate || bChanged); Plan.Component = Actor.PCGComponent;
		if (!bControl)
		{
			Plan.Apply = [Target = &Actor, State, bChanged, Options, Aliases](FDeepLevelPCGAutomationResult& R)
			{
				TMap<FString, int32> AliasIndices;
				for (const auto& Pair : Aliases) { AliasIndices.Add(Pair.Key, State->Branches.IndexOfByPredicate([&](const FBranch& B) { return B.Id == Pair.Value; })); }
				if (bChanged && !Options.bDryRun)
				{
					const FScopedTransaction Transaction(LOCTEXT("NetworkBatch", "Edit Road Network")); Apply(*Target, *State); R.Report->SetBoolField(TEXT("applied"), true);
				}
				auto Ids = MakeShared<FJsonObject>();
				for (const auto& Pair : AliasIndices) { if (State->Branches.IsValidIndex(Pair.Value)) { Ids->SetStringField(Pair.Key, State->Branches[Pair.Value].Id); } }
				R.Report->SetObjectField(TEXT("ids"), Ids);
			};
		}
		if (bGenerate && !Options.bDryRun) { Plan.Generate = [Target = &Actor]() { if (!Target->PCGComponent->IsGenerating()) { Target->PCGComponent->GenerateLocal(true); } }; }
		Plan.Finish = [Target = &Actor, State, Options, bGenerate](FDeepLevelPCGAutomationResult& R)
		{
			R.Report->SetBoolField(TEXT("generating"), Target->PCGComponent->IsGenerating());
			R.Report->SetBoolField(TEXT("hasGeneratedOutput"), Target->PCGComponent->bGenerated);
			if (bGenerate && !Options.bDryRun && R.bSuccess && !Target->PCGComponent->bGenerated)
			{ R.bSuccess = false; R.Report->SetStringField(TEXT("generationError"), TEXT("PCG completed without generated output.")); }
			if (Options.bIncludeState)
			{
				const FState Current = Options.bDryRun ? *State : Read(*Target);
				TArray<TSharedPtr<FJsonValue>> Branches;
				for (const auto& B : Current.Branches)
				{
					auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("id"), B.Id);
					TArray<TSharedPtr<FJsonValue>> Points;
					for (const auto& P : B.Points) { Points.Add(MakeShared<FJsonValueArray>(DeepLevelPCGRequest::VectorJson(P))); }
					J->SetArrayField(TEXT("points"), Points); Branches.Add(MakeShared<FJsonValueObject>(J));
				}
				R.Report->SetArrayField(TEXT("branches"), Branches);
				R.Report->SetStringField(TEXT("city"), Current.City.IsValid() ? Current.City->GetPathName() : FString());
				R.Report->SetStringField(TEXT("catalog"), Current.Catalog.ToString()); R.Report->SetStringField(TEXT("collisionProfile"), Current.CollisionProfile.ToString());
				TArray<TSharedPtr<FJsonValue>> Cells;
				for (const auto& C : Current.Overrides)
				{
					auto J = MakeShared<FJsonObject>(); J->SetNumberField(TEXT("x"), C.GridCell.X); J->SetNumberField(TEXT("y"), C.GridCell.Y);
					J->SetStringField(TEXT("mode"), C.Mode == EDeepLevelRoadCellOverrideMode::Add ? TEXT("add") : C.Mode == EDeepLevelRoadCellOverrideMode::Remove ? TEXT("remove") : TEXT("modify"));
					J->SetStringField(TEXT("mesh"), C.ReplacementMesh.ToString()); J->SetStringField(TEXT("material"), C.MaterialOverride.ToString());
					J->SetBoolField(TEXT("overrideMaterial"), C.bOverrideMaterial); J->SetStringField(TEXT("kind"), C.AddedTileKind == EDeepLevelRoadTileKind::Road ? TEXT("road") : TEXT("sidewalk"));
					J->SetArrayField(TEXT("offset"), DeepLevelPCGRequest::VectorJson(C.LocalOffset)); J->SetArrayField(TEXT("rotation"), DeepLevelPCGRequest::VectorJson(FVector(C.RotationOffset.Pitch, C.RotationOffset.Yaw, C.RotationOffset.Roll)));
					J->SetArrayField(TEXT("scale"), DeepLevelPCGRequest::VectorJson(C.ScaleMultiplier)); Cells.Add(MakeShared<FJsonValueObject>(J));
				}
				R.Report->SetArrayField(TEXT("cells"), Cells);
				FDeepLevelCityGrid Grid; FText Error;
				if (Current.City.IsValid() && Current.City->ResolveGrid(Grid, Error))
				{ auto J = MakeShared<FJsonObject>(); J->SetNumberField(TEXT("tileSize"), Grid.TileSize); J->SetArrayField(TEXT("origin"), DeepLevelPCGRequest::VectorJson(Grid.Origin)); R.Report->SetObjectField(TEXT("grid"), J); }
			}
			if (R.bSuccess && Options.bSave) { FString E; bool bSaved = DeepLevelPCGRequest::Save(*Target, E); R.Report->SetBoolField(TEXT("saved"), bSaved); if (!bSaved) { R.Report->SetStringField(TEXT("saveError"), E); } }
			if (R.bSuccess && Options.bCapture) { DeepLevelPCGRequest::CaptureWorld(*Target, *Target->PCGComponent, Options, R); }
		};
		return true;
	}
}

FString DeepLevelRoadDSL::Describe()
{
	return TEXT(R"JSON({"target":"loaded editor RoadNetwork actor path OR RoadTileCatalog asset path","operations":{"network.configure":["city","catalog","collisionProfile"],"branch.add":["points","snap?","as?"],"branch.update":["branch (path or $alias)","points","snap?"],"branch.remove":["branch"],"cell.set":["x","y","mode: add|modify|remove","kind: road|sidewalk","mesh","material","overrideMaterial","offset","rotation","scale"],"cell.clear":["x","y"],"catalog.configure":["sidewalkWidth: 1..8"],"tile.add":["mesh","material?","connections: 0..15","junction: 0 or one connected bit","center","rotation","extent","weight","calibrated"],"tile.update":["tile index","same fields as add"],"tile.remove":["tile index"],"tile.autoFit":["tile index","tileSize?"],"generation.cancel":[],"generation.cleanup":[]},"generate":"actor-only boolean; waits for PCG completion; failures/cancellation prevent capture/save","preview":"catalog capture tile index, default 0","coordinates":"world centimeters for branches; offsets are tile-local; rotations [pitch,yaw,roll]","catalog":"indices refer to the staged array at that operation; partial authoring allowed, validForGeneration reports readiness","control":"cancel/cleanup standalone; cleanup refuses busy generation","limits":{"pointsPerBranch":4096},"capture":"isolated catalog preview or fresh editor-world viewport; no user camera changes, no PIE","dryRun":"validates authoring only, never runs PCG; actor staged capture unsupported"})JSON");
}
bool DeepLevelRoadDSL::Prepare(const TSharedRef<FJsonObject>& Root, FDeepLevelPCGAutomationPlan& Plan, FDeepLevelPCGAutomationResult& Out)
{
	FString Error, Target; FOptions Options;
	if (!DeepLevelPCGRequest::Fields(*Root, {TEXT("version"), TEXT("domain"), TEXT("target"), TEXT("operations"), TEXT("generate"), TEXT("preview"), TEXT("dryRun"), TEXT("save"), TEXT("capture"), TEXT("includeState"), TEXT("camera"), TEXT("captureSize"), TEXT("timeoutSeconds")}, Error)
		|| !DeepLevelPCGRequest::String(*Root, TEXT("target"), Target, Error, true) || !Options.Read(*Root, Error)) { Out.Report->SetStringField(TEXT("error"), Error); return false; }
	const TArray<TSharedPtr<FJsonValue>>* Ops;
	if (!Root->TryGetArrayField(TEXT("operations"), Ops) || Ops->Num() > 256) { Out.Report->SetStringField(TEXT("error"), TEXT("operations requires an array of at most 256 commands.")); return false; }
	Out.Report->SetStringField(TEXT("target"), Target);
	if (auto* Actor = FindObject<ADeepLevelRoadNetworkActor>(nullptr, *Target))
	{
		if (!Actor->GetWorld() || Actor->GetWorld()->IsGameWorld() || Actor->IsTemplate() || Root->HasField(TEXT("preview")))
		{ Out.Report->SetStringField(TEXT("error"), TEXT("Requires an editor actor instance; preview is catalog-only.")); return false; }
		return DeepLevelRoadDSLInternal::NetworkPlan(*Actor, Root, *Ops, Options, Plan, Out);
	}
	if (Root->HasField(TEXT("generate"))) { Out.Report->SetStringField(TEXT("error"), TEXT("generate is actor-only.")); return false; }
	if (auto* Catalog = LoadObject<UDeepLevelRoadTileCatalog>(nullptr, *Target)) { return DeepLevelRoadDSLInternal::CatalogPlan(*Catalog, Root, *Ops, Options, Plan, Out); }
	Out.Report->SetStringField(TEXT("error"), TEXT("Target is not a loaded RoadNetwork actor or RoadTileCatalog.")); return false;
}
#undef LOCTEXT_NAMESPACE
