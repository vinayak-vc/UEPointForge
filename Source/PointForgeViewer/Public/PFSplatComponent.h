// Copyright. PointForge Viewer — Gaussian splat component.
#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "PFNativeApi.h"           // PFSplatGpuRecord
#include "PFSplatComponent.generated.h"

class UMaterialInterface;
class UPFSplatComponent;

/**
 * Which way is "up" in the splat file. Splat files are right-handed; Unreal is
 * left-handed Z-up, so every option also flips handedness (no mirrored scenes).
 */
UENUM(BlueprintType)
enum class EPFSplatUpAxis : uint8
{
	/** 3DGS / COLMAP training output: x right, y down, z forward. */
	YDown   UMETA(DisplayName = "Y down (3DGS / COLMAP)"),
	/** glTF / OpenGL convention: x right, y up, z towards the viewer. */
	YUp     UMETA(DisplayName = "Y up (glTF / OpenGL)"),
	/** Right-handed Z up (LiDAR / CAD style). */
	ZUp     UMETA(DisplayName = "Z up (right-handed)"),
	/** Use the stored coordinates unchanged (mirrored in Unreal). */
	AsStored UMETA(DisplayName = "As stored (no conversion)"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPFSplatLoadedEvent, UPFSplatComponent*, Component, int32, SplatCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPFSplatLoadFailedEvent, UPFSplatComponent*, Component, const FString&, Path);

/** Result of parsing a splat file; built off the game thread, applied on it. */
struct FPFSplatLoadResult
{
	bool bOk = false;
	FString Path;
	EPFSplatUpAxis Axis = EPFSplatUpAxis::YDown;
	TSharedPtr<TArray<PFSplatGpuRecord>, ESPMode::ThreadSafe> Records;   // Unreal axes, metres
	FBox BoundsM = FBox(ForceInit);       // all splats
	FBox CoreBoundsM = FBox(ForceInit);   // 2nd..98th percentile per axis (ignores floaters)
	int32 Count = 0;
};

/**
 * Draws a 3D Gaussian Splat cloud (.ply 3DGS, .splat, or .vxpc with splat.bin)
 * through PointForgeNative.dll.
 *
 * Blueprint:
 *   - Load:     LoadSplatFile (blocking) or LoadSplatFileAsync (background);
 *               bind OnSplatLoaded / OnSplatLoadFailed.
 *   - Settings: Set Up Axis / Unit Scale / Min Splat Pixels / Max Splat
 *               Instances / Use Gpu Splats / Splat Material (rebuild the render
 *               state; setting the variables directly in Blueprint calls these).
 *   - Material: standard material slot 0, so "Create Dynamic Material Instance"
 *               and Set Scalar Parameter (Brightness, OpacityScale) work.
 *   - Queries:  Get Splat Bounds / Get Core Bounds / Get Suggested Viewpoint.
 *
 * The file is parsed once per path / up-axis and kept in memory (about 56 bytes
 * per splat), so property edits and level loads do not re-read it. Bounds are
 * computed from the splats and saved with the level.
 *
 * Coordinates: the file's centred metres are converted to Unreal axes (UpAxis)
 * and scaled by UnitScale (100 = Unreal cm).
 */
UCLASS(ClassGroup = (PointForge), BlueprintType, meta = (BlueprintSpawnableComponent))
class POINTFORGEVIEWER_API UPFSplatComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPFSplatComponent();

	// ---- Loading ---------------------------------------------------------------

	/** Loads (or reloads) the file on the game thread, then rebuilds the render proxy. Fires OnSplatLoaded / OnSplatLoadFailed. */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Splats")
	bool LoadSplatFile(const FString& Path);

	/**
	 * Loads the file on a background thread; the current splats stay visible until
	 * it finishes. Fires OnSplatLoaded / OnSplatLoadFailed on the game thread. If
	 * called again before finishing, only the latest request is applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Splats")
	void LoadSplatFileAsync(const FString& Path);

	/** Drops the splats (frees the cached data and GPU buffers). */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Splats")
	void UnloadSplats();

	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	bool IsSplatLoaded() const { return GpuRecords.IsValid() || SplatCount > 0; }

	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	bool IsLoading() const { return bAsyncLoading; }

	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	int32 GetSplatCount() const { return SplatCount; }

	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	FString GetSplatFile() const { return SplatFile; }

	/** Fired on the game thread when a load (sync or async) succeeds. */
	UPROPERTY(BlueprintAssignable, Category = "PointForge|Splats")
	FPFSplatLoadedEvent OnSplatLoaded;

	/** Fired on the game thread when a load (sync or async) fails. */
	UPROPERTY(BlueprintAssignable, Category = "PointForge|Splats")
	FPFSplatLoadFailedEvent OnSplatLoadFailed;

	// ---- Queries ---------------------------------------------------------------

	/** Bounds of every splat, component space (Unreal units). Includes floaters. */
	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	FBox GetSplatBounds() const { return GetLocalBoundsUU(); }

	/**
	 * "Core" bounds: 2nd..98th percentile per axis, so stray floaters far outside
	 * the scene are ignored. In world space when bWorldSpace (axis-aligned box of
	 * the transformed core), else component space (Unreal units).
	 */
	UFUNCTION(BlueprintPure, Category = "PointForge|Splats")
	FBox GetCoreBounds(bool bWorldSpace = true) const;

	/**
	 * A starting camera inside the scene: eye height (1.7 m) above the core
	 * floor, near one end of the longest horizontal side, looking along it.
	 * Returns false if nothing is loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Splats")
	bool GetSuggestedViewpoint(FVector& OutLocation, FRotator& OutRotation) const;

	// ---- Settings --------------------------------------------------------------

	/** Splat file to draw (absolute path). Use LoadSplatFile / LoadSplatFileAsync to change it at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PointForge|Splats")
	FString SplatFile;

	/** "Up" in the file (see EPFSplatUpAxis). GPU path only; the CPU fallback draws the file as stored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUpAxis, Category = "PointForge|Splats")
	EPFSplatUpAxis UpAxis = EPFSplatUpAxis::YDown;

	/** Metres -> Unreal units. 100 = centimetres (matches the point cloud component). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUnitScale, Category = "PointForge|Splats", meta = (ClampMin = "0.001"))
	float UnitScale = 100.0f;

	/** LOD: splats smaller than this on screen are skipped. 0 = draw all (LOD off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMinSplatPixels, Category = "PointForge|Splats", meta = (ClampMin = "0.0"))
	float MinSplatPixels = 0.5f;

	/**
	 * Splats whose on-screen radius exceeds this fraction of the larger viewport
	 * side are skipped (GPU path). Such splats are blurry blobs right in front of
	 * the camera that cover the whole screen and dominate the frame cost.
	 * Higher = keeps more near-camera splats (slower). 1 = radius up to one screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMaxSplatScreenFraction, Category = "PointForge|Splats", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float MaxSplatScreenFraction = 1.0f;

	/** Upper bound on splats drawn per view and frame (cost cap). The nearest are kept. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMaxSplatInstances, Category = "PointForge|Splats", meta = (ClampMin = "1000"))
	int32 MaxSplatInstances = 300000;

	/**
	 * Render on the GPU: compute-shader projection + GPU radix sort every frame
	 * (no CPU work per frame, no camera lag). Falls back to the CPU path when the
	 * RHI is below SM5 or the native DLL is too old.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUseGpuSplats, Category = "PointForge|Splats")
	bool bUseGpuSplats = true;

	/** Translucent material with the Gaussian falloff (see Scripts/build_splat_material.py). Material slot 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSplatMaterial, Category = "PointForge|Splats")
	TObjectPtr<UMaterialInterface> SplatMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PointForge|Splats")
	int32 SplatCount = 0;

	UFUNCTION(BlueprintSetter) void SetUpAxis(EPFSplatUpAxis NewUpAxis);
	UFUNCTION(BlueprintSetter) void SetUnitScale(float NewUnitScale);
	UFUNCTION(BlueprintSetter) void SetMinSplatPixels(float NewMinSplatPixels);
	UFUNCTION(BlueprintSetter) void SetMaxSplatInstances(int32 NewMaxSplatInstances);
	UFUNCTION(BlueprintSetter) void SetMaxSplatScreenFraction(float NewFraction);
	UFUNCTION(BlueprintSetter) void SetUseGpuSplats(bool bNewUseGpuSplats);
	UFUNCTION(BlueprintSetter) void SetSplatMaterial(UMaterialInterface* NewMaterial);

	//~ UActorComponent / UPrimitiveComponent
	virtual void OnRegister() override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Bounds in component-local Unreal units (converted metres x UnitScale). */
	FBox GetLocalBoundsUU() const
	{
		return LocalBoundsM.IsValid ? FBox(LocalBoundsM.Min * UnitScale, LocalBoundsM.Max * UnitScale)
		                            : FBox(FVector(-1.0), FVector(1.0));
	}

	/** Parses a splat file and converts it to Unreal axes. Touches no UObject: safe on any thread. */
	static FPFSplatLoadResult ParseSplatFile(const FString& Path, EPFSplatUpAxis Axis);

private:
	/** Parses SplatFile if the path or up-axis changed since the last load. Game thread. */
	bool EnsureLoaded(bool bForce = false);
	/** Stores a successful result's data (no render-state changes, no events). */
	void StoreLoadResult(const FPFSplatLoadResult& Result);
	/** Store + rebuild render state + bounds + broadcast. Game thread. */
	void ApplyLoadResult(const FPFSplatLoadResult& Result);
	void RebuildRenderState();

	/**
	 * Bounds in converted splat-local metres. Saved with the level so the
	 * primitive is culled correctly even before (or without) the file loading.
	 */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "PointForge|Splats")
	FBox LocalBoundsM = FBox(ForceInit);

	/** 2nd..98th percentile bounds in converted metres (ignores floaters). Saved with the level. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "PointForge|Splats")
	FBox CoreBoundsM = FBox(ForceInit);

	/** Parsed records in Unreal axes (metres), shared read-only with GPU proxies. */
	TSharedPtr<const TArray<PFSplatGpuRecord>, ESPMode::ThreadSafe> GpuRecords;
	FString LoadedFile;
	EPFSplatUpAxis LoadedAxis = EPFSplatUpAxis::YDown;

	bool bAsyncLoading = false;
	uint32 LoadTicket = 0;     // latest async request wins
};
