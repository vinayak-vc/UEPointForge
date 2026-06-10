#include "PFOctreeStore.h"

#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogPointForge, Log, All);

bool FPFOctreeStore::Open(const FString& InOctreeDir)
{
	bValid = false;
	Nodes.Reset();
	OctreeDir = InOctreeDir;

	const FString MetaPath = FPaths::Combine(OctreeDir, TEXT("meta.bin"));
	const FString HierPath = FPaths::Combine(OctreeDir, TEXT("hierarchy.bin"));
	OctreeBinPath = FPaths::Combine(OctreeDir, TEXT("octree.bin"));

	// --- meta.bin ---
	TArray<uint8> MetaBytes;
	if (!FFileHelper::LoadFileToArray(MetaBytes, *MetaPath))
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: cannot read %s"), *MetaPath);
		return false;
	}
	if (MetaBytes.Num() < static_cast<int32>(sizeof(FPFFileMetadata)))
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: meta.bin too small (%d bytes)"), MetaBytes.Num());
		return false;
	}
	FMemory::Memcpy(&Meta, MetaBytes.GetData(), sizeof(FPFFileMetadata));
	if (FMemory::Memcmp(Meta.Magic, "PFO1", 4) != 0)
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: bad magic (expected PFO1)"));
		return false;
	}
	if (Meta.BytesPerPoint != sizeof(FPFPackedPoint))
	{
		UE_LOG(LogPointForge, Warning, TEXT("OctreeStore: bytesPerPoint=%u, expected %d"),
			Meta.BytesPerPoint, static_cast<int32>(sizeof(FPFPackedPoint)));
	}

	// --- hierarchy.bin ---
	TArray<uint8> HierBytes;
	if (!FFileHelper::LoadFileToArray(HierBytes, *HierPath))
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: cannot read %s"), *HierPath);
		return false;
	}
	const int32 Count = static_cast<int32>(Meta.NodeCount);
	const int64 Need = static_cast<int64>(Count) * sizeof(FPFNodeRecord);
	if (Count <= 0 || HierBytes.Num() < Need)
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: hierarchy.bin has %d bytes, need %lld"),
			HierBytes.Num(), Need);
		return false;
	}
	Nodes.SetNumUninitialized(Count);
	FMemory::Memcpy(Nodes.GetData(), HierBytes.GetData(), Need);

	if (Meta.RootNodeIndex >= static_cast<uint32>(Count))
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: rootNodeIndex %u out of range (%d nodes)"),
			Meta.RootNodeIndex, Count);
		return false;
	}

	bValid = true;
	UE_LOG(LogPointForge, Log, TEXT("OctreeStore: opened %s (%llu points, %d nodes, cube %.3f)"),
		*OctreeDir, Meta.PointCount, Count, Meta.CubeSize);
	return true;
}

bool FPFOctreeStore::ReadNodePoints(const FPFNodeRecord& Node, TArray<FPFPackedPoint>& Out) const
{
	Out.Reset();
	if (!bValid)
	{
		return false;
	}
	if (Node.ByteSize == 0 || Node.PointCount == 0)
	{
		return true; // empty node is a valid result
	}

	// NOTE: opens octree.bin per call for clarity. For the streaming renderer,
	// keep one persistent IFileHandle (or memory-map) and issue async reads.
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> Handle(PF.OpenRead(*OctreeBinPath));
	if (!Handle.IsValid())
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: cannot open %s"), *OctreeBinPath);
		return false;
	}

	const int32 NumPts = static_cast<int32>(Node.PointCount);
	Out.SetNumUninitialized(NumPts);

	if (!Handle->Seek(static_cast<int64>(Node.ByteOffset)))
	{
		Out.Reset();
		return false;
	}
	if (!Handle->Read(reinterpret_cast<uint8*>(Out.GetData()), static_cast<int64>(Node.ByteSize)))
	{
		Out.Reset();
		return false;
	}
	return true;
}

FBox FPFOctreeStore::GetSourceCubeBounds() const
{
	const FVector Min(Meta.CubeMin[0], Meta.CubeMin[1], Meta.CubeMin[2]);
	const FVector Max = Min + FVector(Meta.CubeSize, Meta.CubeSize, Meta.CubeSize);
	return FBox(Min, Max);
}
