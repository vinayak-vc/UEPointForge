#include "PFPointCloudComponent.h"

#include "PFOctreeStore.h"
#include "PFPointCloudSceneProxy.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"

UPFPointCloudComponent::UPFPointCloudComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	CastShadow = false;
	bUseAsOccluder = false;
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stats = MakeShared<FPFViewerStats, ESPMode::ThreadSafe>();
}

bool UPFPointCloudComponent::OpenOctreeDir(const FString& OctreeDir)
{
	if (!Stats.IsValid())
	{
		Stats = MakeShared<FPFViewerStats, ESPMode::ThreadSafe>();
	}

	TSharedPtr<FPFOctreeStore> NewStore = MakeShared<FPFOctreeStore>();
	if (!NewStore->Open(OctreeDir, static_cast<double>(UnitScale), bColorIs16Bit))
	{
		return false;
	}

	Store = MoveTemp(NewStore);

	// Wrap PointMaterial in a dynamic instance so the panel can drive
	// PointSize/Round/Attenuate live. (Points are invisible unless the assigned
	// material does the billboard World Position Offset — see M_PFPoint.)
	if (PointMaterial)
	{
		PointMID = Cast<UMaterialInstanceDynamic>(PointMaterial);
		if (!PointMID)
		{
			PointMID = UMaterialInstanceDynamic::Create(PointMaterial, this);
			if (PointMID)
			{
				PointMaterial = PointMID;
			}
		}
	}

	// Drain all pending render commands (including any in-flight PFSetTunables
	// that captured the old SceneProxy pointer) before scheduling proxy deletion.
	// Without this, TickComponent's render command can race the old-proxy delete.
	FlushRenderingCommands();

	MarkRenderStateDirty(); // recreate the scene proxy (reads PointMaterial)
	UpdateBounds();
	return true;
}

void UPFPointCloudComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Smoothed frame time for the panel FPS readout.
	SmoothedDeltaSeconds = (SmoothedDeltaSeconds <= 0.f)
		? DeltaTime
		: FMath::Lerp(SmoothedDeltaSeconds, DeltaTime, 0.1f);

	// Push live tunables to the render-thread proxy.
	// IsRenderStateCreated() is a safer gate than SceneProxy != nullptr alone:
	// it is false during the window between MarkRenderStateDirty() and proxy recreation.
	if (IsRenderStateCreated() && SceneProxy)
	{
		FPFPointCloudSceneProxy* Proxy = static_cast<FPFPointCloudSceneProxy*>(SceneProxy);
		const float Sse = SseBudgetPixels;
		const int64 GpuBytes = static_cast<int64>(GpuBudgetMB) * 1024 * 1024;
		const int32 Uploads = UploadsPerFrame;
		const float Limit = PointCountLimit;
		ENQUEUE_RENDER_COMMAND(PFSetTunables)(
			[Proxy, Sse, GpuBytes, Uploads, Limit](FRHICommandListImmediate&)
			{
				Proxy->SetTunables_RenderThread(Sse, GpuBytes, Uploads, Limit);
			});
	}

	// Drive the billboard material params (game-thread MID setters).
	if (PointMID)
	{
		PointMID->SetScalarParameterValue(TEXT("PointSize"), PointSize);
		PointMID->SetScalarParameterValue(TEXT("Round"), bRoundPoints ? 1.f : 0.f);
		PointMID->SetScalarParameterValue(TEXT("Attenuate"), bAttenuate ? 1.f : 0.f);
		PointMID->SetScalarParameterValue(TEXT("SoftRound"), SoftRoundFalloff);
		PointMID->SetScalarParameterValue(TEXT("ColorMode"), static_cast<float>(ColorMode));

		// Elevation range: use overrides if Max>Min, else fall back to cloud's bbox Z.
		float ElevMin = ElevationMinZ;
		float ElevMax = ElevationMaxZ;
		if (!(ElevMax > ElevMin) && Store.IsValid() && Store->IsValid())
		{
			const FPFFileMetadata& M = Store->GetMeta();
			// Vertices are uploaded relative to the cube centre (in source units treated as UE cm).
			// WorldZ = (sourceZ - cubeCentreZ) + componentWorldZ.
			const double CubeCentreZ = M.CubeMin[2] + M.CubeSize * 0.5;
			const float CompZ = static_cast<float>(GetComponentLocation().Z);
			ElevMin = static_cast<float>(M.BbMin[2] - CubeCentreZ) + CompZ;
			ElevMax = static_cast<float>(M.BbMax[2] - CubeCentreZ) + CompZ;
		}
		PointMID->SetScalarParameterValue(TEXT("ElevationMinZ"), ElevMin);
		PointMID->SetScalarParameterValue(TEXT("ElevationMaxZ"), ElevMax);

		// Clipping plane
		PointMID->SetScalarParameterValue(TEXT("ClipEnable"), bUseClippingPlane ? 1.f : 0.f);
		PointMID->SetVectorParameterValue(TEXT("ClipOrigin"),
			FLinearColor(ClippingPlaneOrigin.X, ClippingPlaneOrigin.Y, ClippingPlaneOrigin.Z, 0.f));
		const FVector N = ClippingPlaneNormal.IsNearlyZero()
			? FVector(0, 0, 1) : ClippingPlaneNormal.GetSafeNormal();
		PointMID->SetVectorParameterValue(TEXT("ClipNormal"),
			FLinearColor(N.X, N.Y, N.Z, 0.f));
	}
}

FPFViewerStatsBP UPFPointCloudComponent::GetStats() const
{
	FPFViewerStatsBP Out;
	if (Store.IsValid() && Store->IsValid())
	{
		const FPFFileMetadata& M = Store->GetMeta();
		Out.CloudPoints = static_cast<int64>(M.PointCount);
		Out.CloudNodes  = static_cast<int32>(M.NodeCount);
		// World Z range matching the elevation auto-range formula.
		const double CubeCentreZ = M.CubeMin[2] + M.CubeSize * 0.5;
		const float CompZ = static_cast<float>(GetComponentLocation().Z);
		Out.CloudZMin = static_cast<float>(M.BbMin[2] - CubeCentreZ) + CompZ;
		Out.CloudZMax = static_cast<float>(M.BbMax[2] - CubeCentreZ) + CompZ;
	}
	if (Stats.IsValid())
	{
		Out.VisibleNodes = Stats->VisibleNodes.load();
		Out.DrawnNodes = Stats->DrawnNodes.load();
		Out.PointsOnGpu = Stats->PointsOnGpu.load();
		Out.DrawnPoints = Stats->DrawnPoints.load();
		Out.ResidentMB = static_cast<float>(Stats->ResidentBytes.load() / (1024.0 * 1024.0));
		Out.LoadQueue = Stats->PendingLoads.load();
	}
	Out.FPS = (SmoothedDeltaSeconds > 0.f) ? (1.f / SmoothedDeltaSeconds) : 0.f;
	return Out;
}

void UPFPointCloudComponent::OnUnregister()
{
	// Drain all queued render commands (including any PFSetTunables that captured
	// the raw proxy pointer) before UE schedules the proxy for deletion.
	// Without this, the render thread can execute a PFSetTunables command on a
	// proxy that was already freed when PIE ends or the level is unloaded.
	FlushRenderingCommands();
	Super::OnUnregister();
}

void UPFPointCloudComponent::Close()
{
	// Flush BEFORE releasing the store so the render thread can finish any
	// in-flight GetDynamicMeshElements that reads the store's node data.
	FlushRenderingCommands();
	Store.Reset();
	MarkRenderStateDirty();
	UpdateBounds();
}

FPrimitiveSceneProxy* UPFPointCloudComponent::CreateSceneProxy()
{
	if (Store.IsValid() && Store->IsValid())
	{
		return new FPFPointCloudSceneProxy(this);
	}
	return nullptr;
}

void UPFPointCloudComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (PointMID)
	{
		OutMaterials.Add(PointMID);
	}
	else if (PointMaterial)
	{
		OutMaterials.Add(PointMaterial);
	}
}

FBoxSphereBounds UPFPointCloudComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (Store.IsValid() && Store->IsValid())
	{
		const FPFFileMetadata& M = Store->GetMeta();
		const double Half = M.CubeSize * 0.5 * static_cast<double>(UnitScale);
		const FBox LocalBox(FVector(-Half, -Half, -Half), FVector(Half, Half, Half));
		return FBoxSphereBounds(LocalBox).TransformBy(LocalToWorld);
	}

	return FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f).TransformBy(LocalToWorld);
}
