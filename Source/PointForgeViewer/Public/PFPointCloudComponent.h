#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "PFPointCloudComponent.generated.h"

class FPFOctreeStore;

/**
 * Renders a converted PointForge octree directory. Holds the resident hierarchy
 * (FPFOctreeStore) and produces an FPFPointCloudSceneProxy.
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class POINTFORGEVIEWER_API UPFPointCloudComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPFPointCloudComponent();

	/** Opens an already-converted octree directory and (re)builds the proxy. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	bool OpenOctreeDir(const FString& OctreeDir);

	/** Frees the resident hierarchy and clears the proxy. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void Close();

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	/** Source units (e.g. metres) -> UE world units (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	float UnitScale = 100.0f;

	/** Resident octree (game-thread owned; read by the proxy at create time). */
	TSharedPtr<FPFOctreeStore> Store;
};
