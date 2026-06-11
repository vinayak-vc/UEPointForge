#include "PFPointCloudActor.h"

#include "PFPointCloudComponent.h"
#include "PFConvert.h"
#include "PFViewerPanel.h"
#include "PFConvertPanel.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"

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
			return;
		}
		if (Self->bShowPanel)
		{
			Self->ShowPanel();
		}
	});

	UE_LOG(LogPointForgeActor, Log, TEXT("LoadPointCloudFile: %s -> cache %s"), *SourceFile, *CacheDir);
	FPFConvert::ConvertAsync(SourceFile, Exe, Done);
}

void APFPointCloudActor::ShowPanel()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// AddToViewport needs a game viewport — PIE or packaged, not edit mode.
		UE_LOG(LogPointForgeActor, Warning, TEXT("ShowPanel: no game world (run in PIE/standalone)"));
		return;
	}

	if (!Panel)
	{
		TSubclassOf<UUserWidget> Cls = PanelClass ? PanelClass : TSubclassOf<UUserWidget>(UPFViewerPanel::StaticClass());
		Panel = Cast<UPFViewerPanel>(CreateWidget(World, Cls));
	}
	if (!Panel)
	{
		UE_LOG(LogPointForgeActor, Error, TEXT("ShowPanel: PanelClass must derive from UPFViewerPanel"));
		return;
	}

	Panel->SetTarget(PointCloud);
	if (!Panel->IsInViewport())
	{
		Panel->AddToViewport(1000);
	}
	Panel->SetAlignmentInViewport(FVector2D(0.f, 0.f));
	Panel->SetPositionInViewport(FVector2D(20.f, 20.f), /*bRemoveDPIScale*/ false);
	Panel->SetDesiredSizeInViewport(FVector2D(380.f, 560.f));
}

void APFPointCloudActor::HidePanel()
{
	if (Panel)
	{
		Panel->RemoveFromParent();
	}
}

float APFPointCloudActor::GetCacheSizeMB(const FString& SourceFile) const
{
	return static_cast<float>(FPFConvert::GetCacheSizeBytes(SourceFile) / (1024.0 * 1024.0));
}

bool APFPointCloudActor::ClearCacheForFile(const FString& SourceFile)
{
	return FPFConvert::ClearCacheFor(SourceFile);
}

int32 APFPointCloudActor::ClearAllCachesForDir(const FString& SourceFile)
{
	return FPFConvert::ClearAllCachesUnderSource(SourceFile);
}

void APFPointCloudActor::HideConvertPanel()
{
	if (ConvertPanel)
	{
		ConvertPanel->RemoveFromParent();
	}
}

void APFPointCloudActor::ShowConvertPanel()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogPointForgeActor, Warning, TEXT("ShowConvertPanel: no game world (run in PIE/standalone)"));
		return;
	}

	if (!ConvertPanel)
	{
		TSubclassOf<UUserWidget> Cls = ConvertPanelClass ? ConvertPanelClass : TSubclassOf<UUserWidget>(UPFConvertPanel::StaticClass());
		ConvertPanel = Cast<UPFConvertPanel>(CreateWidget(World, Cls));
	}
	if (!ConvertPanel)
	{
		UE_LOG(LogPointForgeActor, Error, TEXT("ShowConvertPanel: ConvertPanelClass must derive from UPFConvertPanel"));
		return;
	}

	ConvertPanel->SetTarget(this);
	if (!ConvertPanel->IsInViewport())
	{
		ConvertPanel->AddToViewport(900);
	}
	// Centre the panel in the viewport. Alignment 0.5,0.5 puts the widget's centre
	// on the position; position (0,0) at centre anchor = dead centre.
	// ConvertPanel->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
	// ConvertPanel->SetAnchorsInViewport(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
	// ConvertPanel->SetPositionInViewport(FVector2D(0.f, 0.f), false);
	// ConvertPanel->SetDesiredSizeInViewport(FVector2D(440.f, 560.f));
}
