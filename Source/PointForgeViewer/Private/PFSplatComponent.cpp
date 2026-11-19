// Copyright. PointForge Viewer — Gaussian splat component.

#include "PFSplatComponent.h"
#include "PFSplatSceneProxy.h"
#include "PFSplatGpuSceneProxy.h"
#include "PFSplatGpuShaders.h"
#include "PFNativeApi.h"
#include "SceneInterface.h"

#include "Async/Async.h"
#include "Materials/MaterialInterface.h"

#include <algorithm>

namespace
{
	/**
	 * Row-major 3x3 A with Unreal = A * File. Each option maps a right-handed
	 * file frame to Unreal's left-handed (X forward, Y right, Z up) frame, so
	 * det(A) = -1 (handedness flip) except AsStored.
	 */
	void AxisMatrix(EPFSplatUpAxis Axis, float A[3][3])
	{
		const float Id[3][3] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
		const float YDown[3][3] = { {0, 0, 1}, {1, 0, 0}, {0, -1, 0} };   // X = z, Y = x, Z = -y
		const float YUp[3][3]   = { {0, 0, -1}, {1, 0, 0}, {0, 1, 0} };   // X = -z, Y = x, Z = y
		const float ZUp[3][3]   = { {1, 0, 0}, {0, -1, 0}, {0, 0, 1} };   // X = x, Y = -y, Z = z
		const float (*Src)[3] = Id;
		switch (Axis)
		{
		case EPFSplatUpAxis::YDown: Src = YDown; break;
		case EPFSplatUpAxis::YUp:   Src = YUp;   break;
		case EPFSplatUpAxis::ZUp:   Src = ZUp;   break;
		default: break;
		}
		FMemory::Memcpy(A, Src, sizeof(float) * 9);
	}

	/** Position p' = A p, covariance S' = A S A^T, in place. */
	void ConvertRecord(PFSplatGpuRecord& R, const float A[3][3])
	{
		const float P[3] = { R.position[0], R.position[1], R.position[2] };
		for (int32 i = 0; i < 3; ++i)
		{
			R.position[i] = A[i][0] * P[0] + A[i][1] * P[1] + A[i][2] * P[2];
		}

		const float* s = R.sigma;   // s00 s01 s02 s11 s12 s22
		const float S[3][3] = { {s[0], s[1], s[2]}, {s[1], s[3], s[4]}, {s[2], s[4], s[5]} };
		float AS[3][3];
		for (int32 i = 0; i < 3; ++i)
			for (int32 j = 0; j < 3; ++j)
				AS[i][j] = A[i][0] * S[0][j] + A[i][1] * S[1][j] + A[i][2] * S[2][j];
		float Out[3][3];
		for (int32 i = 0; i < 3; ++i)
			for (int32 j = 0; j < 3; ++j)
				Out[i][j] = AS[i][0] * A[j][0] + AS[i][1] * A[j][1] + AS[i][2] * A[j][2];
		R.sigma[0] = Out[0][0]; R.sigma[1] = Out[0][1]; R.sigma[2] = Out[0][2];
		R.sigma[3] = Out[1][1]; R.sigma[4] = Out[1][2]; R.sigma[5] = Out[2][2];
	}

	/** Value at fraction Q (0..1) of V; reorders V. */
	float Percentile(TArray<float>& V, double Q)
	{
		const int32 K = FMath::Clamp(static_cast<int32>(Q * (V.Num() - 1)), 0, V.Num() - 1);
		std::nth_element(V.GetData(), V.GetData() + K, V.GetData() + V.Num());
		return V[K];
	}
}

UPFSplatComponent::UPFSplatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bUseAttachParentBound = false;
}

// ---- Parsing (any thread) ----------------------------------------------------------

FPFSplatLoadResult UPFSplatComponent::ParseSplatFile(const FString& Path, EPFSplatUpAxis Axis)
{
	FPFSplatLoadResult Result;
	Result.Path = Path;
	Result.Axis = Axis;
	if (Path.IsEmpty())
	{
		return Result;
	}

	// The DLL must already be loaded (callers load it on the game thread first).
	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!Api.IsLoaded())
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: PointForgeNative.dll not loaded"));
		return Result;
	}

	FTCHARToUTF8 Utf8(*Path);
	PFSplatCloud* Cloud = Api.Splat_Open(Utf8.Get());
	if (!Cloud)
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: could not open %s"), *Path);
		return Result;
	}

	PFSplatInfo Info;
	FMemory::Memzero(&Info, sizeof(Info));
	const bool bInfo = Api.Splat_GetInfo(Cloud, &Info) == 1;
	const int64 Count = bInfo ? static_cast<int64>(Info.count) : 0;

	// The GPU buffers are indexed with 32-bit sizes; stay well inside that.
	if (Api.Splat_CopyGpuRecords && Count > 0 && Count <= 64 * 1024 * 1024)
	{
		Result.Records = MakeShared<TArray<PFSplatGpuRecord>, ESPMode::ThreadSafe>();
		Result.Records->SetNumUninitialized(static_cast<int32>(Count));
		const int64 Copied = Api.Splat_CopyGpuRecords(Cloud, Result.Records->GetData(), Count);
		Result.Records->SetNum(static_cast<int32>(FMath::Max<int64>(Copied, 0)));
	}
	Api.Splat_Close(Cloud);

	if (Result.Records.IsValid() && Result.Records->Num() > 0)
	{
		// Convert to Unreal axes once; bounds come from the data itself.
		float A[3][3];
		AxisMatrix(Axis, A);
		const int32 N = Result.Records->Num();
		TArray<float> Axes[3];
		for (TArray<float>& V : Axes) { V.SetNumUninitialized(N); }
		int32 i = 0;
		for (PFSplatGpuRecord& R : *Result.Records)
		{
			ConvertRecord(R, A);
			const FVector P(R.position[0], R.position[1], R.position[2]);
			Result.BoundsM += P;
			Axes[0][i] = R.position[0]; Axes[1][i] = R.position[1]; Axes[2][i] = R.position[2];
			++i;
		}
		FVector Lo, Hi;
		for (int32 a = 0; a < 3; ++a)
		{
			Lo[a] = Percentile(Axes[a], 0.02);
			Hi[a] = Percentile(Axes[a], 0.98);
		}
		Result.CoreBoundsM = FBox(Lo, Hi);
		Result.Count = N;
		Result.bOk = true;
	}
	else if (bInfo && Count > 0)
	{
		// Old DLL (no record export) or too large: CPU path only, header bounds, unconverted.
		Result.BoundsM = FBox(
			FVector(Info.boundsMin[0], Info.boundsMin[1], Info.boundsMin[2]),
			FVector(Info.boundsMax[0], Info.boundsMax[1], Info.boundsMax[2]));
		Result.CoreBoundsM = Result.BoundsM;
		Result.Count = static_cast<int32>(FMath::Min<int64>(Count, MAX_int32));
		Result.bOk = true;
	}
	return Result;
}

// ---- Applying results (game thread) -------------------------------------------------

void UPFSplatComponent::StoreLoadResult(const FPFSplatLoadResult& Result)
{
	SplatFile = Result.Path;
	GpuRecords = Result.Records;   // null for the CPU-only case
	LocalBoundsM = Result.BoundsM;
	CoreBoundsM = Result.CoreBoundsM;
	SplatCount = Result.Count;
	LoadedFile = Result.Path;
	LoadedAxis = Result.Axis;
}

void UPFSplatComponent::RebuildRenderState()
{
	MarkRenderStateDirty();
	UpdateBounds();
}

void UPFSplatComponent::ApplyLoadResult(const FPFSplatLoadResult& Result)
{
	if (Result.bOk)
	{
		StoreLoadResult(Result);
		RebuildRenderState();
		OnSplatLoaded.Broadcast(this, SplatCount);
	}
	else
	{
		OnSplatLoadFailed.Broadcast(this, Result.Path);
	}
}

bool UPFSplatComponent::EnsureLoaded(bool bForce)
{
	if (SplatFile.IsEmpty())
	{
		GpuRecords.Reset();
		LoadedFile.Reset();
		return false;
	}
	if (!bForce && LoadedFile == SplatFile && LoadedAxis == UpAxis && (GpuRecords.IsValid() || SplatCount > 0))
	{
		return true;
	}

	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!Api.IsLoaded() && !Api.Load())
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: PointForgeNative.dll not available"));
		return false;
	}
	const FPFSplatLoadResult Result = ParseSplatFile(SplatFile, UpAxis);
	if (!Result.bOk)
	{
		GpuRecords.Reset();
		LoadedFile.Reset();
		return false;
	}
	StoreLoadResult(Result);
	return true;
}

bool UPFSplatComponent::LoadSplatFile(const FString& Path)
{
	++LoadTicket;              // cancels any async load in flight
	bAsyncLoading = false;

	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!Api.IsLoaded() && !Api.Load())
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: PointForgeNative.dll not available"));
		OnSplatLoadFailed.Broadcast(this, Path);
		return false;
	}
	const FPFSplatLoadResult Result = ParseSplatFile(Path, UpAxis);
	ApplyLoadResult(Result);
	return Result.bOk;
}

void UPFSplatComponent::LoadSplatFileAsync(const FString& Path)
{
	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!Api.IsLoaded() && !Api.Load())   // load the DLL here, on the game thread
	{
		UE_LOG(LogTemp, Error, TEXT("PFSplatComponent: PointForgeNative.dll not available"));
		OnSplatLoadFailed.Broadcast(this, Path);
		return;
	}

	const uint32 Ticket = ++LoadTicket;
	bAsyncLoading = true;
	TWeakObjectPtr<UPFSplatComponent> WeakThis(this);
	const EPFSplatUpAxis Axis = UpAxis;

	Async(EAsyncExecution::ThreadPool, [WeakThis, Path, Axis, Ticket]()
	{
		// Background thread: parse + convert only; no UObject access.
		TSharedRef<FPFSplatLoadResult, ESPMode::ThreadSafe> Result =
			MakeShared<FPFSplatLoadResult, ESPMode::ThreadSafe>(ParseSplatFile(Path, Axis));

		AsyncTask(ENamedThreads::GameThread, [WeakThis, Result, Ticket]()
		{
			UPFSplatComponent* Self = WeakThis.Get();
			if (!Self || Ticket != Self->LoadTicket)
			{
				return;   // component gone, or a newer load superseded this one
			}
			Self->bAsyncLoading = false;
			Self->ApplyLoadResult(*Result);
		});
	});
}

void UPFSplatComponent::UnloadSplats()
{
	++LoadTicket;
	bAsyncLoading = false;
	SplatFile.Reset();
	GpuRecords.Reset();
	LoadedFile.Reset();
	SplatCount = 0;
	LocalBoundsM = FBox(ForceInit);
	CoreBoundsM = FBox(ForceInit);
	RebuildRenderState();
}

// ---- Queries ---------------------------------------------------------------------------

FBox UPFSplatComponent::GetCoreBounds(bool bWorldSpace) const
{
	const FBox Core = CoreBoundsM.IsValid ? CoreBoundsM : LocalBoundsM;
	if (!Core.IsValid)
	{
		return FBox(ForceInit);
	}
	const FBox LocalUU(Core.Min * UnitScale, Core.Max * UnitScale);
	return bWorldSpace ? LocalUU.TransformBy(GetComponentTransform()) : LocalUU;
}

bool UPFSplatComponent::GetSuggestedViewpoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const FBox Core = GetCoreBounds(/*bWorldSpace*/ false);
	if (!Core.IsValid)
	{
		return false;
	}
	// Component space: walk along the longer horizontal side, from 10% in.
	const FVector Size = Core.GetSize();
	const FVector Centre = Core.GetCenter();
	const bool bAlongX = Size.X >= Size.Y;
	FVector Eye = Centre;
	FVector Dir = FVector::ZeroVector;
	if (bAlongX) { Eye.X = Core.Min.X + 0.1 * Size.X; Dir.X = 1.0; }
	else         { Eye.Y = Core.Min.Y + 0.1 * Size.Y; Dir.Y = 1.0; }
	Eye.Z = Core.Min.Z + FMath::Min(1.7 * UnitScale, 0.5 * Size.Z);   // eye height, but stay inside

	const FTransform& T = GetComponentTransform();
	OutLocation = T.TransformPosition(Eye);
	FRotator R = T.TransformVectorNoScale(Dir).Rotation();
	R.Pitch = -5.0;
	OutRotation = R;
	return true;
}

// ---- Setters (Blueprint) -----------------------------------------------------------

void UPFSplatComponent::SetUpAxis(EPFSplatUpAxis NewUpAxis)
{
	if (UpAxis == NewUpAxis)
	{
		return;
	}
	UpAxis = NewUpAxis;
	EnsureLoaded();   // re-converts the cached file for the new axis
	RebuildRenderState();
}

void UPFSplatComponent::SetUnitScale(float NewUnitScale)
{
	UnitScale = FMath::Max(NewUnitScale, 0.001f);
	RebuildRenderState();
}

void UPFSplatComponent::SetMinSplatPixels(float NewMinSplatPixels)
{
	MinSplatPixels = FMath::Max(NewMinSplatPixels, 0.0f);
	MarkRenderStateDirty();
}

void UPFSplatComponent::SetMaxSplatInstances(int32 NewMaxSplatInstances)
{
	MaxSplatInstances = FMath::Max(NewMaxSplatInstances, 1000);
	MarkRenderStateDirty();
}

void UPFSplatComponent::SetMaxSplatScreenFraction(float NewFraction)
{
	MaxSplatScreenFraction = FMath::Clamp(NewFraction, 0.05f, 4.0f);
	MarkRenderStateDirty();
}

void UPFSplatComponent::SetUseGpuSplats(bool bNewUseGpuSplats)
{
	bUseGpuSplats = bNewUseGpuSplats;
	MarkRenderStateDirty();
}

void UPFSplatComponent::SetSplatMaterial(UMaterialInterface* NewMaterial)
{
	SplatMaterial = NewMaterial;
	MarkRenderStateDirty();
}

// ---- Material slot ----------------------------------------------------------------

UMaterialInterface* UPFSplatComponent::GetMaterial(int32 ElementIndex) const
{
	return ElementIndex == 0 ? SplatMaterial.Get() : nullptr;
}

void UPFSplatComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* Material)
{
	if (ElementIndex == 0)
	{
		SetSplatMaterial(Material);   // enables Create Dynamic Material Instance on this component
	}
}

void UPFSplatComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (SplatMaterial)
	{
		OutMaterials.Add(SplatMaterial);   // also makes cooking include the material
	}
}

// ---- Component lifecycle ------------------------------------------------------------

void UPFSplatComponent::OnRegister()
{
	// Load before the render state is created, so bounds and data are ready
	// for the first proxy (level load, duplication, PIE, packaged game).
	EnsureLoaded();
	Super::OnRegister();
}

#if WITH_EDITOR
void UPFSplatComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Name = PropertyChangedEvent.GetPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UPFSplatComponent, SplatFile)
		|| Name == GET_MEMBER_NAME_CHECKED(UPFSplatComponent, UpAxis))
	{
		EnsureLoaded();
		UpdateBounds();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);   // recreates the render state
}
#endif

FPrimitiveSceneProxy* UPFSplatComponent::CreateSceneProxy()
{
	// May run on a worker thread (concurrent render-state creation in packaged
	// games), so this only READS component state. Loading happens on the game
	// thread (OnRegister, setters, LoadSplatFile[Async]); the GPU view extension
	// is registered at engine start (FPointForgeViewerModule).

	// GPU path: share the cached records; per-frame work runs on the GPU.
	const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : GMaxRHIFeatureLevel;
	if (bUseGpuSplats && PFSplatGpu_IsSupported(FeatureLevel) && GpuRecords.IsValid() && PFSplatGpu_HasViewExtension())
	{
		return new FPFSplatGpuSceneProxy(this, GpuRecords);
	}

	// CPU fallback: the proxy owns its own native handle (draws the file as stored).
	PFSplatCloud* Cloud = nullptr;
	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!SplatFile.IsEmpty() && Api.IsLoaded())   // loaded on the game thread by OnRegister
	{
		FTCHARToUTF8 Utf8(*SplatFile);
		Cloud = Api.Splat_Open(Utf8.Get());
	}
	// A null cloud gives a proxy that draws nothing, which is safer than failing here.
	return new FPFSplatSceneProxy(this, Cloud);
}

FBoxSphereBounds UPFSplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Local bounds are in metres; the component's local space is in Unreal units.
	// Pad by a little so splats whose centres lie on the hull are not culled early.
	const FBox Box = GetLocalBoundsUU().ExpandBy(UnitScale * 0.5);
	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}
