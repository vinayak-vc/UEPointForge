#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>

/** Lifecycle of an in-flight convert. */
UENUM(BlueprintType)
enum class EPFConvertState : uint8
{
	Idle       UMETA(DisplayName = "Idle"),
	Running    UMETA(DisplayName = "Running"),
	Done       UMETA(DisplayName = "Done"),
	Failed     UMETA(DisplayName = "Failed"),
	Cancelled  UMETA(DisplayName = "Cancelled"),
};

/**
 * Live handle to an in-flight convert. The worker writes state/status; the game
 * thread reads them for the UI. Cancel = set bCancelRequested; the worker kills
 * the pfconvert subprocess and wipes the half-written cache dir.
 */
struct POINTFORGEVIEWER_API FPFConvertHandle
{
	std::atomic<EPFConvertState> State{ EPFConvertState::Idle };
	std::atomic<bool> bCancelRequested{ false };

	/** Latest phase string parsed from pfconvert stdout (e.g. "Counting", "Chunking", "Indexing"). */
	mutable FCriticalSection StatusCS;
	FString StatusText;

	void SetStatus(const FString& In)
	{
		FScopeLock Lock(&StatusCS);
		StatusText = In;
	}
	FString GetStatus() const
	{
		FScopeLock Lock(&StatusCS);
		return StatusText;
	}

	/** 0–1 progress estimate updated by ParseConvertStatus. */
	std::atomic<float> Progress{0.f};

	mutable FCriticalSection LogCS;
	TArray<FString> LogLines;

	void AppendLog(const FString& Line)
	{
		FScopeLock Lock(&LogCS);
		LogLines.Add(Line);
	}

	TArray<FString> GetLogLines() const
	{
		FScopeLock Lock(&LogCS);
		return LogLines;
	}
};
using FPFConvertHandlePtr = TSharedPtr<FPFConvertHandle, ESPMode::ThreadSafe>;

DECLARE_DELEGATE_OneParam(FPFConvertDone, bool /*bSuccess*/);

/**
 * Convert-once-then-stream cache.
 *
 * Maps a source point-cloud file to a cached PointForge octree directory under
 * <sourceDir>/PointForgeCache/<hash>. First open runs the (slow) conversion;
 * later opens of the same file find the cache and stream instantly.
 */
class POINTFORGEVIEWER_API FPFConvert
{
public:
	/** Stable cache dir for a source file (hash of path + size + mtime + convert params). */
	static FString GetCacheDirFor(const FString& SourceFile);

	/** True if the cache dir already holds a converted octree (meta.bin present). */
	static bool IsConverted(const FString& CacheDir);

	/** Total bytes used by ALL cached converts under <sourceDir>/PointForgeCache. */
	static int64 GetCacheSizeBytes(const FString& SourceFile);

	/** Delete the cache dir for this source file (this convert only). Returns true on success. */
	static bool ClearCacheFor(const FString& SourceFile);

	/** Delete every cache directory under <sourceDir>/PointForgeCache. */
	static int32 ClearAllCachesUnderSource(const FString& SourceFile);

	/** Locate pfconvert.exe: Override -> plugin Binaries/ThirdParty -> PointForge build/Release. */
	static FString LocatePfConvert(const FString& Override = FString());

	/**
	 * Converts SourceFile if not already cached. Returns a handle the caller can
	 * poll for state/status or cancel via handle->bCancelRequested = true.
	 *
	 * - If already cached, returns a handle that is immediately in Done state and
	 *   OnDone(true) is dispatched on the game thread asynchronously.
	 * - Otherwise spawns pfconvert.exe with a read pipe, parses stdout for phase
	 *   markers, updates the handle. On completion, OnDone fires on game thread.
	 * - On cancel: terminates the subprocess + wipes the half-written cache dir.
	 */
	static FPFConvertHandlePtr ConvertAsync(const FString& SourceFile, const FString& PfConvertExe, FPFConvertDone OnDone);
};
