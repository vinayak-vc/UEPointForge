#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include <atomic>
#include "PFPointCloudComponent.generated.h"

class FPFOctreeStore;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** How the billboard material drives the points' visible colour. */
UENUM(BlueprintType)
enum class EPFColorMode : uint8
{
	/** Use the cloud's per-point RGB (default). */
	RGB             UMETA(DisplayName = "RGB"),
	/** Greyscale ramp from per-point intensity (LAS Intensity field). */
	Intensity       UMETA(DisplayName = "Intensity"),
	/** Colormap by world-Z (blue=low → red=high). Range from the cloud's bbox. */
	Elevation       UMETA(DisplayName = "Elevation"),
	/** ASPRS LAS classification palette (ground/building/vegetation/water/etc.). */
	Classification  UMETA(DisplayName = "Classification")
};

/**
 * Live streaming stats, written by the scene proxy (render thread) and read by
 * the component (game thread) for the viewer panel. Display-only — relaxed
 * atomics, no synchronisation guarantees beyond per-field tearing-free reads.
 */
struct FPFViewerStats
{
	std::atomic<int32> VisibleNodes{ 0 };
	std::atomic<int32> DrawnNodes{ 0 };
	std::atomic<int32> ResidentNodes{ 0 };
	std::atomic<int64> PointsOnGpu{ 0 };
	std::atomic<int64> DrawnPoints{ 0 };
	std::atomic<int64> ResidentBytes{ 0 };
	std::atomic<int32> PendingLoads{ 0 };
};

/** Blueprint-readable snapshot of FPFViewerStats for the UMG panel. */
USTRUCT(BlueprintType)
struct FPFViewerStatsBP
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int64 CloudPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int32 CloudNodes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int32 VisibleNodes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int32 DrawnNodes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int64 PointsOnGpu = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int64 DrawnPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") float ResidentMB = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") int32 LoadQueue = 0;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") float FPS = 0.f;
	// Cloud world-space Z range (used to set Elevation sliders correctly).
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") float CloudZMin = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "PointForge") float CloudZMax = 0.f;
};

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
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Snapshot of live streaming stats for the viewer panel. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	FPFViewerStatsBP GetStats() const;

	/** Source units (e.g. metres) -> UE world units (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	float UnitScale = 100.0f;

	/**
	 * Unlit, vertex-colour material for the points. If unset, the engine default
	 * material is used (points render grey/lit — visible, but no colour). Assign
	 * a material reading VertexColor -> Emissive (Shading Model: Unlit) to see colour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	TObjectPtr<UMaterialInterface> PointMaterial;

	/** Source colour is 16-bit (LAS/LAZ); use the high byte. Set false for 8-bit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	bool bColorIs16Bit = true;

	//~ Streaming tunables (mirror pfview's panel; pushed to the proxy each tick) ---
	/** Screen-space-error budget in pixels. Smaller = more detail streamed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|LOD", meta = (ClampMin = "0.3", ClampMax = "8.0"))
	float SseBudgetPixels = 1.5f;

	/** GPU memory budget for resident point buffers (MB); LRU evicts over this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|LOD", meta = (ClampMin = "128", ClampMax = "8192"))
	int32 GpuBudgetMB = 1024;

	/** Max node payloads uploaded to the GPU per frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|LOD", meta = (ClampMin = "1", ClampMax = "256"))
	int32 UploadsPerFrame = 32;

	/** Point count limit in millions (0 = unlimited). Thins the cloud by halting traversal once reached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|LOD", meta = (ClampMin = "0", ClampMax = "500"))
	float PointCountLimit = 0.0f;

	/** Base point size in pixels (used once milestone #2B sized points land). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Render", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float PointSize = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Render")
	bool bRoundPoints = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Render")
	bool bAttenuate = false;

	/** Soft round edge falloff (0 = hard mask, >0 = anti-aliased disc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Render",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SoftRoundFalloff = 0.f;

	/** Drives the material's colour selection (RGB / Intensity / Elevation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Color")
	EPFColorMode ColorMode = EPFColorMode::RGB;

	/** Override the cloud's bbox Z range for Elevation mode (used if Max > Min, else auto). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Color")
	float ElevationMinZ = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Color")
	float ElevationMaxZ = 0.f;

	/** Enable a slicing plane (everything on the positive side of Normal is hidden). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Clip")
	bool bUseClippingPlane = false;

	/** Clipping plane origin (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Clip")
	FVector ClippingPlaneOrigin = FVector::ZeroVector;

	/** Clipping plane normal — positive side is hidden. Will be normalised. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|Clip")
	FVector ClippingPlaneNormal = FVector(0, 0, 1);

	/** Dynamic instance of PointMaterial; drives PointSize/Round/Attenuate live. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PointMID;

	/** Resident octree (game-thread owned; const hierarchy read by the proxy). */
	TSharedPtr<FPFOctreeStore> Store;

	/** Shared with the proxy; proxy writes (render thread), component reads (game). */
	TSharedPtr<FPFViewerStats, ESPMode::ThreadSafe> Stats;

	/** Smoothed frame time for the panel FPS readout. */
	float SmoothedDeltaSeconds = 0.f;
};
