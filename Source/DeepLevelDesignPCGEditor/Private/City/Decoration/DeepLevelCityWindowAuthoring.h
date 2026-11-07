#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "City/Decoration/DeepLevelCityBuildingDecorationProfile.h"
#include "DeepLevelCityWindowAuthoring.generated.h"
class UDeepLevelCityDecorationSet;
class UMaterialInterface;

UCLASS()
class UDeepLevelCityWindowAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "City Decoration|Windows")
    static bool ImportWindows(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building,
        UMaterialInterface* Material, FText& Error);
    UFUNCTION(BlueprintCallable, Category = "City Decoration|Windows")
    static bool FillArrangement(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building, FGuid Variant, float Probability, int32 Seed);
    UFUNCTION(BlueprintCallable, Category = "City Decoration|Windows")
    static void OpenWindowEditor(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building);
    UFUNCTION(BlueprintCallable, Category = "City Decoration|Windows")
    static bool SetWindowArrangements(UDeepLevelCityDecorationSet* Set, TSubclassOf<AActor> Building,
        const TArray<FDeepLevelCityBuildingDecorationVariant>& WindowVariants, FText& Error);
};
