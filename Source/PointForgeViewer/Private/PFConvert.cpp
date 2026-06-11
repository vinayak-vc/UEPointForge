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

void FPFConvert::ConvertAsync(const FString& SourceFile, const FString& PfConvertExe, FPFConvertDone OnDone)
{
	const FString CacheDir = GetCacheDirFor(SourceFile);

	if (IsConverted(CacheDir))
	{
		AsyncTask(ENamedThreads::GameThread, [OnDone]() { OnDone.ExecuteIfBound(true); });
		return;
	}

	// Snapshot convert params on the game thread (captured into the worker).
	const UPFConvertSettings* S = UPFConvertSettings::Get();
	const FString SettingsArgs = S->ToArgs();
	const int32  OptChunkDepth = S->ChunkDepth;
	const double OptSpacing = static_cast<double>(S->Spacing);
	const uint32 OptLeaf = static_cast<uint32>(S->LeafSize);
	const int32  OptMaxDepth = S->MaxDepth;
	const uint64 OptFlush = static_cast<uint64>(S->FlushBudget);
	const bool   OptKeepChunks = S->bKeepChunks;

	Async(EAsyncExecution::Thread, [SourceFile, PfConvertExe, CacheDir, OnDone, SettingsArgs,
		OptChunkDepth, OptSpacing, OptLeaf, OptMaxDepth, OptFlush, OptKeepChunks]()
	{
		IFileManager::Get().MakeDirectory(*CacheDir, /*Tree*/ true);

		bool bOk = false;

#if PF_LINK_PFCORE
		{
			// In-process convert via pfcore, using the panel's params.
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
		}
		else
		{
			const FString Args = FString::Printf(TEXT("\"%s\" --out \"%s\"%s"), *SourceFile, *CacheDir, *SettingsArgs);
			UE_LOG(LogPointForgeConvert, Log, TEXT("ConvertAsync: %s %s"), *PfConvertExe, *Args);

			int32 RetCode = -1;
			FProcHandle Proc = FPlatformProcess::CreateProc(
				*PfConvertExe, *Args,
				/*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
				/*OutProcessID*/ nullptr, /*PriorityModifier*/ 0,
				/*OptionalWorkingDirectory*/ nullptr, /*PipeWriteChild*/ nullptr);

			if (Proc.IsValid())
			{
				FPlatformProcess::WaitForProc(Proc);
				FPlatformProcess::GetProcReturnCode(Proc, &RetCode);
				FPlatformProcess::CloseProc(Proc);
				bOk = (RetCode == 0);
				UE_LOG(LogPointForgeConvert, Log, TEXT("ConvertAsync: pfconvert exited %d"), RetCode);
			}
			else
			{
				UE_LOG(LogPointForgeConvert, Error, TEXT("ConvertAsync: failed to launch pfconvert.exe"));
			}
		}
#endif

		bOk = bOk && IsConverted(CacheDir);
		AsyncTask(ENamedThreads::GameThread, [OnDone, bOk]() { OnDone.ExecuteIfBound(bOk); });
	});
}
