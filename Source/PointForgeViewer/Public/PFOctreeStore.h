#pragma once

#include "CoreMinimal.h"
#include "PFOctreeFormat.h"

/**
 * Loads a converted PointForge octree directory (meta.bin + hierarchy.bin) and
 * reads per-node PackedPoint payloads from octree.bin on demand.
 *
 * The hierarchy is tiny (52 bytes/node) and kept resident; point payloads are
 * streamed. This is the read side of the on-disk format invariant — keep the
 * mirrored structs in PFOctreeFormat.h in sync with the canonical headers.
 */
class POINTFORGEVIEWER_API FPFOctreeStore
{
public:
	/** Opens an octree directory. Returns false on missing/invalid files. */
	bool Open(const FString& InOctreeDir);

	bool IsValid() const { return bValid; }

	const FPFFileMetadata& GetMeta() const { return Meta; }
	const TArray<FPFNodeRecord>& GetNodes() const { return Nodes; }
	int32 GetRootIndex() const { return static_cast<int32>(Meta.RootNodeIndex); }

	/**
	 * Reads a node's payload into Out (PackedPoint array). SYNCHRONOUS — call
	 * from a worker thread for large nodes. Returns true on success (an empty
	 * node returns true with Out emptied).
	 */
	bool ReadNodePoints(const FPFNodeRecord& Node, TArray<FPFPackedPoint>& Out) const;

	/** Octree root cube in SOURCE units (no UE transform applied). */
	FBox GetSourceCubeBounds() const;

private:
	bool bValid = false;
	FString OctreeDir;
	FString OctreeBinPath;
	FPFFileMetadata Meta;
	TArray<FPFNodeRecord> Nodes;
};
