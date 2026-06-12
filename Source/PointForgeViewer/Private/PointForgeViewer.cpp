#include "PointForgeViewer.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FPointForgeViewerModule"

static void* ZstdDllHandle = nullptr;

void FPointForgeViewerModule::StartupModule()
{
	// Load delay-loaded zstd.dll dependency
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PointForgeViewer"));
	if (Plugin.IsValid())
	{
		FString DllPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/Win64/zstd.dll"));
		if (FPaths::FileExists(DllPath))
		{
			ZstdDllHandle = FPlatformProcess::GetDllHandle(*DllPath);
		}
	}
}

void FPointForgeViewerModule::ShutdownModule()
{
	if (ZstdDllHandle)
	{
		FPlatformProcess::FreeDllHandle(ZstdDllHandle);
		ZstdDllHandle = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPointForgeViewerModule, PointForgeViewer)
