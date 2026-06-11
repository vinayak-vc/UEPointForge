#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFPointCloudActor.generated.h"

class UPFPointCloudComponent;
class UPFViewerPanel;
class UPFConvertPanel;
class UUserWidget;

/**
 * Drop-in actor for the convert-once-then-stream workflow:
 *
 *   LoadPointCloudFile("D:/scans/site.laz")
 *     -> first time:  pfconvert builds an octree (cached by file hash) on a
 *                     worker thread, then streams it.
 *     -> after that:  cache hit, streams in seconds.
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup = "PointForge",
	meta = (DisplayName = "PointForge Point Cloud"))
class POINTFORGEVIEWER_API APFPointCloudActor : public AActor
{
	GENERATED_BODY()

public:
	APFPointCloudActor();

	/** Convert (if needed) then display. Returns immediately; loads asynchronously. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void LoadPointCloudFile(const FString& SourceFile);

	/** Optional explicit path to pfconvert.exe. Empty => auto-locate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	FString PfConvertExePath;

	/** Auto-show the viewer panel when a cloud finishes loading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	bool bShowPanel = true;

	/** Panel widget class (defaults to the built-in UPFViewerPanel; override with a BP). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	TSubclassOf<UUserWidget> PanelClass;

	/** Create + show the viewer panel (runtime/PIE only). */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void ShowPanel();

	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void HidePanel();
	
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void HideConvertPanel();

	/** Show the conversion settings panel (set params + path, then Convert). */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void ShowConvertPanel();

	/** Cache size (MB) of every PointForge cache under the source file's directory. */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Cache")
	float GetCacheSizeMB(const FString& SourceFile) const;

	/** Delete the cache for this source file (this convert only). */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Cache")
	bool ClearCacheForFile(const FString& SourceFile);

	/** Delete every PointForgeCache subdir under the source file's directory. */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Cache")
	int32 ClearAllCachesForDir(const FString& SourceFile);

	/** Convert-panel widget class (defaults to the built-in UPFConvertPanel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge")
	TSubclassOf<UUserWidget> ConvertPanelClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PointForge")
	TObjectPtr<UPFPointCloudComponent> PointCloud;

	UPROPERTY(Transient)
	TObjectPtr<UPFViewerPanel> Panel;

	UPROPERTY(Transient)
	TObjectPtr<UPFConvertPanel> ConvertPanel;
};
