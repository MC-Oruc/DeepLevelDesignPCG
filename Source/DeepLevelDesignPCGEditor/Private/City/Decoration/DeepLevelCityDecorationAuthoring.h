// Copyright <--\, Inc. All Rights Reserved.

#pragma once

#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"

namespace DeepLevelCityDecorationAuthoring
{
	FDeepLevelCityBuildingDecorationVariant* FindVariant(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id);
	FDeepLevelCityBuildingDecorationEntry* FindEntry(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FGuid& EntryId);
	const FDeepLevelCityBuildingDecorationVariant* FindVariant(const UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id);
	const FDeepLevelCityBuildingDecorationEntry* FindEntry(const UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FGuid& EntryId);
	FGuid AddVariant(UDeepLevelCityBuildingDecorationProfile& Profile);
	FGuid DuplicateVariant(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id);
	FGuid AddEntry(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FDeepLevelCityBuildingDecorationEntry& Definition);
	FGuid DuplicateEntry(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FGuid& EntryId);
	bool RemoveVariant(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& Id);
	bool RemoveEntry(UDeepLevelCityBuildingDecorationProfile& Profile, const FGuid& VariantId, const FGuid& EntryId);
	bool MakeEntryFromAsset(UObject* Asset, FDeepLevelCityBuildingDecorationEntry& OutEntry, FText& OutError);
	FTransform ApplyTransformDelta(const FTransform& Transform, const FVector& Translation, const FRotator& Rotation, const FVector& Scale);
}
