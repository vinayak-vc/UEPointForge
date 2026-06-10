#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFPointCloudActor.generated.h"

class UPFPointCloudComponent;

/**
 * Drop-in actor for the convert-once-then-stream workflow:
 *
 *   LoadPointCloudFile("D:/scans/site.laz")
 *     -> first time:  pfconvert builds an octree (cached by file hash) on a
 *                     worker thread, then streams it.
 *     -> after that:  cache hit, streams in seconds.
 */
UCLASS()
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PointForge")
	TObjectPtr<UPFPointCloudComponent> PointCloud;
};
