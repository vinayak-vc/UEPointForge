#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PFConvertSettings.generated.h"

/**
 * Persistent pfconvert parameters. Auto-loaded from config on startup; SaveConfig
 * persists edits (survives restarts). The cache key includes these (see
 * KeyString) so changing a value re-converts instead of returning a stale octree.
 *
 * Density levers (the "Important" group): Spacing, LeafSize, MaxDepth — smaller
 * spacing / deeper max-depth = finer detail available when you zoom in.
 */
UCLASS(config = PointForge, defaultconfig, meta = (DisplayName = "PointForge Convert Settings"))
class POINTFORGEVIEWER_API UPFConvertSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ---- Important (affect detail / density) -------------------------------
	/** Root sample spacing in source units; 0 = auto (cubeSize/128). Smaller = denser. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Important", meta = (ClampMin = "0.0"))
	float Spacing = 0.0f;

	/** Target max points per leaf node. Higher = denser leaves (more detail). pfconvert default 50000. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Important", meta = (ClampMin = "1000", ClampMax = "2000000"))
	int32 LeafSize = 50000;

	/** Hard octree depth cap. Deeper = finer detail available when zoomed in. Default 24. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Important", meta = (ClampMin = "1", ClampMax = "30"))
	int32 MaxDepth = 24;

	// ---- Advanced ----------------------------------------------------------
	/** Coarse chunk grid depth L; grid is (2^L)^3. Range 0..10. Default 4. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ClampMin = "0", ClampMax = "10"))
	int32 ChunkDepth = 4;

	/** Chunker memory budget in points. Default 16M (~320 MB). Higher = more RAM, fewer flushes. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta = (ClampMin = "1000000", ClampMax = "268435456"))
	int64 FlushBudget = 16 * 1024 * 1024;

	/** Keep intermediate chunk files (debug). */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bKeepChunks = false;

	/** Verbose pfconvert logging. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bVerbose = false;

	/** Compress node payloads with zstd in octree.bin (reduces disk I/O, slight CPU cost). */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	bool bCompress = false;

	/** Last source file path the user converted — pre-fills the path box on open. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Advanced")
	FString LastSourceFile;

	/** Singleton (config-backed CDO). */
	static UPFConvertSettings* Get();

	/** Persist current values to config. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SaveSettings();

	/** Reset every value to the pfconvert defaults and persist. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void ResetToDefaults();

	/** pfconvert CLI args for these settings (NOT including the input/--out). */
	FString ToArgs() const;

	/** Stable key for the cache hash — only fields that change the output octree. */
	FString KeyString() const;
};
