// Copyright <--\, Inc. All Rights Reserved.
#include "City/Decoration/Automation/DeepLevelCityDecorationDSL.h"
#include "City/Decoration/Automation/DeepLevelCityDecorationCapture.h"
#include "City/Decoration/DeepLevelCityDecorationDocument.h"
#include "City/Decoration/DeepLevelCityDecorationAuthoring.h"
#include "City/Decoration/DeepLevelCityDecorationValidation.h"
#include "Automation/Data/DeepLevelPCGAutomationResult.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

namespace
{
	namespace Authoring = DeepLevelCityDecorationAuthoring;
	using FObject = TSharedRef<FJsonObject>;

	FString Json(const FObject& Object)
	{
		FString Text;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}
	bool Fields(const FObject& Object, std::initializer_list<const TCHAR*> Allowed, FString& Error)
	{
		for (const auto& Field : Object->Values)
		{
			bool bKnown = false;
			for (const TCHAR* Name : Allowed) { bKnown |= Field.Key == Name; }
			if (!bKnown) { Error = FString::Printf(TEXT("Unknown field: %s"), *Field.Key); return false; }
		}
		return true;
	}
	bool String(const FObject& Object, const TCHAR* Key, FString& Value, FString& Error, bool bRequired = true)
	{
		if (!bRequired && !Object->HasField(Key)) { return true; }
		if (Object->TryGetStringField(Key, Value) && !Value.IsEmpty()) { return true; }
		Error = FString(Key) + TEXT(" must be a nonempty string."); return false;
	}
	bool Bool(const FObject& Object, const TCHAR* Key, bool& Value, FString& Error)
	{
		if (!Object->HasField(Key)) { return true; }
		if (Object->TryGetBoolField(Key, Value)) { return true; }
		Error = FString(Key) + TEXT(" must be boolean."); return false;
	}
	bool Number(const FObject& Object, const TCHAR* Key, double& Value, FString& Error)
	{
		if (!Object->HasField(Key)) { return true; }
		if (Object->TryGetNumberField(Key, Value) && FMath::IsFinite(Value)) { return true; }
		Error = FString(Key) + TEXT(" must be a finite number."); return false;
	}
	bool Vector(const FObject& Object, const TCHAR* Key, FVector& Value, FString& Error)
	{
		if (!Object->HasField(Key)) { return true; }
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 3)
		{
			Error = FString(Key) + TEXT(" must have three finite numeric components."); return false;
		}
		for (int32 I = 0; I < 3; ++I)
		{
			double Component;
			if (!(*Values)[I]->TryGetNumber(Component) || !FMath::IsFinite(Component))
			{
				Error = FString(Key) + TEXT(" must have three finite numeric components."); return false;
			}
			Value[I] = Component;
		}
		return true;
	}
	TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& V)
	{
		return {MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)};
	}
	void WriteTransform(const FTransform& T, const FObject& Out)
	{
		Out->SetArrayField(TEXT("location"), VectorJson(T.GetLocation()));
		const auto R = T.Rotator();
		Out->SetArrayField(TEXT("rotation"), VectorJson(FVector(R.Pitch, R.Yaw, R.Roll)));
		Out->SetArrayField(TEXT("scale"), VectorJson(T.GetScale3D()));
	}
	bool PatchTransform(const FObject& Object, FTransform& T, FString& Error)
	{
		FVector L = T.GetLocation(), S = T.GetScale3D();
		const auto Old = T.Rotator();
		FVector R(Old.Pitch, Old.Yaw, Old.Roll);
		if (!Vector(Object, TEXT("location"), L, Error) || !Vector(Object, TEXT("rotation"), R, Error) || !Vector(Object, TEXT("scale"), S, Error)) { return false; }
		if (Object->HasField(TEXT("location"))) { T.SetLocation(L); }
		if (Object->HasField(TEXT("rotation"))) { T.SetRotation(FRotator(R.X, R.Y, R.Z).Quaternion()); }
		if (Object->HasField(TEXT("scale"))) { T.SetScale3D(S); }
		FText Failure;
		if (!DeepLevelCityDecorationValidation::ValidateTransform(T, Failure)) { Error = Failure.ToString(); return false; }
		return true;
	}
	FObject State(const UDeepLevelCityBuildingDecorationProfile& Profile)
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("building"), Profile.BuildingClass.ToString());
		TArray<TSharedPtr<FJsonValue>> Variants;
		for (const auto& V : Profile.Variants)
		{
			auto Variant = MakeShared<FJsonObject>();
			Variant->SetStringField(TEXT("id"), V.VariantGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Variant->SetStringField(TEXT("name"), V.Name.ToString());
			Variant->SetNumberField(TEXT("weight"), V.SelectionWeight);
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (const auto& E : V.Entries)
			{
				auto Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("id"), E.EntryGuid.ToString(EGuidFormats::DigitsWithHyphens));
				Entry->SetStringField(TEXT("name"), E.Name.ToString());
				Entry->SetStringField(TEXT("output"), E.Output == EDeepLevelCityDecorationOutput::Mesh ? TEXT("mesh") : E.Output == EDeepLevelCityDecorationOutput::Actor ? TEXT("actor") : TEXT("decal"));
				Entry->SetStringField(TEXT("asset"), E.Output == EDeepLevelCityDecorationOutput::Mesh ? E.Mesh.ToString() : E.Output == EDeepLevelCityDecorationOutput::Actor ? E.ActorClass.ToString() : E.DecalMaterial.ToString());
				Entry->SetArrayField(TEXT("decalSize"), VectorJson(E.DecalSize));
				WriteTransform(E.LocalTransform, Entry);
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Variant->SetArrayField(TEXT("entries"), Entries);
			TArray<TSharedPtr<FJsonValue>> Links;
			for (const auto& P : V.SymmetryPairs)
			{
				auto Link = MakeShared<FJsonObject>();
				Link->SetStringField(TEXT("source"), P.First.ToString(EGuidFormats::DigitsWithHyphens));
				Link->SetStringField(TEXT("target"), P.Second.ToString(EGuidFormats::DigitsWithHyphens));
				Link->SetArrayField(TEXT("planeOrigin"), VectorJson(P.PlaneOrigin));
				Link->SetArrayField(TEXT("planeNormal"), VectorJson(P.PlaneNormal));
				Link->SetBoolField(TEXT("location"), P.bSyncLocation);
				Link->SetBoolField(TEXT("rotation"), P.bSyncRotation);
				Link->SetBoolField(TEXT("scale"), P.bSyncScale);
				Link->SetBoolField(TEXT("mirrorGeometry"), P.bMirrorGeometry);
				Link->SetStringField(TEXT("rotationMode"), P.RotationMode == EDeepLevelCityDecorationRotationLink::Mirror ? TEXT("mirror") : TEXT("copy"));
				Link->SetArrayField(TEXT("locationOffset"), VectorJson(P.LocationOffset));
				Link->SetArrayField(TEXT("rotationOffset"), VectorJson(FVector(P.RotationOffset.Pitch, P.RotationOffset.Yaw, P.RotationOffset.Roll)));
				Link->SetArrayField(TEXT("scaleMultiplier"), VectorJson(P.ScaleMultiplier));
				Links.Add(MakeShared<FJsonValueObject>(Link));
			}
			Variant->SetArrayField(TEXT("links"), Links);
			Variants.Add(MakeShared<FJsonValueObject>(Variant));
		}
		Out->SetArrayField(TEXT("variants"), Variants);
		return Out;
	}

	bool OperationFields(const FObject& Op, const FString& Action, FString& Error)
	{
		if (Action == TEXT("variant.add")) { return Fields(Op, {TEXT("op"),TEXT("as"),TEXT("name"),TEXT("weight")}, Error); }
		if (Action == TEXT("variant.update")) { return Fields(Op, {TEXT("op"),TEXT("variant"),TEXT("name"),TEXT("weight")}, Error); }
		if (Action == TEXT("variant.duplicate")) { return Fields(Op, {TEXT("op"),TEXT("as"),TEXT("variant"),TEXT("name"),TEXT("weight")}, Error); }
		if (Action == TEXT("variant.remove")) { return Fields(Op, {TEXT("op"),TEXT("variant")}, Error); }
		if (Action == TEXT("entry.add")) { return Fields(Op, {TEXT("op"),TEXT("as"),TEXT("variant"),TEXT("asset"),TEXT("name"),TEXT("location"),TEXT("rotation"),TEXT("scale"),TEXT("decalSize")}, Error); }
		if (Action == TEXT("entry.update")) { return Fields(Op, {TEXT("op"),TEXT("variant"),TEXT("entry"),TEXT("asset"),TEXT("name"),TEXT("location"),TEXT("rotation"),TEXT("scale"),TEXT("decalSize")}, Error); }
		if (Action == TEXT("entry.duplicate")) { return Fields(Op, {TEXT("op"),TEXT("as"),TEXT("variant"),TEXT("entry")}, Error); }
		if (Action == TEXT("entry.remove") || Action == TEXT("link.remove")) { return Fields(Op, {TEXT("op"),TEXT("variant"),TEXT("entry")}, Error); }
		if (Action == TEXT("entry.move")) { return Fields(Op, {TEXT("op"),TEXT("variant"),TEXT("entry"),TEXT("direction")}, Error); }
		if (Action == TEXT("link.create")) { return Fields(Op, {TEXT("op"),TEXT("as"),TEXT("variant"),TEXT("entry"),TEXT("target"),TEXT("axis"),TEXT("syncLocation"),TEXT("syncRotation"),TEXT("syncScale"),TEXT("mirrorGeometry"),TEXT("rotationMode"),TEXT("locationOffset"),TEXT("rotationOffset"),TEXT("scaleMultiplier")}, Error); }
		if (Action == TEXT("link.update")) { return Fields(Op, {TEXT("op"),TEXT("variant"),TEXT("entry"),TEXT("syncLocation"),TEXT("syncRotation"),TEXT("syncScale"),TEXT("mirrorGeometry"),TEXT("rotationMode"),TEXT("locationOffset"),TEXT("rotationOffset"),TEXT("scaleMultiplier")}, Error); }
		Error = TEXT("Unsupported operation: ") + Action;
		return false;
	}

	struct FAlias { FGuid Id; bool bVariant = false; };
	class FBatch
	{
	public:
		FDeepLevelCityDecorationDocument& Document;
		TMap<FString, FAlias> Aliases;
		TArray<TSharedPtr<FJsonValue>> Results;
		FString Error;
		explicit FBatch(FDeepLevelCityDecorationDocument& InDocument) : Document(InDocument) {}

		bool Resolve(const FObject& Op, const TCHAR* Key, bool bVariant, FGuid& Id)
		{
			FString Text;
			if (!String(Op, Key, Text, Error)) { return false; }
			if (Text.StartsWith(TEXT("$")))
			{
				const auto* Alias = Aliases.Find(Text.Mid(1));
				if (Alias && Alias->bVariant == bVariant) { Id = Alias->Id; return true; }
			}
			else if (FGuid::Parse(Text, Id) && Id.IsValid()) { return true; }
			Error = FString(Key) + TEXT(" requires a GUID or earlier alias of the correct kind."); return false;
		}
		bool Select(const FObject& Op, bool bEntry)
		{
			FGuid Variant, Entry;
			if (!Resolve(Op, TEXT("variant"), true, Variant) || !Authoring::FindVariant(*Document.GetProfile(), Variant))
			{
				if (Error.IsEmpty()) { Error = TEXT("Variant not found in the selected building profile."); }
				return false;
			}
			Document.SelectVariant(Variant);
			if (!bEntry) { return true; }
			if (!Resolve(Op, TEXT("entry"), false, Entry) || !Document.GetEntry(Entry))
			{
				if (Error.IsEmpty()) { Error = TEXT("Entry not found in this variant."); }
				return false;
			}
			Document.SelectEntry(Entry);
			return true;
		}
		bool LinkSettings(const FObject& Op)
		{
			const auto* Pair = Document.GetSymmetryPair();
			if (!Pair) { Error = TEXT("Selected entry has no transform link."); return false; }
			auto Desired = *Pair;
			FVector R(Desired.RotationOffset.Pitch, Desired.RotationOffset.Yaw, Desired.RotationOffset.Roll);
			if (!Bool(Op, TEXT("syncLocation"), Desired.bSyncLocation, Error) || !Bool(Op, TEXT("syncRotation"), Desired.bSyncRotation, Error)
				|| !Bool(Op, TEXT("syncScale"), Desired.bSyncScale, Error) || !Bool(Op, TEXT("mirrorGeometry"), Desired.bMirrorGeometry, Error)
				|| !Vector(Op, TEXT("locationOffset"), Desired.LocationOffset, Error) || !Vector(Op, TEXT("rotationOffset"), R, Error)
				|| !Vector(Op, TEXT("scaleMultiplier"), Desired.ScaleMultiplier, Error)) { return false; }
			Desired.RotationOffset = FRotator(R.X, R.Y, R.Z);
			if (Op->HasField(TEXT("rotationMode")))
			{
				FString Mode;
				if (!String(Op, TEXT("rotationMode"), Mode, Error)) { return false; }
				if (Mode != TEXT("mirror") && Mode != TEXT("copy")) { Error = TEXT("rotationMode must be mirror or copy."); return false; }
				Desired.RotationMode = Mode == TEXT("copy") ? EDeepLevelCityDecorationRotationLink::Copy : EDeepLevelCityDecorationRotationLink::Mirror;
			}
			FText Failure;
			if (!Document.SetSymmetrySettings(Desired, Failure)) { Error = Failure.ToString(); return false; }
			return true;
		}
		bool Run(const FObject& Op)
		{
			FString Action, Alias;
			if (!String(Op, TEXT("op"), Action, Error) || !OperationFields(Op, Action, Error) || !String(Op, TEXT("as"), Alias, Error, false)) { return false; }
			if (!Alias.IsEmpty() && (Alias.StartsWith(TEXT("$")) || Aliases.Contains(Alias))) { Error = TEXT("Alias must be new and omit the $ prefix."); return false; }
			FGuid Produced;
			bool bVariant = false;
			if (Action == TEXT("variant.add") || Action == TEXT("variant.update") || Action == TEXT("variant.duplicate") || Action == TEXT("variant.remove"))
			{
				if (Action != TEXT("variant.add") && !Select(Op, false)) { return false; }
				FString Name;
				double Weight = Action == TEXT("variant.add") ? 1 : Document.GetVariant()->SelectionWeight;
				if (!String(Op, TEXT("name"), Name, Error, false) || !Number(Op, TEXT("weight"), Weight, Error) || Weight < 0) { if (Error.IsEmpty()) { Error = TEXT("weight must be nonnegative."); } return false; }
				if (Action == TEXT("variant.remove")) { Document.RemoveVariant(); }
				else
				{
					if (Action == TEXT("variant.add")) { Document.AddVariant(); }
					if (Action == TEXT("variant.duplicate")) { Document.DuplicateVariant(); if (!Op->HasField(TEXT("weight"))) { Weight = Document.GetVariant()->SelectionWeight; } }
					Document.SetVariantFields(Name.IsEmpty() ? Document.GetVariant()->Name : FName(*Name), Weight);
					Produced = Document.GetVariantId(); bVariant = true;
				}
			}
			else if (Action == TEXT("entry.add") || Action == TEXT("entry.update"))
			{
				if (!Select(Op, Action == TEXT("entry.update"))) { return false; }
				const auto Baseline = Action == TEXT("entry.update") ? *Document.GetEntry() : FDeepLevelCityBuildingDecorationEntry();
				auto Desired = Baseline;
				FString Asset, Name;
				if (!String(Op, TEXT("asset"), Asset, Error, Action == TEXT("entry.add")) || !String(Op, TEXT("name"), Name, Error, false)) { return false; }
				FText Failure;
				if (!Asset.IsEmpty())
				{
					FDeepLevelCityBuildingDecorationEntry FromAsset;
					if (!Authoring::MakeEntryFromAsset(LoadObject<UObject>(nullptr, *Asset), FromAsset, Failure)) { Error = Failure.ToString(); return false; }
					Desired.Output = FromAsset.Output; Desired.Mesh = FromAsset.Mesh; Desired.ActorClass = FromAsset.ActorClass; Desired.DecalMaterial = FromAsset.DecalMaterial;
					if (Action == TEXT("entry.add")) { Desired.Name = FromAsset.Name; }
				}
				if (!Name.IsEmpty()) { Desired.Name = FName(*Name); }
				if (!PatchTransform(Op, Desired.LocalTransform, Error) || !Vector(Op, TEXT("decalSize"), Desired.DecalSize, Error)) { return false; }
				if (!Desired.Validate(Failure)) { Error = Failure.ToString(); return false; }
				if (Action == TEXT("entry.add")) { Document.AddEntries(MakeArrayView(&Desired, 1)); }
				else if (!Document.SetEntryFields(Desired, Baseline, Failure)) { Error = Failure.ToString(); return false; }
				Produced = Document.GetEntryId();
			}
			else if (Action == TEXT("entry.duplicate") || Action == TEXT("entry.remove") || Action == TEXT("entry.move"))
			{
				if (!Select(Op, true)) { return false; }
				if (Action == TEXT("entry.duplicate")) { Document.DuplicateEntry(); Produced = Document.GetEntryId(); }
				else if (Action == TEXT("entry.remove")) { Document.RemoveEntry(); }
				else
				{
					double Direction = 0;
					if (!Number(Op, TEXT("direction"), Direction, Error) || (Direction != 1 && Direction != -1)) { Error = TEXT("direction must be -1 or 1."); return false; }
					Document.MoveEntry(static_cast<int32>(Direction)); Produced = Document.GetEntryId();
				}
			}
			else if (Action == TEXT("link.create") || Action == TEXT("link.update") || Action == TEXT("link.remove"))
			{
				if (!Select(Op, true)) { return false; }
				if (Action == TEXT("link.remove"))
				{
					if (!Document.GetSymmetryPair()) { Error = TEXT("Entry has no link."); return false; }
					Document.UnlinkSymmetry();
				}
				else
				{
					if (Action == TEXT("link.create"))
					{
						FString Axis; FGuid Target;
						if (!String(Op, TEXT("axis"), Axis, Error)) { return false; }
						if (Op->HasField(TEXT("target")) && !Resolve(Op, TEXT("target"), false, Target)) { return false; }
						const auto A = Axis == TEXT("X") ? EAxis::X : Axis == TEXT("Y") ? EAxis::Y : Axis == TEXT("Z") ? EAxis::Z : EAxis::None;
						if (!Document.CreateSymmetry(A, Target)) { Error = TEXT("Cannot link: check axis, calibration, existing pairing and target variant."); return false; }
					}
					if (!LinkSettings(Op)) { return false; }
					const auto* Pair = Document.GetSymmetryPair();
					Produced = Action == TEXT("link.create") ? Pair->Second : Document.GetEntryId();
				}
			}
			else { Error = TEXT("Unsupported operation: ") + Action; return false; }
			if (!Alias.IsEmpty())
			{
				if (!Produced.IsValid()) { Error = TEXT("This operation does not produce an aliasable ID."); return false; }
				Aliases.Add(Alias, {Produced, bVariant});
			}
			auto Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("op"), Action);
			if (Produced.IsValid()) { Result->SetStringField(TEXT("id"), Produced.ToString(EGuidFormats::DigitsWithHyphens)); }
			if (!Alias.IsEmpty()) { Result->SetStringField(TEXT("as"), Alias); }
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return true;
		}
	};
}

FString DeepLevelCityDecorationDSL::Describe()
{
	return TEXT(R"JSON({"scope":"Existing building profiles linked through a City Decoration Set. No level generation, asset creation or arbitrary property/function calls.","context":"set required; building required for edits/capture. Empty operations inspects catalog; provide building to inspect variants/entries/links.","operations":[{"op":"variant.add","fields":["name?","weight?","as?"]},{"op":"variant.update","fields":["variant","name?","weight?"]},{"op":"variant.duplicate","fields":["variant","name?","weight?","as?"]},{"op":"variant.remove","fields":["variant"]},{"op":"entry.add","fields":["variant","asset","name?","location?","rotation?","scale?","decalSize?","as?"]},{"op":"entry.update","fields":["variant","entry","asset?","name?","location?","rotation?","scale?","decalSize?"]},{"op":"entry.duplicate","fields":["variant","entry","as?"]},{"op":"entry.remove","fields":["variant","entry"]},{"op":"entry.move","fields":["variant","entry","direction=-1|1"]},{"op":"link.create","fields":["variant","entry","axis=X|Y|Z","target?","syncLocation?","syncRotation?","syncScale?","mirrorGeometry?","rotationMode=mirror|copy?","locationOffset?","rotationOffset?","scaleMultiplier?","as?"]},{"op":"link.update","fields":["variant","entry","syncLocation?","syncRotation?","syncScale?","mirrorGeometry?","rotationMode=mirror|copy?","locationOffset?","rotationOffset?","scaleMultiplier?"]},{"op":"link.remove","fields":["variant","entry"]}],"vectors":"[x,y,z], positions cm; rotations [pitch,yaw,roll] degrees; scales/multipliers dimensionless","captureVariant":"root variant GUID/$alias or explicitly selected variant of the last operation; no guessed arrangement","linkOffsets":"First/source to Second/target, same semantics as Details; existing targets keep placements when offsets are omitted","example":{"version":1,"domain":"decoration","set":"/Game/Example/DA_Set.DA_Set","building":"/Game/Example/BP_Building.BP_Building_C","operations":[{"op":"variant.add","name":"Facade","as":"v"},{"op":"entry.add","variant":"$v","asset":"/Engine/BasicShapes/Cube.Cube","location":[100,200,300],"as":"lamp"},{"op":"link.create","variant":"$v","entry":"$lamp","axis":"Y","as":"other"}],"variant":"$v","capture":true,"save":false}})JSON");
}

void DeepLevelCityDecorationDSL::Execute(const FObject& Request, FDeepLevelPCGAutomationResult& Out)
{
	FString Error, SetPath, Building;
	bool bCapture = false, bSave = false, bDryRun = false, bState = true;
	const auto Fail = [&](const FString& Message) { Out.Report->SetStringField(TEXT("error"), Message); };
	if (!Fields(Request, {TEXT("version"),TEXT("domain"),TEXT("set"),TEXT("building"),TEXT("variant"),TEXT("operations"),TEXT("capture"),TEXT("save"),TEXT("dryRun"),TEXT("includeState"),TEXT("camera"),TEXT("captureSize")}, Error)
		|| !String(Request, TEXT("set"), SetPath, Error) || !String(Request, TEXT("building"), Building, Error, false)
		|| !Bool(Request, TEXT("capture"), bCapture, Error) || !Bool(Request, TEXT("save"), bSave, Error)
		|| !Bool(Request, TEXT("dryRun"), bDryRun, Error) || !Bool(Request, TEXT("includeState"), bState, Error)) { Fail(Error); return; }
	if (!bCapture && (Request->HasField(TEXT("camera")) || Request->HasField(TEXT("captureSize")))) { Fail(TEXT("camera/captureSize require capture=true.")); return; }
	if (bDryRun && bSave) { Fail(TEXT("dryRun and save cannot both be true.")); return; }
	const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
	if (!Request->TryGetArrayField(TEXT("operations"), Operations) || Operations->Num() > 256) { Fail(TEXT("operations must be an array with at most 256 entries.")); return; }
	FDeepLevelCityDecorationCaptureOptions CaptureOptions;
	if (Request->HasField(TEXT("captureSize")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Size = nullptr;
		if (!Request->TryGetArrayField(TEXT("captureSize"), Size) || Size->Num() != 2) { Fail(TEXT("captureSize requires [width,height].")); return; }
		for (int32 I = 0; I < 2; ++I)
		{
			double V;
			if (!(*Size)[I]->TryGetNumber(V) || !FMath::IsFinite(V) || V < 128 || V > 2048 || FMath::FloorToDouble(V) != V) { Fail(TEXT("captureSize dimensions must be integers from 128 to 2048.")); return; }
			CaptureOptions.Size[I] = static_cast<int32>(V);
		}
	}
	if (Request->HasField(TEXT("camera")))
	{
		const TSharedPtr<FJsonObject>* Camera = nullptr;
		if (!Request->TryGetObjectField(TEXT("camera"), Camera) || !Camera || !Camera->IsValid()) { Fail(TEXT("camera must be an object.")); return; }
		const auto C = Camera->ToSharedRef();
		FVector L = FVector::ZeroVector, R = FVector::ZeroVector; double FOV = 90;
		if (!Fields(C, {TEXT("location"),TEXT("rotation"),TEXT("fov")}, Error) || !C->HasField(TEXT("location")) || !C->HasField(TEXT("rotation"))
			|| !Vector(C, TEXT("location"), L, Error) || !Vector(C, TEXT("rotation"), R, Error) || !Number(C, TEXT("fov"), FOV, Error)
			|| FOV < 10 || FOV > 150) { Fail(Error.IsEmpty() ? TEXT("camera requires location/rotation and FOV between 10 and 150.") : Error); return; }
		CaptureOptions.Camera = FTransform(FRotator(R.X, R.Y, R.Z), L);
		CaptureOptions.FOV = FOV;
	}
	TStrongObjectPtr<UDeepLevelCityDecorationSet> Set(LoadObject<UDeepLevelCityDecorationSet>(nullptr, *SetPath));
	if (!Set.IsValid()) { Fail(TEXT("City Decoration Set not found.")); return; }
	FDeepLevelCityDecorationDocument Live;
	Live.Open(Set.Get());
	Out.Report->SetStringField(TEXT("set"), Set->GetPathName());
	if (Building.IsEmpty())
	{
		if (!Operations->IsEmpty() || bCapture || bSave) { Fail(TEXT("An explicit catalog building class is required for edits, capture or save.")); return; }
		TArray<TSharedPtr<FJsonValue>> Buildings;
		if (const auto* Catalog = Live.GetCatalog())
		{
			for (const auto& Definition : Catalog->Buildings)
			{
				auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("building"), Definition.BuildingClass.ToString());
				Row->SetBoolField(TEXT("calibrated"), Definition.bCalibrated);
				const auto* P = Live.GetProfileForBuilding(Definition.BuildingClass);
				Row->SetStringField(TEXT("profile"), P ? P->GetPathName() : FString());
				Buildings.Add(MakeShared<FJsonValueObject>(Row));
			}
		}
		Out.Report->SetArrayField(TEXT("buildings"), Buildings);
		TArray<TSharedPtr<FJsonValue>> Issues;
		for (const auto& Issue : Live.GetIssues()) { Issues.Add(MakeShared<FJsonValueString>(Issue.Message.ToString())); }
		Out.Report->SetArrayField(TEXT("issues"), Issues);
		Out.bSuccess = true; return;
	}
	if (!Live.SelectBuilding(TSoftClassPtr<AActor>(FSoftObjectPath(Building))) || !Live.GetProfile()) { Fail(TEXT("Building requires a unique linked profile in the set's catalog.")); return; }
	if (!Operations->IsEmpty() && !Live.CanEdit()) { Fail(TEXT("Profile identities/links are invalid; resolve document diagnostics before editing.")); return; }
	auto* Original = const_cast<UDeepLevelCityBuildingDecorationProfile*>(Live.GetProfile());
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> OriginalOwner(Original);
	TStrongObjectPtr<UDeepLevelCityBuildingDecorationProfile> Stage(NewObject<UDeepLevelCityBuildingDecorationProfile>(GetTransientPackage(), NAME_None, RF_Transient));
	Stage->BuildingClass = Original->BuildingClass;
	Stage->Variants = Original->Variants;
	TStrongObjectPtr<UDeepLevelCityDecorationSet> StageSet(NewObject<UDeepLevelCityDecorationSet>(GetTransientPackage(), NAME_None, RF_Transient));
	StageSet->BuildingCatalog = Set->BuildingCatalog;
	StageSet->Categories = Set->Categories;
	StageSet->BuildingProfiles = Set->BuildingProfiles;
	for (auto& P : StageSet->BuildingProfiles) { if (P == Original) { P = Stage.Get(); } }
	FDeepLevelCityDecorationDocument Document(EDeepLevelCityDecorationEditMode::Staging);
	Document.Open(StageSet.Get());
	Document.SelectBuilding(Stage->BuildingClass);
	FBatch Batch(Document);
	for (int32 I = 0; I < Operations->Num(); ++I)
	{
		const TSharedPtr<FJsonObject>* Op = nullptr;
		if (!(*Operations)[I]->TryGetObject(Op) || !Op || !Op->IsValid() || !Batch.Run(Op->ToSharedRef()))
		{
			Out.Report->SetNumberField(TEXT("failedOperation"), I);
			Fail(Batch.Error.IsEmpty() ? TEXT("Operation must be an object.") : Batch.Error); return;
		}
	}
	FText Failure;
	const bool bValidated = Stage->Validate(Failure);
	if ((!Operations->IsEmpty() || bSave) && !bValidated) { Fail(Failure.ToString()); return; }
	FGuid CaptureVariant = Document.GetVariantId();
	if (Request->HasField(TEXT("variant")))
	{
		if (!Batch.Resolve(Request, TEXT("variant"), true, CaptureVariant) || !Authoring::FindVariant(*Stage, CaptureVariant))
		{ Fail(Batch.Error.IsEmpty() ? TEXT("Capture variant not found.") : Batch.Error); return; }
	}
	if (bCapture && !Authoring::FindVariant(*Stage, CaptureVariant)) { Fail(TEXT("Capture requires an explicit variant or a variant selected by an operation.")); return; }
	const bool bChanged = Json(State(*Original)) != Json(State(*Stage));
	if (!bDryRun && bChanged && !Live.CommitVariants(Stage->Variants, Failure)) { Fail(Failure.IsEmpty() ? TEXT("Commit rejected.") : Failure.ToString()); return; }
	Out.Report->SetBoolField(TEXT("applied"), !bDryRun && bChanged);
	Out.Report->SetBoolField(TEXT("changed"), bChanged);
	Out.Report->SetBoolField(TEXT("validated"), bValidated);
	if (!bValidated) { Out.Report->SetStringField(TEXT("validationError"), Failure.ToString()); }
	Out.Report->SetBoolField(TEXT("dryRun"), bDryRun);
	Out.Report->SetStringField(TEXT("profile"), Original->GetPathName());
	Out.Report->SetArrayField(TEXT("operations"), Batch.Results);
	if (bState) { Out.Report->SetObjectField(TEXT("state"), State(bDryRun ? *Stage : *Original)); }
	Out.bSuccess = true;
	if (bSave)
	{
		UPackage* Package = Original->GetOutermost();
		FString Filename;
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		const bool bSaved = !Original->HasAnyFlags(RF_Transient) && FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension())
			&& UPackage::SavePackage(Package, Original, *Filename, Args);
		Out.Report->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved) { Out.bSuccess = false; Out.Report->SetStringField(TEXT("saveError"), TEXT("Profile save failed; check package path and write access. Applied changes remain undoable.")); }
	}
	if (bCapture) { DeepLevelCityDecorationCapture::Capture(bDryRun ? *Stage : *Original, CaptureVariant, CaptureOptions, Out); }
}
