// Copyright. PointForge Viewer — Gaussian splat component (billboard path).
#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "PFSplatComponent.generated.h"

class UMaterialInterface;

/**
 * Draws a 3D Gaussian Splat cloud (.ply 3DGS, .splat, or .vxpc with splat.bin)
 * through PointForgeNative.dll.
 *
 * Usage: add to any actor, call LoadSplatFile(path), and assign SplatMaterial
 * (a translucent material built by Scripts/build_splat_material.py).
 *
 * Coordinates: the file's centred metres are scaled by UnitScale (100 = Unreal cm),
 * the same convention as the point cloud component.
 */
UCLASS(ClassGroup = (PointForge), meta = (BlueprintSpawnableComponent))
class POINTFORGEVIEWER_API UPFSplatComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPFSplatComponent();

	/** Reads the file's bounds and count, then rebuilds the render proxy. */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Splats")
	bool LoadSplatFile(const FString& Path);

	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	int32 GetSplatCount() const { return SplatCount; }

	/** Path last loaded with LoadSplatFile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PointForge|Splats")
	FString SplatFile;

	/** Metres -> Unreal units. 100 = centimetres (matches the point cloud component). */
	UPROPERTY(EditAnywhere, Category = "PointForge|Splats", meta = (ClampMin = "0.001"))
	float UnitScale = 100.0f;

	/** LOD: splats smaller than this on screen are skipped. 0 = draw all. */
	UPROPERTY(EditAnywhere, Category = "PointForge|Splats", meta = (ClampMin = "0.0"))
	float MinSplatPixels = 0.5f;

	/** Upper bound on splats drawn per view and frame (cost cap). */
	UPROPERTY(EditAnywhere, Category = "PointForge|Splats", meta = (ClampMin = "1000"))
	int32 MaxSplatInstances = 300000;

	/** Translucent material with the Gaussian falloff (see Scripts/build_splat_material.py). */
	UPROPERTY(EditAnywhere, Category = "PointForge|Splats")
	TObjectPtr<UMaterialInterface> SplatMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PointForge|Splats")
	int32 SplatCount = 0;

	//~ UPrimitiveComponent
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	/** Bounds in splat-local metres (from the native DLL). */
	FBox LocalBoundsM = FBox(ForceInit);
};
