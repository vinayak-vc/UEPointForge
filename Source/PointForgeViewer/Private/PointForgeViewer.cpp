#include "PointForgeViewer.h"
#include "PFSplatGpuSceneProxy.h"   // PFSplatGpu_EnsureViewExtension / ReleaseViewExtension
#include "Engine/Engine.h"           // GEngine
#include "Misc/CoreDelegates.h"      // OnPostEngineInit
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#define LOCTEXT_NAMESPACE "FPointForgeViewerModule"

static void* ZstdDllHandle = nullptr;

void FPointForgeViewerModule::StartupModule()
{
	// GPU splats: register the view extension on the game thread once the
	// engine exists (scene proxies may be created on worker threads later).
	if (GEngine)
	{
		PFSplatGpu_EnsureViewExtension();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddLambda([]() { PFSplatGpu_EnsureViewExtension(); });
	}

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
	PFSplatGpu_ReleaseViewExtension();

	if (ZstdDllHandle)
	{
		FPlatformProcess::FreeDllHandle(ZstdDllHandle);
		ZstdDllHandle = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPointForgeViewerModule, PointForgeViewer)
