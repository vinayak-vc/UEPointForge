#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PFConvertPanel.generated.h"

class APFPointCloudActor;
class UEditableTextBox;
class USpinBox;
class UCheckBox;
class UButton;
class UTextBlock;

/**
 * Conversion control panel — built entirely in C++ (no WidgetBlueprint required).
 * Exposes all pfconvert parameters via UPFConvertSettings; edits auto-save.
 * Browse button opens a file-picker dialog (editor builds only).
 */
UCLASS()
class POINTFORGEVIEWER_API UPFConvertPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetTarget(APFPointCloudActor* InTarget);

	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetSourcePath(const FString& InPath);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void RefreshFromSettings();
	void RefreshConvertUi();

	UFUNCTION() void OnBrowseClicked();
	UFUNCTION() void OnSpacingChanged(float V);
	UFUNCTION() void OnLeafChanged(float V);
	UFUNCTION() void OnMaxDepthChanged(float V);
	UFUNCTION() void OnChunkDepthChanged(float V);
	UFUNCTION() void OnFlushChanged(float V);
	UFUNCTION() void OnKeepChunksChanged(bool b);
	UFUNCTION() void OnVerboseChanged(bool b);
	UFUNCTION() void OnCompressChanged(bool b);
	UFUNCTION() void OnConvertClicked();
	UFUNCTION() void OnResetClicked();
	UFUNCTION() void OnSaveClicked();
	UFUNCTION() void OnCancelClicked();
	UFUNCTION() void OnClearThisClicked();
	UFUNCTION() void OnClearAllClicked();

	TWeakObjectPtr<APFPointCloudActor> Target;

	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> PathBox;
	UPROPERTY(Transient) TObjectPtr<UButton>          BrowseBtn;
	UPROPERTY(Transient) TObjectPtr<USpinBox>         SpacingSpin;
	UPROPERTY(Transient) TObjectPtr<USpinBox>         LeafSpin;
	UPROPERTY(Transient) TObjectPtr<USpinBox>         MaxDepthSpin;
	UPROPERTY(Transient) TObjectPtr<USpinBox>         ChunkSpin;
	UPROPERTY(Transient) TObjectPtr<USpinBox>         FlushSpin;
	UPROPERTY(Transient) TObjectPtr<UCheckBox>        KeepChunksCheck;
	UPROPERTY(Transient) TObjectPtr<UCheckBox>        VerboseCheck;
	UPROPERTY(Transient) TObjectPtr<UCheckBox>        CompressCheck;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>       StatusText;
	UPROPERTY(Transient) TObjectPtr<UButton>          ConvertBtn;
	UPROPERTY(Transient) TObjectPtr<UButton>          CancelBtn;
	UPROPERTY(Transient) TObjectPtr<UButton>          SaveBtn;
	UPROPERTY(Transient) TObjectPtr<UButton>          ResetBtn;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>       CacheSizeText;
	UPROPERTY(Transient) TObjectPtr<UButton>          ClearThisBtn;
	UPROPERTY(Transient) TObjectPtr<UButton>          ClearAllBtn;

	float CacheLabelTimer = 0.f;
};
