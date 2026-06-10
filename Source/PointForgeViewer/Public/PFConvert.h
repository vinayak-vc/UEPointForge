#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_OneParam(FPFConvertDone, bool /*bSuccess*/);

/**
 * Convert-once-then-stream cache.
 *
 * Maps a source point-cloud file to a cached PointForge octree directory under
 * <ProjectSaved>/PointForgeCache/<hash>. First open runs the (slow) conversion;
 * later opens of the same file find the cache and stream instantly.
 */
class POINTFORGEVIEWER_API FPFConvert
{
public:
	/** Stable cache dir for a source file (hash of path + size + mtime; no full-file hash). */
	static FString GetCacheDirFor(const FString& SourceFile);

	/** True if the cache dir already holds a converted octree (meta.bin present). */
	static bool IsConverted(const FString& CacheDir);

	/** Locate pfconvert.exe: Override -> plugin Binaries/ThirdParty -> PointForge build/Release. */
	static FString LocatePfConvert(const FString& Override = FString());

	/**
	 * Converts SourceFile if not already cached. Conversion runs on a background
	 * thread; OnDone fires on the GAME thread with success/failure. If already
	 * cached, OnDone(true) is dispatched (still async) immediately.
	 */
	static void ConvertAsync(const FString& SourceFile, const FString& PfConvertExe, FPFConvertDone OnDone);
};
