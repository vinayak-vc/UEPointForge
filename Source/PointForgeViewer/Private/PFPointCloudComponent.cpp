#include "PFPointCloudComponent.h"

#include "PFOctreeStore.h"
#include "PFPointCloudSceneProxy.h"

UPFPointCloudComponent::UPFPointCloudComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	CastShadow = false;
	bUseAsOccluder = false;
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool UPFPointCloudComponent::OpenOctreeDir(const FString& OctreeDir)
{
	TSharedPtr<FPFOctreeStore> NewStore = MakeShared<FPFOctreeStore>();
	if (!NewStore->Open(OctreeDir))
	{
		return false;
	}

	Store = MoveTemp(NewStore);
	MarkRenderStateDirty(); // recreate the scene proxy
	UpdateBounds();
	return true;
}

void UPFPointCloudComponent::Close()
{
	Store.Reset();
	MarkRenderStateDirty();
	UpdateBounds();
}

FPrimitiveSceneProxy* UPFPointCloudComponent::CreateSceneProxy()
{
	if (Store.IsValid() && Store->IsValid())
	{
		return new FPFPointCloudSceneProxy(this);
	}
	return nullptr;
}

FBoxSphereBounds UPFPointCloudComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (Store.IsValid() && Store->IsValid())
	{
		const FPFFileMetadata& M = Store->GetMeta();
		const double Half = M.CubeSize * 0.5 * static_cast<double>(UnitScale);
		const FBox LocalBox(FVector(-Half, -Half, -Half), FVector(Half, Half, Half));
		return FBoxSphereBounds(LocalBox).TransformBy(LocalToWorld);
	}

	return FBoxSphereBounds(FVector::ZeroVector, FVector(100.f), 100.f).TransformBy(LocalToWorld);
}
