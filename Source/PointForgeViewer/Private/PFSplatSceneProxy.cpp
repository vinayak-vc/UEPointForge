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
#include "RenderingThread.h"             // ENQUEUE_RENDER_COMMAND (deferred batch release)

// ---------------------------------------------------------------------------
// FPFSplatBatch
// ---------------------------------------------------------------------------

void FPFSplatBatch::ReleaseResources()
{
	Buffers.PositionVertexBuffer.ReleaseResource();
	Buffers.StaticMeshVertexBuffer.ReleaseResource();
	Buffers.ColorVertexBuffer.ReleaseResource();
	IndexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
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

	// Same rule as the point proxy: the batch is created and released from
	// GetDynamicMeshElements, which must run on the render thread itself.
	bSupportsParallelGDME = false;

	UnitScale = InComponent ? InComponent->UnitScale : 100.0f;
	MinSplatPixels = InComponent ? InComponent->MinSplatPixels : 0.5f;
	MaxSplatInstances = InComponent ? InComponent->MaxSplatInstances : 300000;

	UMaterialInterface* Mat = InComponent ? InComponent->SplatMaterial.Get() : nullptr;
	if (!Mat)
	{
		// Opaque default: splats will look wrong until SplatMaterial is assigned.
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	MaterialRenderProxy = Mat->GetRenderProxy();
	MaterialRelevance = Mat->GetRelevance_Concurrent(GetScene().GetFeatureLevel());
}

FPFSplatSceneProxy::~FPFSplatSceneProxy()
{
	// Destructor runs on the render thread, so both releases are safe here.
	if (Batch)
	{
		Batch->ReleaseResources();
		Batch.Reset();
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

void FPFSplatSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily,
	uint32 VisibilityMap,
	FMeshElementCollector& Collector) const
{
	if (!Cloud || !MaterialRenderProxy)
	{
		return;
	}
	FPFNativeApi& Api = FPFNativeApi::Get();
	if (!Api.IsLoaded())
	{
		return;
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (!(VisibilityMap & (1u << ViewIndex)))
		{
			continue;
		}
		const FSceneView* View = Views[ViewIndex];
		if (!View)
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

		// ---- camera for the DLL, in splat metres ----------------------------
		// Unreal: row-vector matrices, view space +Z forward (left-handed), cm.
		// Native DLL: column-vector, right-handed, view space -Z forward, metres.
		// Chain: metres -> cm (S_u) -> local-to-world -> view -> metres (S_u^-1),
		// then flip Z so the DLL sees the camera looking down -Z.
		const FMatrix ViewM = View->ViewMatrices.GetViewMatrix();
		const FMatrix ProjM = View->ViewMatrices.GetProjectionMatrix();
		const bool bOrtho = ProjM.M[3][3] > 0.5f;

		const float U = UnitScale;
		const FMatrix ScaleUp(FPlane(U, 0, 0, 0), FPlane(0, U, 0, 0), FPlane(0, 0, U, 0), FPlane(0, 0, 0, 1));
		const FMatrix ScaleDown(FPlane(1.0f / U, 0, 0, 0), FPlane(0, 1.0f / U, 0, 0), FPlane(0, 0, 1.0f / U, 0), FPlane(0, 0, 0, 1));
		const FMatrix Flip(FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, -1, 0), FPlane(0, 0, 0, 1));
		const FMatrix L2W = GetLocalToWorld();
		const FMatrix ViewMetres = ScaleUp * L2W * ViewM * ScaleDown * Flip;

		PFSplatCamera Cam;
		FMemory::Memzero(&Cam, sizeof(Cam));
		// Column-major: element (row r, col c) lives at [c*4 + r]. Unreal's FMatrix is
		// row-vector, so for the same linear map the DLL-side element is M[r][c].
		for (int32 r = 0; r < 4; ++r)
		{
			for (int32 c = 0; c < 4; ++c)
			{
				Cam.view[c * 4 + r] = static_cast<float>(ViewMetres.M[r][c]);
			}
		}

		// Projection: keep Unreal's x/y scale (same field of view and aspect), use a
		// standard GL-style depth (the DLL only needs x/y scale and the -Z convention).
		FMemory::Memzero(Cam.proj, sizeof(Cam.proj));
		const float P00 = static_cast<float>(ProjM.M[0][0]);
		const float P11 = static_cast<float>(ProjM.M[1][1]);
		const float N = 0.05f;      // metres
		const float F = 10000.0f;   // metres
		Cam.proj[0] = P00;
		Cam.proj[5] = P11;
		if (bOrtho)
		{
			Cam.proj[10] = -2.0f / (F - N);
			Cam.proj[14] = -(F + N) / (F - N);
			Cam.proj[15] = 1.0f;
		}
		else
		{
			Cam.proj[10] = (F + N) / (N - F);
			Cam.proj[11] = -1.0f;
			Cam.proj[14] = 2.0f * F * N / (N - F);
		}

		Cam.viewportWidth = static_cast<float>(VpW);
		Cam.viewportHeight = static_cast<float>(VpH);
		Cam.minPixelRadius = MinSplatPixels;
		Cam.maxInstances = 0;     // capacity below is the cap
		Cam.isOrtho = bOrtho ? 1u : 0u;

		// ---- native update: cull, LOD, order, Gaussian ----------------------
		Instances.SetNumUninitialized(MaxSplatInstances);
		const int32 N_Visible = Api.Splat_Update(Cloud, &Cam, Instances.GetData(), MaxSplatInstances);
		LastVisibleCount = N_Visible;
		if (N_Visible <= 0)
		{
			break;   // one batch per frame: the first visible view
		}

		// ---- camera right/up in splat-local metres (for camera-facing quads)
		const FMatrix ViewInv = ViewMetres.Inverse();
		const FVector RightL = ViewInv.TransformVector(FVector(1, 0, 0)).GetSafeNormal();
		const FVector UpL    = ViewInv.TransformVector(FVector(0, 1, 0)).GetSafeNormal();

		// ---- expand each splat to a quad (4 verts, 2 tris), back-to-front order kept
		Verts.SetNumUninitialized(N_Visible * 4);
		Indices.SetNumUninitialized(N_Visible * 6);

		static const FVector2f Corners[4] = {
			FVector2f(-1.0f, -1.0f), FVector2f(1.0f, -1.0f),
			FVector2f(1.0f,  1.0f),  FVector2f(-1.0f, 1.0f)
		};

		for (int32 i = 0; i < N_Visible; ++i)
		{
			const PFSplatInstance& S = Instances[i];
			const FVector Centre(S.position[0], S.position[1], S.position[2]);   // metres, splat-local
			const double Wr = S.worldRadius;                                     // metres at this depth

			const FColor Col(
				static_cast<uint8>(FMath::Clamp(S.color[0] * 255.0f, 0.0f, 255.0f)),
				static_cast<uint8>(FMath::Clamp(S.color[1] * 255.0f, 0.0f, 255.0f)),
				static_cast<uint8>(FMath::Clamp(S.color[2] * 255.0f, 0.0f, 255.0f)),
				static_cast<uint8>(FMath::Clamp(S.color[3] * 255.0f, 0.0f, 255.0f)));   // alpha = opacity

			const int32 v = i * 4;
			for (int32 c = 0; c < 4; ++c)
			{
				FDynamicMeshVertex& Vtx = Verts[v + c];
				Vtx = FDynamicMeshVertex();

				// Camera-facing corner in splat-local metres, then to Unreal cm.
				const FVector P = (Centre + (RightL * Corners[c].X + UpL * Corners[c].Y) * Wr) * U;
				Vtx.Position = FVector3f(static_cast<float>(P.X), static_cast<float>(P.Y), static_cast<float>(P.Z));
				Vtx.Color = Col;
				Vtx.TextureCoordinate[0] = Corners[c];                                   // quad corner
				Vtx.TextureCoordinate[1] = FVector2f(S.conic[0], S.conic[1]);            // conic a, b
				Vtx.TextureCoordinate[2] = FVector2f(S.conic[2], S.radiusPx);            // conic c, radius px
				Vtx.SetTangents(FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1));
			}

			const int32 idx = i * 6;
			Indices[idx + 0] = v;
			Indices[idx + 1] = v + 1;
			Indices[idx + 2] = v + 2;
			Indices[idx + 3] = v;
			Indices[idx + 4] = v + 2;
			Indices[idx + 5] = v + 3;
		}

		// ---- GPU batch for this frame. Retire the previous one on the render
		// thread (deferred), exactly like the point proxy's eviction.
		if (Batch)
		{
			ENQUEUE_RENDER_COMMAND(PFSplatRetireBatch)(
				[Dead = MoveTemp(Batch)](FRHICommandListImmediate&) mutable
				{
					Dead->ReleaseResources();
					Dead.Reset();
				});
		}

		Batch = MakeUnique<FPFSplatBatch>(GetScene().GetFeatureLevel());
		Batch->NumSplats = N_Visible;
		// 3 texture coordinates: corner, conic.ab, (conic.c, radiusPx).
		Batch->Buffers.InitFromDynamicVertex(&Batch->VertexFactory, Verts, /*NumTexCoords*/ 3);
		Batch->IndexBuffer.Indices = Indices;
		BeginInitResource(&Batch->Buffers.PositionVertexBuffer);
		BeginInitResource(&Batch->Buffers.StaticMeshVertexBuffer);
		BeginInitResource(&Batch->Buffers.ColorVertexBuffer);
		BeginInitResource(&Batch->IndexBuffer);
		BeginInitResource(&Batch->VertexFactory);

		// Readiness gate (same reasoning as the point proxy): only emit the batch
		// when the stream RHIs are live.
		const bool bReady = Batch->VertexFactory.IsInitialized()
			&& Batch->Buffers.PositionVertexBuffer.VertexBufferRHI.IsValid()
			&& Batch->Buffers.StaticMeshVertexBuffer.IsValid()
			&& Batch->Buffers.ColorVertexBuffer.VertexBufferRHI.IsValid()
			&& Batch->IndexBuffer.IndexBufferRHI.IsValid();
		if (!bReady)
		{
			break;
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
		Element.NumPrimitives = N_Visible * 2;             // 2 triangles per splat
		Element.MinVertexIndex = 0;
		Element.MaxVertexIndex = N_Visible * 4 - 1;        // 4 verts per splat
		Collector.AddMesh(ViewIndex, Mesh);

		break;   // one batch per frame
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
