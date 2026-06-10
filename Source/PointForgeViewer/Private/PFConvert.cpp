#include "PFConvert.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

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

	const FString Key = FString::Printf(TEXT("%s|%lld|%lld"),
		*SourceFile, Size, Stamp.GetTicks());
	const FString Hash = FMD5::HashAnsiString(*Key);

	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PointForgeCache"), Hash);
}

bool FPFConvert::IsConverted(const FString& CacheDir)
{
	return FPaths::FileExists(FPaths::Combine(CacheDir, TEXT("meta.bin")));
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

	Async(EAsyncExecution::Thread, [SourceFile, PfConvertExe, CacheDir, OnDone]()
	{
		IFileManager::Get().MakeDirectory(*CacheDir, /*Tree*/ true);

		bool bOk = false;

#if PF_LINK_PFCORE
		{
			// In-process convert via pfcore. Defaults mirror the pfconvert CLI.
			pf::IndexOptions Opts;
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
			const FString Args = FString::Printf(TEXT("\"%s\" --out \"%s\""), *SourceFile, *CacheDir);
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
