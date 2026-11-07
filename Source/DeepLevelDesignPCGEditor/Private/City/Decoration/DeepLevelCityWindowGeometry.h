#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
class UDynamicMesh;
namespace DeepLevelCityWindows
{
    FVector CenterGlass(UDynamicMesh* Glass);
    // Join neighboring glass panes inside one opening; never bridge floor gaps or different facade planes.
    TArray<TStrongObjectPtr<UDynamicMesh>> GroupGlass(UDynamicMesh* Glass);
}
