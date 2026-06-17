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

	TObjectPtr<UEditableTextBox> PathBox;
	TObjectPtr<UButton>          BrowseBtn;
	TObjectPtr<USpinBox>         SpacingSpin;
	TObjectPtr<USpinBox>         LeafSpin;
	TObjectPtr<USpinBox>         MaxDepthSpin;
	TObjectPtr<USpinBox>         ChunkSpin;
	TObjectPtr<USpinBox>         FlushSpin;
	TObjectPtr<UCheckBox>        KeepChunksCheck;
	TObjectPtr<UCheckBox>        VerboseCheck;
	TObjectPtr<UCheckBox>        CompressCheck;
	TObjectPtr<UTextBlock>       StatusText;
	TObjectPtr<UButton>          ConvertBtn;
	TObjectPtr<UButton>          CancelBtn;
	TObjectPtr<UButton>          SaveBtn;
	TObjectPtr<UButton>          ResetBtn;
	TObjectPtr<UTextBlock>       CacheSizeText;
	TObjectPtr<UButton>          ClearThisBtn;
	TObjectPtr<UButton>          ClearAllBtn;

	float CacheLabelTimer = 0.f;
};
