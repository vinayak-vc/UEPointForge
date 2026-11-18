// Copyright. PointForge Viewer — Gaussian splat component (billboard path).

#include "PFSplatComponent.h"
#include "PFSplatSceneProxy.h"
#include "PFNativeApi.h"

#include "Materials/MaterialInterface.h"

UPFSplatComponent::UPFSplatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bUseAttachParentBound = false;
}

bool UPFSplatComponent::LoadSplatFile(const FString& Path)
{
	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!Api.IsLoaded() && !Api.Load())
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: PointForgeNative.dll not available"));
		return false;
	}

	// Open once to read the bounds. The render proxy opens its own handle, so the
	// file is read twice on load. This keeps ownership simple: the proxy owns its
	// handle and closes it on the render thread.
	FTCHARToUTF8 Utf8(*Path);
	PFSplatCloud* Cloud = Api.Splat_Open(Utf8.Get());
	if (!Cloud)
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: could not open %s"), *Path);
		SplatCount = 0;
		return false;
	}

	PFSplatInfo Info;
	FMemory::Memzero(&Info, sizeof(Info));
	const bool bOk = Api.Splat_GetInfo(Cloud, &Info) == 1;
	Api.Splat_Close(Cloud);
	if (!bOk)
	{
		return false;
	}

	SplatFile = Path;
	SplatCount = static_cast<int32>(Info.count);
	LocalBoundsM = FBox(
		FVector(Info.boundsMin[0], Info.boundsMin[1], Info.boundsMin[2]),
		FVector(Info.boundsMax[0], Info.boundsMax[1], Info.boundsMax[2]));

	MarkRenderStateDirty();   // proxy is recreated with the new file
	UpdateBounds();
	return true;
}

FPrimitiveSceneProxy* UPFSplatComponent::CreateSceneProxy()
{
	PFSplatCloud* Cloud = nullptr;
	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!SplatFile.IsEmpty() && (Api.IsLoaded() || Api.Load()))
	{
		FTCHARToUTF8 Utf8(*SplatFile);
		Cloud = Api.Splat_Open(Utf8.Get());   // owned by the proxy from here on
	}
	// A null cloud gives a proxy that draws nothing, which is safer than failing here.
	return new FPFSplatSceneProxy(this, Cloud);
}

FBoxSphereBounds UPFSplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Local bounds are in metres; the component's local space is in Unreal units.
	FBox Box = LocalBoundsM.IsValid ? LocalBoundsM : FBox(FVector(-1.0), FVector(1.0));
	const FBox Scaled(Box.Min * UnitScale, Box.Max * UnitScale);
	return FBoxSphereBounds(Scaled).TransformBy(LocalToWorld);
}
