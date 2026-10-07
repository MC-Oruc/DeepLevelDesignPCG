// Copyright <--\, Inc. All Rights Reserved.
#pragma once

#include "Building/DeepLevelBuildingPCG.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "EditorUndoClient.h"
#include "UObject/GCObject.h"

class FScopedTransaction;

enum class EDeepLevelCityDecorationChange : uint8
{
	Context, Selection, Variant, Structure, Metadata, Output, Transform, Undo
};

enum class EDeepLevelCityDecorationEditMode : uint8 { Undoable, Staging };

struct FDeepLevelCityDecorationDocumentIssue
{
	FSoftObjectPath Building;
	FGuid Variant;
	FGuid Entry;
	FText Message;
	int32 AssignmentIndex = INDEX_NONE;
	TWeakObjectPtr<const UDeepLevelCityDecorationSet> AssignmentSet;
	TWeakObjectPtr<const UDeepLevelCityBuildingDecorationProfile> AssignmentProfile;
};

/** Sole owner of decoration authoring mutations, selection and transactions. */
class FDeepLevelCityDecorationDocument final : public FGCObject, public FEditorUndoClient
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FChanged, EDeepLevelCityDecorationChange, FGuid);
	FChanged OnChanged;

	explicit FDeepLevelCityDecorationDocument(EDeepLevelCityDecorationEditMode Mode = EDeepLevelCityDecorationEditMode::Undoable);
	virtual ~FDeepLevelCityDecorationDocument() override;
	void Shutdown();
	void Open(UDeepLevelCityDecorationSet* Set, UDeepLevelCityBuildingDecorationProfile* RequestedProfile = nullptr);
	void SetCatalog(UDeepLevelBuildingPlacementCatalog* Catalog);
	bool SelectBuilding(const TSoftClassPtr<AActor>& Class);
	void SelectVariant(const FGuid& Id);
	void SelectEntry(const FGuid& Id);
	bool AttachProfile(UDeepLevelCityBuildingDecorationProfile* Profile, bool bInitializeNew = false);

	const UDeepLevelCityDecorationSet* GetSet() const { return Set; }
	const UDeepLevelBuildingPlacementCatalog* GetCatalog() const { return Catalog; }
	const UDeepLevelCityBuildingDecorationProfile* GetProfile() const { return Profile; }
	const UDeepLevelCityBuildingDecorationProfile* GetProfileForBuilding(const TSoftClassPtr<AActor>& Class) const;
	const TSoftClassPtr<AActor>& GetBuildingClass() const { return BuildingClass; }
	const FDeepLevelBuildingPlacementDefinition* GetBuildingDefinition() const;
	const FDeepLevelCityBuildingDecorationVariant* GetVariant() const;
	const FDeepLevelCityBuildingDecorationEntry* GetEntry() const;
	const FDeepLevelCityBuildingDecorationEntry* GetEntry(const FGuid& Id) const;
	const FDeepLevelCityDecorationSymmetryPair* GetSymmetryPair() const;
	bool CanCreateSymmetry(FGuid Target = {}) const;
	bool CreateSymmetry(EAxis::Type Axis, FGuid Target = {});
	void UnlinkSymmetry();
	bool SetSymmetrySettings(const FDeepLevelCityDecorationSymmetryPair& Desired, FText& Error);
	bool CommitVariants(const TArray<FDeepLevelCityBuildingDecorationVariant>& Variants, FText& Error);
	FGuid GetVariantId() const { return VariantId; }
	FGuid GetEntryId() const { return EntryId; }
	const TArray<FDeepLevelCityDecorationDocumentIssue>& GetIssues() const { return Issues; }
	bool CanEdit() const;
	bool CanCreateProfile() const;
	bool HasClipboard() const { return Clipboard.IsSet(); }
	bool IsDragging() const { return bDragging; }

	bool RemoveAssignment(const UDeepLevelCityDecorationSet* ExpectedSet, int32 Index, const UDeepLevelCityBuildingDecorationProfile* ExpectedProfile);
	void SetCategories(const TArray<TObjectPtr<UDeepLevelCityDecorationCategory>>& Categories);
	void AddVariant();
	void DuplicateVariant();
	void RemoveVariant();
	void AddEntries(TConstArrayView<FDeepLevelCityBuildingDecorationEntry> Entries);
	void DuplicateEntry();
	void RemoveEntry();
	void CopyEntry();
	void PasteEntry();
	void MoveEntry(int32 Direction);
	void SetVariantFields(FName Name, double Weight);
	bool SetEntryFields(const FDeepLevelCityBuildingDecorationEntry& Desired, const FDeepLevelCityBuildingDecorationEntry& Baseline, FText& Error);
	bool SetTransform(const FTransform& Transform, FText& Error);
	void ApplyDragTransform(const FTransform& Transform);
	void EndDrag(bool bNotify = true, bool bExternalEdit = false);
	void Undo();
	void Redo();

	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDeepLevelCityDecorationDocument"); }
private:
	void Edit(const FText& Description, EDeepLevelCityDecorationChange Change, TFunctionRef<void()> Mutation);
	void Notify(EDeepLevelCityDecorationChange Change);
	void SynchronizeSymmetryTransform();
	bool ValidateLinkedTransform(const FTransform& Transform, FText& Error) const;
	void BroadcastTransformChange();
	void ReconcileSelection();
	void RefreshIssues();
	void ObjectChanged(UObject* Object, FPropertyChangedEvent& Event);
	void ResolveBuildingProfile();
	FDeepLevelCityBuildingDecorationEntry* MutableEntry();

	TObjectPtr<UDeepLevelCityDecorationSet> Set;
	TObjectPtr<UDeepLevelBuildingPlacementCatalog> Catalog;
	TObjectPtr<UDeepLevelCityBuildingDecorationProfile> Profile;
	TSoftClassPtr<AActor> BuildingClass;
	FGuid VariantId;
	FGuid EntryId;
	TOptional<FDeepLevelCityBuildingDecorationEntry> Clipboard;
	TArray<FDeepLevelCityDecorationDocumentIssue> Issues;
	FDelegateHandle PropertyChangedHandle;
	TUniquePtr<FScopedTransaction> DragTransaction;
	FTransform DragStart;
	bool bDragging = false;
	bool bDirtyBeforeDrag = false;
	bool bMutating = false;
	bool bClosed = false;
	EDeepLevelCityDecorationEditMode EditMode;
};
