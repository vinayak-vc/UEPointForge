#include "PFConvertPanel.h"

#include "PFConvertSettings.h"
#include "PFPointCloudActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Components/SlateWrapperTypes.h"

void UPFConvertPanel::SetTarget(APFPointCloudActor* InTarget)
{
	Target = InTarget;
}

void UPFConvertPanel::SetSourcePath(const FString& InPath)
{
	if (PathBox)
	{
		PathBox->SetText(FText::FromString(InPath));
	}
}

void UPFConvertPanel::NativeConstruct()
{
	Super::NativeConstruct();

	if (SpacingSpin)     { SpacingSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnSpacingChanged); }
	if (LeafSpin)        { LeafSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnLeafChanged); }
	if (MaxDepthSpin)    { MaxDepthSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnMaxDepthChanged); }
	if (ChunkSpin)       { ChunkSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnChunkDepthChanged); }
	if (FlushSpin)       { FlushSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnFlushChanged); }
	if (KeepChunksCheck) { KeepChunksCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnKeepChunksChanged); }
	if (VerboseCheck)    { VerboseCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnVerboseChanged); }
	if (ConvertBtn)      { ConvertBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnConvertClicked); }
	if (SaveBtn)         { SaveBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnSaveClicked); }
	if (ResetBtn)        { ResetBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnResetClicked); }

	RefreshFromSettings();
}

void UPFConvertPanel::RefreshFromSettings()
{
	const UPFConvertSettings* S = UPFConvertSettings::Get();
	if (SpacingSpin)  { SpacingSpin->SetValue(S->Spacing); }
	if (LeafSpin)     { LeafSpin->SetValue(static_cast<float>(S->LeafSize)); }
	if (MaxDepthSpin) { MaxDepthSpin->SetValue(static_cast<float>(S->MaxDepth)); }
	if (ChunkSpin)    { ChunkSpin->SetValue(static_cast<float>(S->ChunkDepth)); }
	if (FlushSpin)    { FlushSpin->SetValue(static_cast<float>(S->FlushBudget)); }
	if (KeepChunksCheck) { KeepChunksCheck->SetIsChecked(S->bKeepChunks); }
	if (VerboseCheck)    { VerboseCheck->SetIsChecked(S->bVerbose); }
}

void UPFConvertPanel::OnSpacingChanged(float V)   { UPFConvertSettings* S = UPFConvertSettings::Get(); S->Spacing = V; S->SaveSettings(); }
void UPFConvertPanel::OnLeafChanged(float V)      { UPFConvertSettings* S = UPFConvertSettings::Get(); S->LeafSize = FMath::RoundToInt(V); S->SaveSettings(); }
void UPFConvertPanel::OnMaxDepthChanged(float V)  { UPFConvertSettings* S = UPFConvertSettings::Get(); S->MaxDepth = FMath::RoundToInt(V); S->SaveSettings(); }
void UPFConvertPanel::OnChunkDepthChanged(float V){ UPFConvertSettings* S = UPFConvertSettings::Get(); S->ChunkDepth = FMath::RoundToInt(V); S->SaveSettings(); }
void UPFConvertPanel::OnFlushChanged(float V)     { UPFConvertSettings* S = UPFConvertSettings::Get(); S->FlushBudget = static_cast<int64>(V); S->SaveSettings(); }
void UPFConvertPanel::OnKeepChunksChanged(bool b) { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bKeepChunks = b; S->SaveSettings(); }
void UPFConvertPanel::OnVerboseChanged(bool b)    { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bVerbose = b; S->SaveSettings(); }

void UPFConvertPanel::OnSaveClicked()
{
	UPFConvertSettings::Get()->SaveSettings();
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Settings saved."))); }
}

void UPFConvertPanel::OnResetClicked()
{
	UPFConvertSettings::Get()->ResetToDefaults();
	RefreshFromSettings();
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Reset to defaults."))); }
}

void UPFConvertPanel::OnConvertClicked()
{
	const FString Path = PathBox ? PathBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Path.IsEmpty())
	{
		if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Enter a source file path."))); }
		return;
	}
	if (APFPointCloudActor* Actor = Target.Get())
	{
		Actor->LoadPointCloudFile(Path);
		if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Converting / loading… (watch the log)"))); }
	}
	else if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("No target actor set.")));
	}
}
