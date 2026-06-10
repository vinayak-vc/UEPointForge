#include "PFPointCloudActor.h"

#include "PFPointCloudComponent.h"
#include "PFConvert.h"

DEFINE_LOG_CATEGORY_STATIC(LogPointForgeActor, Log, All);

APFPointCloudActor::APFPointCloudActor()
{
	PrimaryActorTick.bCanEverTick = false;

	PointCloud = CreateDefaultSubobject<UPFPointCloudComponent>(TEXT("PointCloud"));
	RootComponent = PointCloud;
}

void APFPointCloudActor::LoadPointCloudFile(const FString& SourceFile)
{
	if (SourceFile.IsEmpty())
	{
		UE_LOG(LogPointForgeActor, Warning, TEXT("LoadPointCloudFile: empty path"));
		return;
	}

	const FString Exe = FPFConvert::LocatePfConvert(PfConvertExePath);
	const FString CacheDir = FPFConvert::GetCacheDirFor(SourceFile);

	TWeakObjectPtr<APFPointCloudActor> WeakThis(this);

	FPFConvertDone Done;
	Done.BindLambda([WeakThis, CacheDir](bool bSuccess)
	{
		APFPointCloudActor* Self = WeakThis.Get();
		if (!Self)
		{
			return; // actor destroyed while converting
		}
		if (!bSuccess)
		{
			UE_LOG(LogPointForgeActor, Error, TEXT("LoadPointCloudFile: conversion failed"));
			return;
		}
		if (!Self->PointCloud->OpenOctreeDir(CacheDir))
		{
			UE_LOG(LogPointForgeActor, Error, TEXT("LoadPointCloudFile: OpenOctreeDir failed for %s"), *CacheDir);
		}
	});

	UE_LOG(LogPointForgeActor, Log, TEXT("LoadPointCloudFile: %s -> cache %s"), *SourceFile, *CacheDir);
	FPFConvert::ConvertAsync(SourceFile, Exe, Done);
}
