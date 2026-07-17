// Copyright <--\, Inc. All Rights Reserved.

#include "AssetToolsModule.h"
#include "ComponentVisualizers.h"
#include "DeepLevelDesignPCGModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Road/DeepLevelRoadEditor.h"
#include "Building/DeepLevelBuildingEditor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DeepLevelDesignPCGEditorModule"

class FDeepLevelDesignPCGEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		AssetCategory = AssetTools.RegisterAdvancedAssetCategory(
			TEXT("DeepLevelDesignPCG"),
			LOCTEXT("AssetCategory", "Deep Level Design PCG"));

		RegisterAssetActions<FDeepLevelBuildingCatalogAssetTypeActions>(AssetTools);
		RegisterAssetActions<FDeepLevelRoadTileCatalogAssetTypeActions>(AssetTools);

		FPropertyEditorModule& PropertyEditor =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditor.RegisterCustomClassLayout(
			TEXT("DeepLevelRoadNetworkActor"),
			FOnGetDetailCustomizationInstance::CreateStatic(&FDeepLevelRoadNetworkActorDetails::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();

		FComponentVisualizersModule& Visualizers =
			FModuleManager::LoadModuleChecked<FComponentVisualizersModule>(TEXT("ComponentVisualizers"));
		RoadSplineVisualizer = MakeShared<FDeepLevelRoadSplineComponentVisualizer>();
		Visualizers.RegisterComponentVisualizer(
			UDeepLevelRoadSplineComponent::StaticClass()->GetFName(),
			RoadSplineVisualizer);
		Visualizers.RegisterComponentVisualizer(
			UDeepLevelRoadNetworkRootComponent::StaticClass()->GetFName(),
			RoadSplineVisualizer);
		FSlateApplication::Get().RegisterInputPreProcessor(
			StaticCastSharedPtr<IInputProcessor>(RoadSplineVisualizer));
		FDeepLevelRoadEditorRefreshService::Initialize();
		GenerationFailedHandle = FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().AddRaw(
			this,
			&FDeepLevelDesignPCGEditorModule::ShowGenerationFailure);
	}

	virtual void ShutdownModule() override
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		FDeepLevelDesignPCGEditorEvents::OnGenerationFailed().Remove(GenerationFailedHandle);
		GenerationFailedHandle.Reset();
		FDeepLevelRoadEditorRefreshService::Shutdown();
		if (FSlateApplication::IsInitialized() && RoadSplineVisualizer.IsValid())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(
				StaticCastSharedPtr<IInputProcessor>(RoadSplineVisualizer));
		}
		if (GUnrealEd && RoadSplineVisualizer.IsValid())
		{
			GUnrealEd->UnregisterComponentVisualizer(UDeepLevelRoadSplineComponent::StaticClass()->GetFName());
			GUnrealEd->UnregisterComponentVisualizer(UDeepLevelRoadNetworkRootComponent::StaticClass()->GetFName());
		}
		RoadSplineVisualizer.Reset();

		if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
		{
			IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			for (const TSharedPtr<IAssetTypeActions>& Action : RegisteredAssetActions)
			{
				if (Action.IsValid())
				{
					AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
				}
			}
		}
		RegisteredAssetActions.Empty();

		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyEditor =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyEditor.UnregisterCustomClassLayout(TEXT("DeepLevelRoadNetworkActor"));
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}

private:
	void ShowGenerationFailure(const FText& SystemName, const FText& Message) const
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("GenerationFailed", "{0} generation failed: {1}"),
			SystemName,
			Message));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 8.0f;
		Info.FadeOutDuration = 0.5f;
		if (const TSharedPtr<SNotificationItem> Notification =
			FSlateNotificationManager::Get().AddNotification(Info))
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	template <typename TAssetActions>
	void RegisterAssetActions(IAssetTools& AssetTools)
	{
		TSharedRef<TAssetActions> Action = MakeShared<TAssetActions>(AssetCategory);
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredAssetActions.Add(Action);
	}

	EAssetTypeCategories::Type AssetCategory = EAssetTypeCategories::Misc;
	TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetActions;
	TSharedPtr<FDeepLevelRoadSplineComponentVisualizer> RoadSplineVisualizer;
	FDelegateHandle GenerationFailedHandle;
};

IMPLEMENT_MODULE(FDeepLevelDesignPCGEditorModule, DeepLevelDesignPCGEditor)

#undef LOCTEXT_NAMESPACE
