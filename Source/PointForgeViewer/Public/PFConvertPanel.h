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
 * Conversion control panel (code-built UMG). Exposes all pfconvert parameters
 * (UPFConvertSettings) with an "Important" density section up top and Advanced
 * below. Edits auto-save; a Reset button restores pfconvert defaults; Convert
 * runs the target actor's LoadPointCloudFile with the entered path.
 */
UCLASS()
class POINTFORGEVIEWER_API UPFConvertPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetTarget(APFPointCloudActor* InTarget);

	/** Pre-fill the source-file path box. */
	UFUNCTION(BlueprintCallable, Category = "PointForge")
	void SetSourcePath(const FString& InPath);

protected:
	virtual void NativeConstruct() override;

	void RefreshFromSettings();

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

	TWeakObjectPtr<APFPointCloudActor> Target;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableTextBox> PathBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USpinBox> SpacingSpin;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USpinBox> LeafSpin;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USpinBox> MaxDepthSpin;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USpinBox> ChunkSpin;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<USpinBox> FlushSpin;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCheckBox> KeepChunksCheck;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UCheckBox> VerboseCheck;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ConvertBtn;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> SaveBtn;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> ResetBtn;
};
