#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PFViewerPanel.generated.h"

class UPFPointCloudComponent;
class APFPointCloudActor;
class UTextBlock;
class USlider;
class UCheckBox;
class UComboBoxString;
class UButton;

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

	/** Bind the owner actor so EDL sliders can reach SetEDLParams. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetOwnerActor(APFPointCloudActor* InActor);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION() void OnPointSizeChanged(float Value);
	UFUNCTION() void OnSseChanged(float Value);
	UFUNCTION() void OnGpuBudgetChanged(float Value);
	UFUNCTION() void OnUploadsChanged(float Value);
	UFUNCTION() void OnPointCountLimitChanged(float Value);
	UFUNCTION() void OnRoundChanged(bool bChecked);
	UFUNCTION() void OnAttenuateChanged(bool bChecked);

	// Extended runtime controls (mirror every UPFPointCloudComponent UPROPERTY worth tuning live).
	UFUNCTION() void OnSoftRoundChanged(float Value);
	UFUNCTION() void OnEmissiveBoostChanged(float Value);
	UFUNCTION() void OnUnitScaleChanged(float Value);
	UFUNCTION() void OnElevationMinChanged(float Value);
	UFUNCTION() void OnElevationMaxChanged(float Value);
	UFUNCTION() void OnColorModeSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION() void OnUseClipChanged(bool bChecked);
	UFUNCTION() void OnColor16BitChanged(bool bChecked);
	UFUNCTION() void OnEDLStrengthChanged(float Value);
	UFUNCTION() void OnEDLRadiusChanged(float Value);
	UFUNCTION() void OnEDLEnableChanged(bool bChecked);
	UFUNCTION() void OnFitElevationClicked();

	TWeakObjectPtr<UPFPointCloudComponent> Target;
	TWeakObjectPtr<APFPointCloudActor>     OwnerActor;   // for EDL params

	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatsText;
	UPROPERTY(Transient) TObjectPtr<USlider> PointSizeSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> SseSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> GpuSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> UploadsSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> PointCountLimitSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PointSizeLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SseLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GpuLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UploadsLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PointCountLimitLabel;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> RoundCheck;
	UPROPERTY(Transient) TObjectPtr<UCheckBox> AttenCheck;

	// Extended controls + their labels.
	UPROPERTY(Transient) TObjectPtr<USlider>     SoftRoundSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  SoftRoundLabel;
	UPROPERTY(Transient) TObjectPtr<USlider>     EmissiveSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  EmissiveLabel;
	UPROPERTY(Transient) TObjectPtr<USlider>     UnitScaleSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  UnitScaleLabel;
	UPROPERTY(Transient) TObjectPtr<USlider>     ElevMinSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ElevMinLabel;
	UPROPERTY(Transient) TObjectPtr<USlider>     ElevMaxSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ElevMaxLabel;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> ColorModeCombo;
	UPROPERTY(Transient) TObjectPtr<UCheckBox>   UseClipCheck;
	UPROPERTY(Transient) TObjectPtr<UCheckBox>   Color16BitCheck;
	UPROPERTY(Transient) TObjectPtr<UButton>     FitElevationBtn;

	// EDL controls.
	UPROPERTY(Transient) TObjectPtr<USlider>     EDLStrengthSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  EDLStrengthLabel;
	UPROPERTY(Transient) TObjectPtr<USlider>     EDLRadiusSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  EDLRadiusLabel;
	UPROPERTY(Transient) TObjectPtr<UCheckBox>   EDLEnableCheck;
};
