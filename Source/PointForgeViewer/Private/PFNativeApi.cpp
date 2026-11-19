// Copyright. PointForge Viewer — runtime loader for PointForgeNative.dll.

#include "PFNativeApi.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

namespace
{
	/** Resolves one export; returns nullptr (and logs) if the symbol is missing. */
	template <typename FnType>
	FnType ResolveExport(void* Handle, const TCHAR* Name)
	{
		void* Sym = FPlatformProcess::GetDllExport(Handle, Name);
		if (!Sym)
		{
			UE_LOG(LogTemp, Error, TEXT("PointForgeNative: missing export %s"), Name);
		}
		return reinterpret_cast<FnType>(Sym);
	}
}

FPFNativeApi& FPFNativeApi::Get()
{
	static FPFNativeApi Instance;
	return Instance;
}

bool FPFNativeApi::Load(const FString& DllPath)
{
	if (DllHandle)
	{
		return true;
	}

	FString Path = DllPath;
	if (Path.IsEmpty())
	{
		// Bundled by PointForgeViewer.Build.cs into Binaries/ThirdParty.
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PointForgeViewer"));
		if (!Plugin.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("PointForgeNative: PointForgeViewer plugin not found"));
			return false;
		}
		Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/ThirdParty/PointForgeNative.dll"));
	}

	DllHandle = FPlatformProcess::GetDllHandle(*Path);
	if (!DllHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("PointForgeNative: could not load %s"), *Path);
		return false;
	}

	Splat_Open             = ResolveExport<PF_Splat_Open_Fn>(DllHandle, TEXT("PF_Splat_Open"));
	Splat_Close            = ResolveExport<PF_Splat_Close_Fn>(DllHandle, TEXT("PF_Splat_Close"));
	Splat_GetInfo          = ResolveExport<PF_Splat_GetInfo_Fn>(DllHandle, TEXT("PF_Splat_GetInfo"));
	Splat_Update           = ResolveExport<PF_Splat_Update_Fn>(DllHandle, TEXT("PF_Splat_Update"));
	Splat_LastVisibleCount = ResolveExport<PF_Splat_LastVisibleCount_Fn>(DllHandle, TEXT("PF_Splat_LastVisibleCount"));
	GetVersion             = ResolveExport<PF_GetVersion_Fn>(DllHandle, TEXT("PF_GetVersion"));
	// Optional: absent in older DLLs (the GPU splat path then stays off).
	Splat_CopyGpuRecords   = ResolveExport<PF_Splat_CopyGpuRecords_Fn>(DllHandle, TEXT("PF_Splat_CopyGpuRecords"));

	const bool bAllResolved = Splat_Open && Splat_Close && Splat_GetInfo && Splat_Update
		&& Splat_LastVisibleCount && GetVersion;
	if (!bAllResolved)
	{
		Unload();
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("PointForgeNative loaded (API version %u) from %s"), GetVersion(), *Path);
	return true;
}

void FPFNativeApi::Unload()
{
	if (DllHandle)
	{
		FPlatformProcess::FreeDllHandle(DllHandle);
		DllHandle = nullptr;
	}
	Splat_Open = nullptr;
	Splat_Close = nullptr;
	Splat_GetInfo = nullptr;
	Splat_Update = nullptr;
	Splat_LastVisibleCount = nullptr;
	Splat_CopyGpuRecords = nullptr;
	GetVersion = nullptr;
}
