// Copyright. PointForge Viewer — runtime loader for PointForgeNative.dll.
//
// The DLL is loaded with FPlatformProcess (LoadLibrary/GetProcAddress) rather
// than linked through an import library, so the plugin builds without the DLL's
// .lib and a missing DLL degrades to "feature unavailable" instead of a failed
// module load.
#pragma once

#include "CoreMinimal.h"

// C API types come from the PointForge repo (src/library/native, src/library/unity).
// Those headers declare functions with __declspec(dllimport); we only take their
// addresses through the function pointers below, so no import library is needed.
#include "PointForgeSplatC.h"

// Function pointer types, one per exported symbol we use.
typedef PFSplatCloud* (*PF_Splat_Open_Fn)(const char* PathUtf8);
typedef void          (*PF_Splat_Close_Fn)(PFSplatCloud* Cloud);
typedef int32         (*PF_Splat_GetInfo_Fn)(PFSplatCloud* Cloud, PFSplatInfo* Out);
typedef int32         (*PF_Splat_Update_Fn)(PFSplatCloud* Cloud, const PFSplatCamera* Cam,
                                            PFSplatInstance* Out, int32 Capacity);
typedef int32         (*PF_Splat_LastVisibleCount_Fn)(PFSplatCloud* Cloud);

typedef uint32        (*PF_GetVersion_Fn)();

/** Resolved entry points of PointForgeNative.dll. Game-thread object. */
class POINTFORGEVIEWER_API FPFNativeApi
{
public:
	/** Loads the DLL from an explicit path, or from the plugin's Binaries/ThirdParty folder. */
	bool Load(const FString& DllPath = FString());
	void Unload();

	bool IsLoaded() const { return DllHandle != nullptr; }

	PF_Splat_Open_Fn               Splat_Open = nullptr;
	PF_Splat_Close_Fn              Splat_Close = nullptr;
	PF_Splat_GetInfo_Fn            Splat_GetInfo = nullptr;
	PF_Splat_Update_Fn             Splat_Update = nullptr;
	PF_Splat_LastVisibleCount_Fn   Splat_LastVisibleCount = nullptr;
	PF_GetVersion_Fn               GetVersion = nullptr;

	/** Singleton accessor (game thread only). */
	static FPFNativeApi& Get();

private:
	void* DllHandle = nullptr;
};
