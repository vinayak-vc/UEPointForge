#include "PFOctreeStore.h"

#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/Compression.h"

THIRD_PARTY_INCLUDES_START
#include <zstd.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY(LogPointForge);

FPFOctreeStore::~FPFOctreeStore()
{
	StopWorker();
}

bool FPFOctreeStore::Open(const FString& InOctreeDir, double InUnitScale, bool bInColorIs16Bit)
{
	bValid = false;
	Nodes.Reset();
	Cubes.Reset();
	UnitScale = InUnitScale;
	bColorIs16Bit = bInColorIs16Bit;

	const FString MetaPath = FPaths::Combine(InOctreeDir, TEXT("meta.bin"));
	const FString HierPath = FPaths::Combine(InOctreeDir, TEXT("hierarchy.bin"));
	OctreeBinPath = FPaths::Combine(InOctreeDir, TEXT("octree.bin"));

	// v1 meta.bin is smaller (no HasClassification/CompressionType fields).
	// Accept either v1 or v2 size. Zero the struct first so v1 new fields are 0.
	constexpr int32 V1MetaSize = offsetof(FPFFileMetadata, HasClassification);
	TArray<uint8> MetaBytes;
	if (!FFileHelper::LoadFileToArray(MetaBytes, *MetaPath) ||
		MetaBytes.Num() < V1MetaSize)
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: cannot read %s"), *MetaPath);
		return false;
	}
	FMemory::Memzero(&Meta, sizeof(FPFFileMetadata));
	const int32 CopySize = FMath::Min(MetaBytes.Num(), static_cast<int32>(sizeof(FPFFileMetadata)));
	FMemory::Memcpy(&Meta, MetaBytes.GetData(), CopySize);
	if (FMemory::Memcmp(Meta.Magic, "PFO1", 4) != 0)
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: bad magic"));
		return false;
	}

	TArray<uint8> HierBytes;
	const int32 Count = static_cast<int32>(Meta.NodeCount);
	const int64 Need = static_cast<int64>(Count) * sizeof(FPFNodeRecord);
	if (!FFileHelper::LoadFileToArray(HierBytes, *HierPath) || Count <= 0 || HierBytes.Num() < Need)
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: bad hierarchy.bin"));
		return false;
	}
	Nodes.SetNumUninitialized(Count);
	FMemory::Memcpy(Nodes.GetData(), HierBytes.GetData(), Need);

	if (Meta.RootNodeIndex >= static_cast<uint32>(Count))
	{
		UE_LOG(LogPointForge, Error, TEXT("OctreeStore: root index out of range"));
		return false;
	}

	bHasColor = (Meta.HasColor != 0);
	bHasClassification = (Meta.HasClassification != 0);
	CompressionType = Meta.CompressionType;
	Centre = FVector(
		Meta.CubeMin[0] + Meta.CubeSize * 0.5,
		Meta.CubeMin[1] + Meta.CubeSize * 0.5,
		Meta.CubeMin[2] + Meta.CubeSize * 0.5);

	ComputeCubes();

	bValid = true;

	// Start the streaming worker.
	bStop = false;
	WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
	Thread = FRunnableThread::Create(this, TEXT("PFOctreeStoreLoader"), 0, TPri_BelowNormal);

	UE_LOG(LogPointForge, Log, TEXT("OctreeStore: opened (%llu pts, %d nodes, cube %.3f, v%u, compress=%u)"),
		Meta.PointCount, Count, Meta.CubeSize, Meta.Version, CompressionType);
	return true;
}

void FPFOctreeStore::ComputeCubes()
{
	Cubes.SetNum(Nodes.Num());
	const int32 Root = GetRootIndex();
	Cubes[Root].Min[0] = Meta.CubeMin[0];
	Cubes[Root].Min[1] = Meta.CubeMin[1];
	Cubes[Root].Min[2] = Meta.CubeMin[2];
	Cubes[Root].Size = Meta.CubeSize;

	TArray<int32> Stack;
	Stack.Push(Root);
	while (Stack.Num() > 0)
	{
		const int32 I = Stack.Pop(EAllowShrinking::No);
		const FPFNodeRecord& Rec = Nodes[I];
		for (int32 O = 0; O < 8; ++O)
		{
			const uint32 C = Rec.Children[O];
			if (C != PF_NO_CHILD && C < static_cast<uint32>(Nodes.Num()))
			{
				double ChildMin[3];
				double ChildSize;
				PFChildCube(Cubes[I].Min, Cubes[I].Size, O, ChildMin, ChildSize);
				Cubes[C].Min[0] = ChildMin[0];
				Cubes[C].Min[1] = ChildMin[1];
				Cubes[C].Min[2] = ChildMin[2];
				Cubes[C].Size = ChildSize;
				Stack.Push(static_cast<int32>(C));
			}
		}
	}
}

double FPFOctreeStore::GetNodeSpacing(uint8 Level) const
{
	return Meta.RootSpacing / static_cast<double>(1ull << Level);
}

FColor FPFOctreeStore::ColorOf(const FPFPackedPoint& P) const
{
	// Alpha carries intensity so the billboard material can colour by intensity.
	// Many LAS files store 8-bit or 12-bit intensity in the 16-bit field. 
	// We use a simple heuristic to preserve brightness.
	uint8 IntensityByte;
	if (P.Intensity <= 255)
	{
		IntensityByte = static_cast<uint8>(P.Intensity);
	}
	else if (P.Intensity <= 4095)
	{
		IntensityByte = static_cast<uint8>((P.Intensity * 255) / 4095);
	}
	else
	{
		IntensityByte = static_cast<uint8>(P.Intensity >> 8);
	}

	if (!bHasColor)
	{
		// No RGB on disk → put intensity in all channels too so RGB mode looks like grey.
		return FColor(IntensityByte, IntensityByte, IntensityByte, IntensityByte);
	}
	if (bColorIs16Bit)
	{
		return FColor(uint8(P.R >> 8), uint8(P.G >> 8), uint8(P.B >> 8), IntensityByte);
	}
	return FColor(uint8(P.R & 0xFF), uint8(P.G & 0xFF), uint8(P.B & 0xFF), IntensityByte);
}

void FPFOctreeStore::RequestLoad(int32 NodeIndex)
{
	if (!bValid || NodeIndex < 0 || NodeIndex >= Nodes.Num())
	{
		return;
	}
	{
		FScopeLock Lock(&InFlightCS);
		if (InFlight.Contains(NodeIndex))
		{
			return;
		}
		InFlight.Add(NodeIndex);
	}
	PendingCount.Increment();
	RequestQueue.Enqueue(NodeIndex);
	if (WakeEvent)
	{
		WakeEvent->Trigger();
	}
}

bool FPFOctreeStore::PopResult(FPFLoadResult& Out)
{
	if (!ResultQueue.Dequeue(Out))
	{
		return false;
	}
	// Keep it in InFlight until it is uploaded+resident; the proxy calls
	// MarkEvicted when it later drops the node. (Re-request is gated by residency
	// on the proxy side, so we clear InFlight here to allow future reloads.)
	{
		FScopeLock Lock(&InFlightCS);
		InFlight.Remove(Out.NodeIndex);
	}
	return true;
}

void FPFOctreeStore::MarkEvicted(int32 NodeIndex)
{
	FScopeLock Lock(&InFlightCS);
	InFlight.Remove(NodeIndex);
}

int32 FPFOctreeStore::PendingRequests()
{
	return PendingCount.GetValue();
}

uint32 FPFOctreeStore::Run()
{
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	// Prefer memory-mapping octree.bin — kernel pages in on demand, no per-read
	// syscall. Falls back to a persistent IFileHandle when mmap isn't available.
	TUniquePtr<IMappedFileHandle> Mapped(PF.OpenMapped(*OctreeBinPath));
	TUniquePtr<IMappedFileRegion> Region;
	if (Mapped.IsValid())
	{
		Region.Reset(Mapped->MapRegion(0, Mapped->GetFileSize()));
	}
	const uint8* MappedBase = Region.IsValid() ? Region->GetMappedPtr() : nullptr;
	const int64  MappedSize = Region.IsValid() ? Region->GetMappedSize() : 0;

	TUniquePtr<IFileHandle> Handle;
	if (!MappedBase)
	{
		Handle.Reset(PF.OpenRead(*OctreeBinPath));
		if (!Handle.IsValid())
		{
			UE_LOG(LogPointForge, Error, TEXT("OctreeStore worker: cannot open %s"), *OctreeBinPath);
			return 1;
		}
		UE_LOG(LogPointForge, Log, TEXT("OctreeStore worker: mmap unavailable, using seek+read"));
	}
	else
	{
		UE_LOG(LogPointForge, Log, TEXT("OctreeStore worker: mmap'd octree.bin (%.1f MB)"),
			MappedSize / (1024.0 * 1024.0));
	}

	TArray<FPFPackedPoint> Pts;

	while (!bStop)
	{
		int32 Idx = INDEX_NONE;
		if (!RequestQueue.Dequeue(Idx))
		{
			if (WakeEvent)
			{
				WakeEvent->Wait();
			}
			continue;
		}

		if (Idx < 0 || Idx >= Nodes.Num())
		{
			PendingCount.Decrement();
			continue;
		}

		const FPFNodeRecord& N = Nodes[Idx];
		FPFLoadResult Result;
		Result.NodeIndex = Idx;

		if (N.ByteSize > 0 && N.PointCount > 0)
		{
			const int32 NumPts = static_cast<int32>(N.PointCount);
			const int64 Off = static_cast<int64>(N.ByteOffset);
			const int64 Sz  = static_cast<int64>(N.ByteSize);

			// Read raw bytes from disk (may be compressed).
			TArray<uint8> RawBytes;
			bool bRead = false;
			if (MappedBase && Off + Sz <= MappedSize)
			{
				RawBytes.SetNumUninitialized(static_cast<int32>(Sz));
				FMemory::Memcpy(RawBytes.GetData(), MappedBase + Off, static_cast<SIZE_T>(Sz));
				bRead = true;
			}
			else if (Handle.IsValid() && Handle->Seek(Off))
			{
				RawBytes.SetNumUninitialized(static_cast<int32>(Sz));
				bRead = Handle->Read(RawBytes.GetData(), Sz);
			}

			if (bRead)
			{
				// Decompress if needed.
				const int32 BytesPerPt = static_cast<int32>(Meta.BytesPerPoint);
				const int64 UncompressedSize = static_cast<int64>(NumPts) * BytesPerPt;

				TArray<uint8> PointBytes;
				if (CompressionType == 1 && Sz < UncompressedSize)
				{
					PointBytes.SetNumUninitialized(static_cast<int32>(UncompressedSize));
					size_t const DecompressedSize = ZSTD_decompress(
						PointBytes.GetData(), static_cast<size_t>(UncompressedSize),
						RawBytes.GetData(), static_cast<size_t>(Sz)
					);
					if (ZSTD_isError(DecompressedSize))
					{
						UE_LOG(LogPointForge, Warning, TEXT("OctreeStore: Zstandard decompression failed for node %d, error: %hs"),
							Idx, ZSTD_getErrorName(DecompressedSize));
						PointBytes = MoveTemp(RawBytes);
					}
				}
				else
				{
					PointBytes = MoveTemp(RawBytes);
				}

				// Convert v1 20-byte points to v2 22-byte if needed.
				if (BytesPerPt == 20)
				{
					// v1 format: 20 bytes per point, no classification.
					Pts.SetNumUninitialized(NumPts);
					const uint8* Src = PointBytes.GetData();
					for (int32 i = 0; i < NumPts; ++i)
					{
						FPFPackedPoint& Dst = Pts[i];
						FMemory::Memcpy(&Dst, Src + i * 20, 20);
						Dst.Classification = 0;
						Dst.Pad = 0;
					}
				}
				else
				{
					// v2 format: 22 bytes, direct copy.
					Pts.SetNumUninitialized(NumPts);
					FMemory::Memcpy(Pts.GetData(), PointBytes.GetData(),
						static_cast<SIZE_T>(NumPts) * sizeof(FPFPackedPoint));
				}

				// Expand each point to a 4-corner quad + 6 indices. Corner sign goes
				// in UV0; classification goes in UV1.
				static const FVector2f Corners[4] = {
					FVector2f(-0.5f, -0.5f), FVector2f(0.5f, -0.5f),
					FVector2f(0.5f, 0.5f), FVector2f(-0.5f, 0.5f)
				};
				Result.NumPoints = NumPts;
				Result.Verts.SetNumUninitialized(NumPts * 4);
				Result.Indices.SetNumUninitialized(NumPts * 6);
				for (int32 i = 0; i < NumPts; ++i)
				{
					const FPFPackedPoint& P = Pts[i];
					const FVector3f Local(
						static_cast<float>((P.X * Meta.Scale[0] + Meta.Offset[0] - Centre.X) * UnitScale),
						static_cast<float>((P.Y * Meta.Scale[1] + Meta.Offset[1] - Centre.Y) * UnitScale),
						static_cast<float>((P.Z * Meta.Scale[2] + Meta.Offset[2] - Centre.Z) * UnitScale));
					const FColor Col = ColorOf(P);
					const float ClassFloat = static_cast<float>(P.Classification);

					const int32 v = i * 4;
					for (int32 c = 0; c < 4; ++c)
					{
						FDynamicMeshVertex& V = Result.Verts[v + c];
						V = FDynamicMeshVertex();
						V.Position = Local;                 // all 4 share the point position
						V.Color = Col;
						V.TextureCoordinate[0] = Corners[c]; // corner sign for the material
						V.TextureCoordinate[1] = FVector2f(ClassFloat, 0.0f); // classification for material palette
						V.SetTangents(FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1));
					}

					const int32 idx = i * 6;
					Result.Indices[idx + 0] = v;
					Result.Indices[idx + 1] = v + 1;
					Result.Indices[idx + 2] = v + 2;
					Result.Indices[idx + 3] = v;
					Result.Indices[idx + 4] = v + 2;
					Result.Indices[idx + 5] = v + 3;
				}
			}
		}

		ResultQueue.Enqueue(MoveTemp(Result));
		PendingCount.Decrement();
	}

	return 0;
}

void FPFOctreeStore::Stop()
{
	bStop = true;
	if (WakeEvent)
	{
		WakeEvent->Trigger();
	}
}

void FPFOctreeStore::StopWorker()
{
	if (Thread)
	{
		Stop();
		Thread->Kill(/*bShouldWait*/ true);
		delete Thread;
		Thread = nullptr;
	}
	if (WakeEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		WakeEvent = nullptr;
	}
}
