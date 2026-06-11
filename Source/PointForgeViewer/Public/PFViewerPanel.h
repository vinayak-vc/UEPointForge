#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PFViewerPanel.generated.h"

class UPFPointCloudComponent;
class UTextBlock;
class USlider;
class UCheckBox;

/**
 * pfview-style overlay panel, built entirely in C++ (no WidgetBlueprint asset).
 * Shows live streaming stats and drives the component's tunables.
 */
UCLASS()
class POINTFORGEVIEWER_API UPFViewerPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UPFViewerPanel(const FObjectInitializer& ObjectInitializer);

	/** Bind the panel to a point-cloud component (its stats + tunables). */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetTarget(UPFPointCloudComponent* InTarget);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION() void OnPointSizeChanged(float Value);
	UFUNCTION() void OnSseChanged(float Value);
	UFUNCTION() void OnGpuBudgetChanged(float Value);
	UFUNCTION() void OnUploadsChanged(float Value);
	UFUNCTION() void OnRoundChanged(bool bChecked);
	UFUNCTION() void OnAttenuateChanged(bool bChecked);

	TWeakObjectPtr<UPFPointCloudComponent> Target;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatsText;
	UPROPERTY(Transient) TObjectPtr<USlider> PointSizeSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> SseSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> GpuSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> UploadsSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PointSizeLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SseLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GpuLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UploadsLabel;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> RoundCheck;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> AttenCheck;
};
