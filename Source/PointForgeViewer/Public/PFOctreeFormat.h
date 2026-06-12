#pragma once

#include "CoreMinimal.h"

// ---------------------------------------------------------------------------
// Mirror of PointForge's on-disk octree format (v1).
//
// CANONICAL definitions live in:
//   <PointForgeRoot>/src/common/OctreeFormat.h   (FileMetadata, NodeRecord)
//   <PointForgeRoot>/src/common/PointFormat.h    (PackedPoint)
//
// They are mirrored here (not #included) so this UE module stays free of
// pfcore's STL-heavy headers. KEEP IN SYNC with the canonical structs — the
// static_asserts below guard the byte layouts (52 / 20 bytes), matching the
// asserts in the canonical headers.
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// meta.bin — magic "PFO1".
struct FPFFileMetadata
{
	char   Magic[4];        // "PFO1"
	uint32 Version;         // = 1 or 2
	uint64 PointCount;      // total points across all nodes
	double BbMin[3];        // true AABB of the data (world coords)
	double BbMax[3];
	double CubeMin[3];      // octree root cube origin (world coords)
	double CubeSize;        // octree root cube edge length
	double Scale[3];        // quantization scale (PackedPoint -> world)
	double Offset[3];       // quantization offset
	double RootSpacing;     // sample spacing at the root node
	uint32 BytesPerPoint;   // == sizeof(FPFPackedPoint) (20 for v1, 22 for v2)
	uint32 HasColor;        // 1 if colour is meaningful
	uint32 NodeCount;       // number of FPFNodeRecord entries in hierarchy.bin
	uint32 RootNodeIndex;   // index of the root node within hierarchy.bin
	uint32 HasClassification; // 1 if classification codes are meaningful (v2+; 0 for v1)
	uint32 CompressionType;   // 0 = none, 1 = zstd per-node (v2+; 0 for v1)
};

// hierarchy.bin — array of NodeCount records, root at RootNodeIndex.
struct FPFNodeRecord
{
	uint8  Level;           // 0 = root
	uint8  ChildMask;       // bit o set => Children[o] != PF_NO_CHILD
	uint16 Reserved;
	uint32 PointCount;      // points stored in THIS node
	uint64 ByteOffset;      // payload offset within octree.bin
	uint32 ByteSize;        // payload size in bytes (may be < PointCount*BytesPerPoint when compressed)
	uint32 Children[8];     // global node index per octant, or PF_NO_CHILD
};

// octree.bin — concatenated, tightly packed per-node payloads.
// v1: 20 bytes per point (no classification).
// v2: 22 bytes per point (with classification + padding).
struct FPFPackedPoint
{
	int32  X, Y, Z;         // quantized position = round((world - Offset) / Scale)
	uint16 R, G, B;
	uint16 Intensity;
	uint8  Classification;  // ASPRS LAS class code (0–255); 0 for v1 data
	uint8  Pad;             // alignment padding
};

#pragma pack(pop)

static constexpr uint32 PF_NO_CHILD = 0xFFFFFFFFu;

static_assert(sizeof(FPFNodeRecord) == 52, "FPFNodeRecord must match NodeRecord (52 bytes)");
static_assert(sizeof(FPFPackedPoint) == 22, "FPFPackedPoint must match PackedPoint v2 (22 bytes)");

// Subdivide a cube into the cube of octant O. Numbering: (x<<2)|(y<<1)|z,
// 0 = low half. SINGLE SOURCE OF TRUTH is childCube() in OctreeFormat.h — this
// is a faithful copy; do not diverge.
FORCEINLINE void PFChildCube(const double ParentMin[3], double ParentSize, int32 O,
                             double OutMin[3], double& OutSize)
{
	OutSize = ParentSize * 0.5;
	OutMin[0] = ParentMin[0] + ((O & 4) ? OutSize : 0.0);
	OutMin[1] = ParentMin[1] + ((O & 2) ? OutSize : 0.0);
	OutMin[2] = ParentMin[2] + ((O & 1) ? OutSize : 0.0);
}
