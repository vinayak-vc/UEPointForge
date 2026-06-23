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
class UProgressBar;
class UScrollBox;
class USizeBox;

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
	
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetStatusText(const FString& text);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void RefreshFromSettings();
	void RefreshConvertUi();

	/** Apply a named preset: writes settings, refreshes spinboxes. */
	void ApplyPreset(float Spacing, int32 Leaf, int32 Depth, int32 Chunk, int64 Flush);

	
	UFUNCTION() void OnBrowseClicked();
	UFUNCTION() void OnSpacingChanged(float V);
	UFUNCTION() void OnLeafChanged(float V);
	UFUNCTION() void OnMaxDepthChanged(float V);
	UFUNCTION() void OnChunkDepthChanged(float V);
	UFUNCTION() void OnFlushChanged(float V);
	UFUNCTION() void OnKeepChunksChanged(bool b);
	UFUNCTION() void OnVerboseChanged(bool b);
	UFUNCTION() void OnConvertClicked();
	UFUNCTION() void OnResetClicked();
	UFUNCTION() void OnSaveClicked();
	UFUNCTION() void OnCancelClicked();
	UFUNCTION() void OnClearThisClicked();
	UFUNCTION() void OnClearAllClicked();

	// Preset handlers
	UFUNCTION() void OnPresetHighQuality();
	UFUNCTION() void OnPresetMedium();
	UFUNCTION() void OnPresetLow();
	UFUNCTION() void OnPresetFast();
	UFUNCTION() void OnPresetSlow();

	TWeakObjectPtr<APFPointCloudActor> Target;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEditableTextBox> PathBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          BrowseBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpinBox>         SpacingSpin;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpinBox>         LeafSpin;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpinBox>         MaxDepthSpin;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpinBox>         ChunkSpin;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<USpinBox>         FlushSpin;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCheckBox>        KeepChunksCheck;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCheckBox>        VerboseCheck;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>       StatusText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          ConvertBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          CancelBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          SaveBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          ResetBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>       CacheSizeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          ClearThisBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          ClearAllBtn;

	// Verbose log + progress bar — created dynamically in NativeConstruct if not in WBP
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar>     ConvertProgressBar;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UScrollBox>       LogScroll;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>       VerboseLogText;
	UPROPERTY() TObjectPtr<USizeBox> LogSizeBox;

	int32 LastLogLineCount = 0;

	// Preset buttons
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          PresetHQBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          PresetMedBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          PresetLowBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          PresetFastBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>          PresetSlowBtn;

	float CacheLabelTimer = 0.f;
};
