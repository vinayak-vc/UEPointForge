#include "PFConvert.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PFConvertSettings.h"

#if PF_LINK_PFCORE
// Declares pf::buildOctree / pf::IndexOptions. Include path added in Build.cs
// only when bLinkPfcoreInProcess is on.
#include "indexer/OctreeIndexer.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogPointForgeConvert, Log, All);

FString FPFConvert::GetCacheDirFor(const FString& SourceFile)
{
	IFileManager& FM = IFileManager::Get();
	const int64 Size = FM.FileSize(*SourceFile);
	const FDateTime Stamp = FM.GetTimeStamp(*SourceFile);

	// Include the convert params in the key so changing spacing/leaf/etc. produces
	// a different cache dir (re-convert) instead of returning a stale octree.
	// Path is converted to absolute first so a relative input doesn't collide with
	// an identically-named absolute one.
	const FString FullSource = FPaths::ConvertRelativePathToFull(SourceFile);
	const FString Key = FString::Printf(TEXT("%s|%lld|%lld|%s"),
		*FullSource, Size, Stamp.GetTicks(), *UPFConvertSettings::Get()->KeyString());
	const FString Hash = FMD5::HashAnsiString(*Key);

	return FPaths::Combine(FPaths::GetPath(FullSource), TEXT("PointForgeCache"), Hash);
}

bool FPFConvert::IsConverted(const FString& CacheDir)
{
	return FPaths::FileExists(FPaths::Combine(CacheDir, TEXT("meta.bin")));
}

int64 FPFConvert::GetCacheSizeBytes(const FString& SourceFile)
{
	const FString Root = FPaths::Combine(
		FPaths::GetPath(FPaths::ConvertRelativePathToFull(SourceFile)),
		TEXT("PointForgeCache"));
	if (!FPaths::DirectoryExists(Root))
	{
		return 0;
	}
	int64 Total = 0;
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false, false);
	for (const FString& F : Files)
	{
		Total += IFileManager::Get().FileSize(*F);
	}
	return Total;
}

bool FPFConvert::ClearCacheFor(const FString& SourceFile)
{
	const FString Dir = GetCacheDirFor(SourceFile);
	if (!FPaths::DirectoryExists(Dir))
	{
		return false;
	}
	const bool bOk = IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	UE_LOG(LogPointForgeConvert, Log, TEXT("ClearCacheFor: %s -> %s"), *Dir, bOk ? TEXT("OK") : TEXT("FAILED"));
	return bOk;
}

int32 FPFConvert::ClearAllCachesUnderSource(const FString& SourceFile)
{
	const FString Root = FPaths::Combine(
		FPaths::GetPath(FPaths::ConvertRelativePathToFull(SourceFile)),
		TEXT("PointForgeCache"));
	if (!FPaths::DirectoryExists(Root))
	{
		return 0;
	}
	TArray<FString> SubDirs;
	IFileManager::Get().FindFiles(SubDirs, *(Root / TEXT("*")), /*Files*/ false, /*Dirs*/ true);
	int32 Removed = 0;
	for (const FString& Sub : SubDirs)
	{
		const FString Full = FPaths::Combine(Root, Sub);
		if (IFileManager::Get().DeleteDirectory(*Full, false, true))
		{
			++Removed;
		}
	}
	UE_LOG(LogPointForgeConvert, Log, TEXT("ClearAllCachesUnderSource: removed %d dirs under %s"), Removed, *Root);
	return Removed;
}

FString FPFConvert::LocatePfConvert(const FString& Override)
{
	if (!Override.IsEmpty() && FPaths::FileExists(Override))
	{
		return Override;
	}

	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PointForgeViewer")))
	{
		const FString Bundled = FPaths::Combine(Plugin->GetBaseDir(),
			TEXT("Binaries"), TEXT("ThirdParty"), TEXT("pfconvert.exe"));
		if (FPaths::FileExists(Bundled))
		{
			return Bundled;
		}
	}

	const FString Fallback = TEXT("C:/UnrealProject/PointForge/build/Release/pfconvert.exe");
	return FPaths::FileExists(Fallback) ? Fallback : FString();
}

// Parse a chunk of pfconvert stdout and refine the handle's status text. Looks
// for phase markers (the indexer logs "Phase A/B/C", "chunking", "indexing",
// "writing meta"). Cheap regex-free substring scan.
static void ParseConvertStatus(const FString& Chunk, FPFConvertHandle& Handle)
{
	if (Chunk.IsEmpty()) { return; }
	FString Status;
	const FString L = Chunk.ToLower();
	if (L.Contains(TEXT("phase a")) || L.Contains(TEXT("counting"))) { Status = TEXT("Counting points"); }
	else if (L.Contains(TEXT("phase b")) || L.Contains(TEXT("chunk")))    { Status = TEXT("Chunking"); }
	else if (L.Contains(TEXT("phase c")) || L.Contains(TEXT("index")))    { Status = TEXT("Building octree"); }
	else if (L.Contains(TEXT("writing meta")) || L.Contains(TEXT("meta.bin"))) { Status = TEXT("Writing metadata"); }
	else if (L.Contains(TEXT("error"))) { Status = TEXT("Error — check log"); }
	if (!Status.IsEmpty())
	{
		Handle.SetStatus(Status);
	}
}

FPFConvertHandlePtr FPFConvert::ConvertAsync(const FString& SourceFile, const FString& PfConvertExe, FPFConvertDone OnDone)
{
	FPFConvertHandlePtr Handle = MakeShared<FPFConvertHandle, ESPMode::ThreadSafe>();
	const FString CacheDir = GetCacheDirFor(SourceFile);

	if (IsConverted(CacheDir))
	{
		Handle->State.store(EPFConvertState::Done);
		Handle->SetStatus(TEXT("Cache hit"));
		AsyncTask(ENamedThreads::GameThread, [OnDone]() { OnDone.ExecuteIfBound(true); });
		return Handle;
	}

	Handle->State.store(EPFConvertState::Running);
	Handle->SetStatus(TEXT("Starting..."));

	// Snapshot convert params on the game thread (captured into the worker).
	const UPFConvertSettings* S = UPFConvertSettings::Get();
	const FString SettingsArgs = S->ToArgs();
	const int32  OptChunkDepth = S->ChunkDepth;
	const double OptSpacing = static_cast<double>(S->Spacing);
	const uint32 OptLeaf = static_cast<uint32>(S->LeafSize);
	const int32  OptMaxDepth = S->MaxDepth;
	const uint64 OptFlush = static_cast<uint64>(S->FlushBudget);
	const bool   OptKeepChunks = S->bKeepChunks;

	Async(EAsyncExecution::Thread, [SourceFile, PfConvertExe, CacheDir, OnDone, SettingsArgs, Handle,
		OptChunkDepth, OptSpacing, OptLeaf, OptMaxDepth, OptFlush, OptKeepChunks]()
	{
		IFileManager::Get().MakeDirectory(*CacheDir, /*Tree*/ true);

		bool bOk = false;
		bool bCancelled = false;

#if PF_LINK_PFCORE
		{
			// In-process convert via pfcore. Cancel isn't cooperative here — the
			// call blocks until done. Cancel only works in the subprocess path.
			pf::IndexOptions Opts;
			Opts.gridDepth = OptChunkDepth;
			Opts.rootSpacing = OptSpacing;
			Opts.targetLeafSize = OptLeaf;
			Opts.maxDepth = OptMaxDepth;
			Opts.flushBudget = OptFlush;
			Opts.keepChunks = OptKeepChunks;
			bOk = pf::buildOctree(TCHAR_TO_UTF8(*SourceFile), TCHAR_TO_UTF8(*CacheDir), Opts);
		}
#else
		if (PfConvertExe.IsEmpty())
		{
			UE_LOG(LogPointForgeConvert, Error,
				TEXT("ConvertAsync: pfconvert.exe not found (set PfConvertExePath or bundle it in Binaries/ThirdParty)."));
			Handle->SetStatus(TEXT("pfconvert.exe not found"));
		}
		else
		{
			const FString Args = FString::Printf(TEXT("\"%s\" --out \"%s\"%s"), *SourceFile, *CacheDir, *SettingsArgs);
			UE_LOG(LogPointForgeConvert, Log, TEXT("ConvertAsync: %s %s"), *PfConvertExe, *Args);

			// Read pipe captures pfconvert stdout so we can parse phase markers
			// and (eventually) progress percentages.
			void* PipeRead = nullptr;
			void* PipeWrite = nullptr;
			if (!FPlatformProcess::CreatePipe(PipeRead, PipeWrite))
			{
				PipeRead = nullptr; PipeWrite = nullptr;
			}

			int32 RetCode = -1;
			FProcHandle Proc = FPlatformProcess::CreateProc(
				*PfConvertExe, *Args,
				/*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
				/*OutProcessID*/ nullptr, /*PriorityModifier*/ 0,
				/*OptionalWorkingDirectory*/ nullptr, /*PipeWriteChild*/ PipeWrite);

			if (Proc.IsValid())
			{
				// Poll: read pipe, parse phase, check cancel, sleep briefly.
				while (FPlatformProcess::IsProcRunning(Proc))
				{
					if (Handle->bCancelRequested.load())
					{
						FPlatformProcess::TerminateProc(Proc, /*bKillTree*/ true);
						bCancelled = true;
						break;
					}
					if (PipeRead)
					{
						const FString Chunk = FPlatformProcess::ReadPipe(PipeRead);
						if (!Chunk.IsEmpty())
						{
							ParseConvertStatus(Chunk, *Handle);
							UE_LOG(LogPointForgeConvert, Log, TEXT("[pfconvert] %s"), *Chunk.TrimStartAndEnd());
						}
					}
					FPlatformProcess::Sleep(0.1f);
				}
				// Drain anything left in the pipe.
				if (PipeRead)
				{
					const FString Tail = FPlatformProcess::ReadPipe(PipeRead);
					if (!Tail.IsEmpty())
					{
						ParseConvertStatus(Tail, *Handle);
						UE_LOG(LogPointForgeConvert, Log, TEXT("[pfconvert] %s"), *Tail.TrimStartAndEnd());
					}
				}
				FPlatformProcess::GetProcReturnCode(Proc, &RetCode);
				FPlatformProcess::CloseProc(Proc);
				bOk = !bCancelled && (RetCode == 0);
				UE_LOG(LogPointForgeConvert, Log, TEXT("ConvertAsync: pfconvert exited %d%s"), RetCode,
					bCancelled ? TEXT(" (cancelled)") : TEXT(""));
			}
			else
			{
				UE_LOG(LogPointForgeConvert, Error, TEXT("ConvertAsync: failed to launch pfconvert.exe"));
				Handle->SetStatus(TEXT("Failed to launch pfconvert.exe"));
			}

			if (PipeRead || PipeWrite)
			{
				FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			}
		}
#endif

		// On cancel: wipe the half-written cache dir.
		if (bCancelled)
		{
			IFileManager::Get().DeleteDirectory(*CacheDir, /*RequireExists*/ false, /*Tree*/ true);
			Handle->SetStatus(TEXT("Cancelled"));
		}

		bOk = bOk && IsConverted(CacheDir);

		// Final state.
		if (bCancelled)        { Handle->State.store(EPFConvertState::Cancelled); }
		else if (bOk)          { Handle->State.store(EPFConvertState::Done);
		                          Handle->SetStatus(TEXT("Done")); }
		else                   { Handle->State.store(EPFConvertState::Failed);
		                          if (Handle->GetStatus().IsEmpty()) { Handle->SetStatus(TEXT("Failed")); } }

		AsyncTask(ENamedThreads::GameThread, [OnDone, bOk]() { OnDone.ExecuteIfBound(bOk); });
	});

	return Handle;
}
