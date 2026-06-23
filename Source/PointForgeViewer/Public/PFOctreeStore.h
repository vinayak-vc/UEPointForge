#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "PFOctreeFormat.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPointForge, Log, All);
#include "DynamicMeshBuilder.h"      // FDynamicMeshVertex
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"

class FRunnableThread;
class FEvent;

/** Octree root cube (source/world units) per node, for LOD + frustum traversal. */
struct FPFNodeCube
{
	double Min[3] = { 0, 0, 0 };
	double Size = 0.0;
};

/** A finished async payload load: node index + render-ready quad geometry.
 *  Each point is expanded to 4 corner verts (corner sign in UV0) + 6 indices
 *  (two triangles), billboarded/sized/rounded in the material. NumPoints is the
 *  source point count (Verts.Num() == NumPoints*4, Indices.Num() == NumPoints*6). */
struct FPFLoadResult
{
	int32 NodeIndex = INDEX_NONE;
	int32 NumPoints = 0;
	TArray<FDynamicMeshVertex> Verts;
	TArray<uint32> Indices;
};

/**
 * Loads a converted PointForge octree (meta.bin + hierarchy.bin, resident) and
 * streams node payloads off disk on a background thread — the read side of the
 * out-of-core viewer. Port of pfview's OctreeStore.
 *
 * Threading: RequestLoad() enqueues (game/render thread); a worker reads
 * octree.bin + builds FDynamicMeshVertex arrays; PopResult() drains finished
 * loads (render thread). MarkEvicted() lets an evicted node be requested again.
 */
class POINTFORGEVIEWER_API FPFOctreeStore : public FRunnable
{
public:
	FPFOctreeStore() = default;
	virtual ~FPFOctreeStore() override;

	/** Opens an octree directory, precomputes cubes, starts the worker. */
	bool Open(const FString& InOctreeDir, double InUnitScale, bool bInColorIs16Bit);

	bool IsValid() const { return bValid; }

	const FPFFileMetadata& GetMeta() const { return Meta; }
	const TArray<FPFNodeRecord>& GetNodes() const { return Nodes; }
	const FPFNodeCube& GetCube(int32 Index) const { return Cubes[Index]; }
	int32 GetRootIndex() const { return static_cast<int32>(Meta.RootNodeIndex); }

	/** Cube centre in source/world units (positions are rendered relative to it). */
	FVector GetCubeCentre() const { return Centre; }

	/** Node sample spacing in source units (rootSpacing / 2^level). */
	double GetNodeSpacing(uint8 Level) const;

	//~ Streaming -----------------------------------------------------------
	/** Enqueue an async payload load (no-op if already queued/in-flight). */
	void RequestLoad(int32 NodeIndex);
	/** Drain one finished load; false if none ready. */
	bool PopResult(FPFLoadResult& Out);
	/** Mark a node no longer resident, so it can be requested again. */
	void MarkEvicted(int32 NodeIndex);
	int32 PendingRequests();

	//~ FRunnable -----------------------------------------------------------
	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	void ComputeCubes();
	void StopWorker();
	FColor ColorOf(const FPFPackedPoint& P) const;

	bool bValid = false;
	FString OctreeBinPath;
	FPFFileMetadata Meta;
	TArray<FPFNodeRecord> Nodes;
	TArray<FPFNodeCube> Cubes;
	FVector Centre = FVector::ZeroVector;
	double UnitScale = 100.0;
	bool bColorIs16Bit = true;
	bool bHasColor = false;
	bool bHasClassification = false;
	uint32 CompressionType = 0;  // 0 = none, 1 = zstd

	// Worker / queues.
	FRunnableThread* Thread = nullptr;
	FEvent* WakeEvent = nullptr;
	FThreadSafeBool bStop = false;

	TQueue<int32, EQueueMode::Mpsc> RequestQueue;
	TQueue<FPFLoadResult, EQueueMode::Mpsc> ResultQueue;

	FCriticalSection InFlightCS;
	TSet<int32> InFlight;          // queued or loading; guards dedupe + re-request
	FThreadSafeCounter PendingCount;
};
