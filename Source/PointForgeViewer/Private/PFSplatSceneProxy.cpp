// Copyright. PointForge Viewer — Gaussian splat scene proxy (billboard path).

#include "PFSplatSceneProxy.h"
#include "PFSplatComponent.h"

#include "MaterialDomain.h"              // MD_Surface
#include "Materials/Material.h"          // UMaterial::GetDefaultMaterial
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"
#include "SceneInterface.h"             // FSceneInterface (GetScene().GetFeatureLevel)
#include "SceneView.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformTime.h"
#include "RHICommandList.h"              // FRHICommandList, QueueAsyncCommandListSubmit
#include "RHIGlobals.h"                  // GRHISupportsAsyncTextureCreation
#include "RenderingThread.h"             // ENQUEUE_RENDER_COMMAND

// TEMP perf instrumentation (spike hunt): accumulated ms, logged every 60 batches.
namespace PFSplatPerf
{
	static std::atomic<double> UpdateMs{0}, FillMs{0}, InitMs{0}, ReleaseMs{0}, InitMax{0}, ReleaseMax{0}, FillMax{0}, UpdateMax{0}, UploadMs{0}, UploadMax{0};
	static std::atomic<int32> Built{0}, Adopted{0};
	static void Add(std::atomic<double>& Sum, std::atomic<double>& Max, double V)
	{
		Sum.store(Sum.load() + V);
		if (V > Max.load()) Max.store(V);
	}
}

// ---------------------------------------------------------------------------
// FPFSplatWorker — runs the DLL update and the quad build off the render thread
// ---------------------------------------------------------------------------

class FPFSplatWorker final : public FRunnable
{
public:
	explicit FPFSplatWorker(FPFSplatSceneProxy* InProxy)
		: Proxy(InProxy)
	{
		Wake = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset*/ false);
		Thread = FRunnableThread::Create(this, TEXT("PFSplatWorker"), 0, TPri_Normal);
	}

	virtual ~FPFSplatWorker() override
	{
		Stop();
		if (Thread)
		{
			Thread->WaitForCompletion();
			delete Thread;
			Thread = nullptr;
		}
		FPlatformProcess::ReturnSynchEventToPool(Wake);
		Wake = nullptr;
	}

	/** Render thread: a new camera was posted. */
	void Notify() { Wake->Trigger(); }

	virtual void Stop() override
	{
		bStop = true;
		if (Wake)
		{
			Wake->Trigger();
		}
	}

	virtual uint32 Run() override
	{
		FPFSplatViewRequest Last;
		bool bHasLast = false;

		while (!bStop)
		{
			Wake->Wait(100);   // timeout only bounds shutdown latency
			if (bStop)
			{
				break;
			}

			FPFSplatViewRequest Req;
			bool bHave = false;
			TArray<FPFSplatBatch*> ToDelete;
			{
				FScopeLock Lock(&Proxy->Mutex);
				ToDelete = MoveTemp(Proxy->DeleteQueue);   // GPU refs already dropped by the render thread
				Proxy->DeleteQueue.Reset();
				if (Proxy->bHasPendingRequest)
				{
					Req = Proxy->PendingRequest;
					Proxy->bHasPendingRequest = false;
					bHave = true;
				}
			}
			// Free retired batches here: their CPU arrays are tens of MB, and
			// freeing them on the render thread showed up as frame spikes.
			for (FPFSplatBatch* Old : ToDelete)
			{
				delete Old;
			}

			// Same camera as the last update: the drawn batch is already correct.
			// (Requests are zero-initialised, so padding compares equal.)
			if (!bHave || (bHasLast && FMemory::Memcmp(&Req, &Last, sizeof(Req)) == 0))
			{
				continue;
			}

			FPFSplatBatch* NewBatch = Proxy->BuildBatch(Req);
			Last = Req;
			bHasLast = true;

			{
				FScopeLock Lock(&Proxy->Mutex);
				// A batch the render thread has not adopted yet may already own GPU
				// buffers and a recorded upload, which only the render thread may
				// submit and release: hand it over instead of deleting it here.
				if (Proxy->ReadyBatch)
				{
					Proxy->StaleBatches.Add(Proxy->ReadyBatch);
				}
				Proxy->ReadyBatch = NewBatch;
			}
		}
		return 0;
	}

private:
	FPFSplatSceneProxy* Proxy = nullptr;   // outlives the worker (joined in ~FPFSplatSceneProxy)
	FEvent* Wake = nullptr;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStop{ false };
};

// ---------------------------------------------------------------------------
// FPFSplatBatch
// ---------------------------------------------------------------------------

void FPFSplatBatch::InitBuffers(FRHICommandListBase& RHICmdList)
{
	// The RHI copies the CPU data while creating each buffer (resource-array
	// upload), so this is where the bulk of the per-batch cost is.
	Buffers.PositionVertexBuffer.InitResource(RHICmdList);
	Buffers.StaticMeshVertexBuffer.InitResource(RHICmdList);
	Buffers.ColorVertexBuffer.InitResource(RHICmdList);
	IndexBuffer.InitResource(RHICmdList);
	bBuffersInit = true;
}

void FPFSplatBatch::SubmitUpload()
{
	check(IsInRenderingThread());
	if (UploadCmdList)
	{
		// Same call the engine's skeletal-mesh stream-in uses: the recorded
		// buffer creation executes before anything recorded later this frame.
		FRHICommandListImmediate::Get().QueueAsyncCommandListSubmit(UploadCmdList);
		UploadCmdList = nullptr;   // owned by the RHI now
	}
}

void FPFSplatBatch::InitFactory(FRHICommandListBase& RHICmdList)
{
	FLocalVertexFactory::FDataType Data;
	Buffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, Data);
	Buffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, Data);
	Buffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&VertexFactory, Data);
	Buffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&VertexFactory, Data, /*LightMapIndex*/ 0);
	Buffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, Data);
	VertexFactory.SetData(RHICmdList, Data);
	VertexFactory.InitResource(RHICmdList);

	// Only draw when every stream RHI is live (same gate as the point proxy).
	bGpuReady = VertexFactory.IsInitialized()
		&& Buffers.PositionVertexBuffer.VertexBufferRHI.IsValid()
		&& Buffers.StaticMeshVertexBuffer.IsValid()
		&& Buffers.ColorVertexBuffer.VertexBufferRHI.IsValid()
		&& IndexBuffer.IndexBufferRHI.IsValid();
}

void FPFSplatBatch::ReleaseResources()
{
	// A recorded upload must reach the RHI before its buffers are released
	// (a recorded, never-submitted command list cannot just be deleted).
	SubmitUpload();
	Buffers.PositionVertexBuffer.ReleaseResource();
	Buffers.StaticMeshVertexBuffer.ReleaseResource();
	Buffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
	bBuffersInit = false;
	bGpuReady = false;
}

// ---------------------------------------------------------------------------
// FPFSplatSceneProxy
// ---------------------------------------------------------------------------

SIZE_T FPFSplatSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<SIZE_T>(&UniquePointer);
}

FPFSplatSceneProxy::FPFSplatSceneProxy(const UPFSplatComponent* InComponent, PFSplatCloud* OwnedCloud)
	: FPrimitiveSceneProxy(InComponent)
	, Cloud(OwnedCloud)
{
	bWillEverBeLit = false;
	bVerifyUsedMaterials = false;

	// Batches are swapped and uploaded from GetDynamicMeshElements, which must
	// run on the render thread itself (same rule as the point proxy).
	bSupportsParallelGDME = false;

	UnitScale = InComponent ? InComponent->UnitScale : 100.0f;
	MinSplatPixels = InComponent ? InComponent->MinSplatPixels : 0.5f;
	MaxSplatInstances = InComponent ? InComponent->MaxSplatInstances : 300000;
	FeatureLevel = GetScene().GetFeatureLevel();

	UMaterialInterface* Mat = InComponent ? InComponent->SplatMaterial.Get() : nullptr;
	if (!Mat)
	{
		// Opaque default: splats will look wrong until SplatMaterial is assigned.
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	MaterialRenderProxy = Mat->GetRenderProxy();
	MaterialRelevance = Mat->GetRelevance_Concurrent(FeatureLevel);

	FMemory::Memzero(&PendingRequest, sizeof(PendingRequest));
	if (Cloud)
	{
		Worker = new FPFSplatWorker(this);
	}
}

FPFSplatSceneProxy::~FPFSplatSceneProxy()
{
	// Runs on the render thread or a renderer task thread. Join the worker first:
	// it is the only user of the cloud handle and of ReadyBatch's writer side.
	delete Worker;
	Worker = nullptr;

	// With the worker gone, every batch is ours: submit any recorded upload,
	// release the GPU side, free the memory.
	TArray<FPFSplatBatch*> All = MoveTemp(StaleBatches);
	All.Append(DeleteQueue);   // already released; ReleaseResources is a no-op for them
	if (ReadyBatch)
	{
		All.Add(ReadyBatch);
		ReadyBatch = nullptr;
	}
	if (Batch)
	{
		All.Add(Batch.Release());
	}
	StaleBatches.Reset();
	DeleteQueue.Reset();
	// The renderer may delete proxies on a task thread, not only on the render
	// thread, and a recorded upload can only be submitted on the render thread
	// (an unsubmitted command list asserts when deleted). ENQUEUE_RENDER_COMMAND
	// runs inline when already on the render thread and queues otherwise.
	if (All.Num() > 0)
	{
		ENQUEUE_RENDER_COMMAND(PFSplatReleaseBatches)(
			[All = MoveTemp(All)](FRHICommandListImmediate&)
			{
				for (FPFSplatBatch* B : All)
				{
					B->ReleaseResources();   // submits any recorded upload first
					delete B;
				}
			});
	}
	if (Cloud)
	{
		FPFNativeApi& Api = FPFNativeApi::Get();
		if (Api.IsLoaded())
		{
			Api.Splat_Close(Cloud);
		}
		Cloud = nullptr;
	}
}

FPFSplatBatch* FPFSplatSceneProxy::BuildBatch(const FPFSplatViewRequest& Req)
{
	FPFNativeApi& Api = FPFNativeApi::Get();
	FPFSplatBatch* Out = new FPFSplatBatch(FeatureLevel);
	if (!Cloud || !Api.IsLoaded())
	{
		return Out;   // empty: draw nothing
	}

	// ---- native update: cull, LOD, order, Gaussian (the expensive part) ----
	Instances.SetNumUninitialized(MaxSplatInstances);
	const double T0 = FPlatformTime::Seconds();
	const int32 N = Api.Splat_Update(Cloud, &Req.Cam, Instances.GetData(), MaxSplatInstances);
	const double T1 = FPlatformTime::Seconds();
	PFSplatPerf::Add(PFSplatPerf::UpdateMs, PFSplatPerf::UpdateMax, (T1 - T0) * 1000.0);
	LastVisibleCount.store(N > 0 ? N : 0);
	if (N <= 0)
	{
		return Out;
	}

	// ---- quads straight into the CPU side of the vertex/index buffers ------
	// bNeedsCPUAccess = false: the CPU copy is dropped after the GPU upload.
	const uint32 NumVerts = static_cast<uint32>(N) * 4u;
	FStaticMeshVertexBuffers& B = Out->Buffers;
	B.PositionVertexBuffer.Init(NumVerts, /*bNeedsCPUAccess*/ false);
	// Full-precision UVs: the conic terms can be tiny, and half floats would
	// quantise them (wrong ellipse shape on large splats).
	B.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
	B.StaticMeshVertexBuffer.Init(NumVerts, /*NumTexCoords*/ 3, /*bNeedsCPUAccess*/ false);
	B.ColorVertexBuffer.Init(NumVerts, /*bNeedsCPUAccess*/ false);
	Out->IndexBuffer.Indices.SetNumUninitialized(N * 6);
	uint32* Idx = Out->IndexBuffer.Indices.GetData();

	static const FVector2f Corners[4] = {
		FVector2f(-1.0f, -1.0f), FVector2f(1.0f, -1.0f),
		FVector2f(1.0f,  1.0f),  FVector2f(-1.0f, 1.0f)
	};
	const FVector3f TX(1, 0, 0), TY(0, 1, 0), TZ(0, 0, 1);
	const float U = UnitScale;

	for (int32 i = 0; i < N; ++i)
	{
		const PFSplatInstance& S = Instances[i];
		const FVector3f Centre(S.position[0], S.position[1], S.position[2]);   // metres, splat-local
		const float Wr = S.worldRadius;                                       // metres at this depth

		// sRGB-encoded colour; the splat material decodes it to linear.
		const FColor Col(
			static_cast<uint8>(FMath::Clamp(S.color[0] * 255.0f + 0.5f, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(S.color[1] * 255.0f + 0.5f, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(S.color[2] * 255.0f + 0.5f, 0.0f, 255.0f)),
			static_cast<uint8>(FMath::Clamp(S.color[3] * 255.0f + 0.5f, 0.0f, 255.0f)));   // alpha = opacity
		const FVector2f Conic01(S.conic[0], S.conic[1]);
		const FVector2f Conic2R(S.conic[2], S.radiusPx);

		const uint32 v = static_cast<uint32>(i) * 4u;
		for (uint32 c = 0; c < 4; ++c)
		{
			// Camera-facing corner in splat-local metres, then to Unreal units.
			const FVector3f P = (Centre + (Req.RightL * Corners[c].X + Req.UpL * Corners[c].Y) * Wr) * U;
			B.PositionVertexBuffer.VertexPosition(v + c) = P;
			B.StaticMeshVertexBuffer.SetVertexTangents(v + c, TX, TY, TZ);
			B.StaticMeshVertexBuffer.SetVertexUV(v + c, 0, Corners[c]);   // quad corner
			B.StaticMeshVertexBuffer.SetVertexUV(v + c, 1, Conic01);      // conic a, b
			B.StaticMeshVertexBuffer.SetVertexUV(v + c, 2, Conic2R);      // conic c, radius px
			B.ColorVertexBuffer.VertexColor(v + c) = Col;
		}

		// Back-to-front order from the DLL is kept: quad i draws before quad i+1.
		uint32* q = Idx + static_cast<size_t>(i) * 6;
		q[0] = v; q[1] = v + 1; q[2] = v + 2;
		q[3] = v; q[4] = v + 2; q[5] = v + 3;
	}

	Out->NumSplats = N;
	const double T2 = FPlatformTime::Seconds();
	PFSplatPerf::Add(PFSplatPerf::FillMs, PFSplatPerf::FillMax, (T2 - T1) * 1000.0);

	// ---- GPU buffers, created and uploaded here rather than on the render
	// thread (it was 56-220 ms of render-thread time per batch at ~1M splats).
	// Gate and pattern match the engine's mesh streaming.
	if (GRHISupportsAsyncTextureCreation)
	{
		Out->UploadCmdList = new FRHICommandList(FRHIGPUMask::All());
		Out->UploadCmdList->SwitchPipeline(ERHIPipeline::Graphics);
		Out->InitBuffers(*Out->UploadCmdList);
		Out->UploadCmdList->FinishRecording();
	}
	PFSplatPerf::Add(PFSplatPerf::UploadMs, PFSplatPerf::UploadMax, (FPlatformTime::Seconds() - T2) * 1000.0);
	PFSplatPerf::Built.fetch_add(1);
	return Out;
}

void FPFSplatSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	if (!Cloud || !Worker || !MaterialRenderProxy)
	{
		return;
	}

	// ---- 1. camera for the worker, from the first visible view -------------
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FSceneView* View = Views[ViewIndex];
		if (!(VisibilityMap & (1u << ViewIndex)) || !View)
		{
			continue;
		}
		const FIntRect Rect = View->UnconstrainedViewRect;
		const int32 VpW = Rect.Width();
		const int32 VpH = Rect.Height();
		if (VpW <= 0 || VpH <= 0)
		{
			continue;
		}

		// Unreal: row-vector matrices, view space +Z forward (left-handed), cm.
		// Native DLL: column-vector, right-handed, view space -Z forward, metres.
		// Chain: metres -> cm (S_u) -> local-to-world -> view -> metres (S_u^-1),
		// then flip Z so the DLL sees the camera looking down -Z.
		const FMatrix ViewM = View->ViewMatrices.GetViewMatrix();
		const FMatrix ProjM = View->ViewMatrices.GetProjectionMatrix();
		const bool bOrtho = ProjM.M[3][3] > 0.5f;

		const double Us = UnitScale;
		const FMatrix ScaleUp(FPlane(Us, 0, 0, 0), FPlane(0, Us, 0, 0), FPlane(0, 0, Us, 0), FPlane(0, 0, 0, 1));
		const FMatrix ScaleDown(FPlane(1.0 / Us, 0, 0, 0), FPlane(0, 1.0 / Us, 0, 0), FPlane(0, 0, 1.0 / Us, 0), FPlane(0, 0, 0, 1));
		const FMatrix Flip(FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, -1, 0), FPlane(0, 0, 0, 1));
		const FMatrix ViewMetres = ScaleUp * GetLocalToWorld() * ViewM * ScaleDown * Flip;

		FPFSplatViewRequest Req;
		FMemory::Memzero(&Req, sizeof(Req));   // zero padding: the worker compares requests bytewise

		// The DLL wants a column-major, column-vector matrix G, element (r, c) at
		// [c*4 + r]. Unreal's FMatrix is row-vector, so for the same map G = M^T,
		// i.e. G(r, c) = M[c][r], which lands at [c*4 + r]. That is M copied in its
		// own row-major order. (Verified against the DLL: the transposed layout
		// culls almost everything.)
		for (int32 r = 0; r < 4; ++r)
		{
			for (int32 c = 0; c < 4; ++c)
			{
				Req.Cam.view[r * 4 + c] = static_cast<float>(ViewMetres.M[r][c]);
			}
		}

		// Projection: keep Unreal's x/y scale (same field of view and aspect), use a
		// standard GL-style depth (the DLL only needs x/y scale and the -Z convention).
		const float N = 0.05f;      // metres
		const float F = 10000.0f;   // metres
		Req.Cam.proj[0] = static_cast<float>(ProjM.M[0][0]);
		Req.Cam.proj[5] = static_cast<float>(ProjM.M[1][1]);
		if (bOrtho)
		{
			Req.Cam.proj[10] = -2.0f / (F - N);
			Req.Cam.proj[14] = -(F + N) / (F - N);
			Req.Cam.proj[15] = 1.0f;
		}
		else
		{
			Req.Cam.proj[10] = (F + N) / (N - F);
			Req.Cam.proj[11] = -1.0f;
			Req.Cam.proj[14] = 2.0f * F * N / (N - F);
		}
		Req.Cam.viewportWidth = static_cast<float>(VpW);
		Req.Cam.viewportHeight = static_cast<float>(VpH);
		Req.Cam.minPixelRadius = MinSplatPixels;
		Req.Cam.maxInstances = 0;     // the worker's capacity is the cap
		Req.Cam.isOrtho = bOrtho ? 1u : 0u;

		// Camera right/up in splat-local metres (for camera-facing quads).
		const FMatrix ViewInv = ViewMetres.Inverse();
		const FVector RightL = ViewInv.TransformVector(FVector(1, 0, 0));   // FVector4 -> FVector
		const FVector UpL = ViewInv.TransformVector(FVector(0, 1, 0));
		Req.RightL = FVector3f(RightL.GetSafeNormal());
		Req.UpL = FVector3f(UpL.GetSafeNormal());

		{
			FScopeLock Lock(&Mutex);
			PendingRequest = Req;          // latest wins
			bHasPendingRequest = true;
		}
		Worker->Notify();
		break;   // one camera per frame (stereo: both eyes draw the same batch)
	}

	// ---- 2. adopt a finished batch, if any ---------------------------------
	FPFSplatBatch* Fresh = nullptr;
	TArray<FPFSplatBatch*> Stale;
	{
		FScopeLock Lock(&Mutex);
		Fresh = ReadyBatch;
		ReadyBatch = nullptr;
		Stale = MoveTemp(StaleBatches);
		StaleBatches.Reset();
	}

	// Retire = submit any recorded upload, drop the GPU refs (render thread;
	// last frame's draws are already submitted and RHI objects are
	// ref-counted), then let the worker free the CPU memory.
	TArray<FPFSplatBatch*> Retired;
	const double R0 = FPlatformTime::Seconds();
	for (FPFSplatBatch* S : Stale)
	{
		S->ReleaseResources();
		Retired.Add(S);
	}
	if (Fresh)
	{
		if (Batch)
		{
			Batch->ReleaseResources();
			Retired.Add(Batch.Release());
		}
		Batch.Reset(Fresh);
	}
	const double R1 = FPlatformTime::Seconds();
	if (Fresh)
	{
		if (Batch->NumSplats > 0)
		{
			Batch->SubmitUpload();
			if (!Batch->bBuffersInit)
			{
				// No async resource creation on this RHI: upload here (old path).
				Batch->InitBuffers(Collector.GetRHICommandList());
			}
			Batch->InitFactory(Collector.GetRHICommandList());
		}
	}
	const double R2 = FPlatformTime::Seconds();
	if (Retired.Num() > 0)
	{
		FScopeLock Lock(&Mutex);
		DeleteQueue.Append(Retired);
	}
	if (Fresh)
	{
		PFSplatPerf::Add(PFSplatPerf::ReleaseMs, PFSplatPerf::ReleaseMax, (R1 - R0) * 1000.0);
		PFSplatPerf::Add(PFSplatPerf::InitMs, PFSplatPerf::InitMax, (R2 - R1) * 1000.0);
		const int32 A = PFSplatPerf::Adopted.fetch_add(1) + 1;
		if (A % 60 == 0)
		{
			const int32 B = FMath::Max(1, PFSplatPerf::Built.load());
			UE_LOG(LogTemp, Log, TEXT("PFSplatPerf: built=%d adopted=%d | worker update avg %.2f max %.2f, fill avg %.2f max %.2f, upload avg %.2f max %.2f | RT release avg %.2f max %.2f, init avg %.2f max %.2f ms (splats %d, async=%d)"),
				B, A,
				PFSplatPerf::UpdateMs.load() / B, PFSplatPerf::UpdateMax.load(),
				PFSplatPerf::FillMs.load() / B, PFSplatPerf::FillMax.load(),
				PFSplatPerf::UploadMs.load() / B, PFSplatPerf::UploadMax.load(),
				PFSplatPerf::ReleaseMs.load() / A, PFSplatPerf::ReleaseMax.load(),
				PFSplatPerf::InitMs.load() / A, PFSplatPerf::InitMax.load(), Batch->NumSplats,
				GRHISupportsAsyncTextureCreation ? 1 : 0);
		}
	}

	if (!Batch || Batch->NumSplats <= 0 || !Batch->bGpuReady)
	{
		return;
	}

	// ---- 3. draw the current batch in every visible view -------------------
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (!(VisibilityMap & (1u << ViewIndex)) || !Views[ViewIndex])
		{
			continue;
		}

		FMeshBatch& Mesh = Collector.AllocateMesh();
		Mesh.VertexFactory = &Batch->VertexFactory;
		Mesh.MaterialRenderProxy = MaterialRenderProxy;
		Mesh.bWireframe = false;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = SDPG_World;
		Mesh.bCanApplyViewModeOverrides = false;
		Mesh.CastShadow = false;

		FDynamicPrimitiveUniformBuffer& DUB = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
		FPrimitiveUniformShaderParametersBuilder Builder;
		BuildUniformShaderParameters(Builder);
		DUB.Set(Collector.GetRHICommandList(), Builder);

		FMeshBatchElement& Element = Mesh.Elements[0];
		Element.PrimitiveUniformBufferResource = &DUB.UniformBuffer;
		Element.IndexBuffer = &Batch->IndexBuffer;
		Element.FirstIndex = 0;
		Element.NumPrimitives = Batch->NumSplats * 2;          // 2 triangles per splat
		Element.MinVertexIndex = 0;
		Element.MaxVertexIndex = Batch->NumSplats * 4 - 1;     // 4 verts per splat
		Collector.AddMesh(ViewIndex, Mesh);
	}
}

FPrimitiveViewRelevance FPFSplatSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bShadowRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = false;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = false;
	return Result;
}
