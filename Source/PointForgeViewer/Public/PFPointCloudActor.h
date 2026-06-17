#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFConvert.h"          // EPFConvertState + FPFConvertHandlePtr
#include "PFPointCloudActor.generated.h"

class UPFPointCloudComponent;
class UPFViewerPanel;
class UPFConvertPanel;
class UUserWidget;
class UPostProcessComponent;
class UMaterialInstanceDynamic;

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

	//~ Convert progress + cancel -----------------------------------------------
	/** State of the most recent convert (Idle until LoadPointCloudFile is called). */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Convert")
	EPFConvertState GetConvertState() const;

	/** Human-readable phase / status string from pfconvert. Empty if no convert ran. */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Convert")
	FString GetConvertStatus() const;

	/** True iff a convert is in flight (state == Running). */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Convert")
	bool IsConverting() const;

	/** Request cancellation. Subprocess is terminated + the cache dir is wiped. */
	UFUNCTION(BlueprintCallable, Category = "PointForge|Convert")
	void CancelConvert();

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

	/** Unbound post-process component — hosts the EDL blendable (affects the full scene). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PointForge|EDL")
	TObjectPtr<UPostProcessComponent> PostProcess;

	// ---- EDL settings --------------------------------------------------------
	/** Enable Eye-Dome Lighting post-process (auto-loads M_EDL at play start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|EDL")
	bool bEnableEDL = true;

	/** EDL silhouette darkness (0.2 subtle – 2.0 strong). Default 0.5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|EDL",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float EDLStrength = 0.5f;

	/** EDL neighbor tap offset in pixels (1–3 typical). Default 1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PointForge|EDL",
		meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float EDLRadius = 1.0f;

	/** Update EDL strength + radius at runtime (pushes new values to the MID). */
	UFUNCTION(BlueprintCallable, Category = "PointForge|EDL")
	void SetEDLParams(float Strength, float Radius);

	UPROPERTY(Transient)
	TObjectPtr<UPFViewerPanel> Panel;

	UPROPERTY(Transient)
	TObjectPtr<UPFConvertPanel> ConvertPanel;

	/** The currently-active convert handle. Reset between calls to LoadPointCloudFile. */
	FPFConvertHandlePtr CurrentConvert;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EDLMid;

	void ApplyEDL();
};
