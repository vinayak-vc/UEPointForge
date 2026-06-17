#include "PFViewerPanel.h"

#include "PFPointCloudComponent.h"
#include "PFPointCloudActor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/SlateWrapperTypes.h"
#include "Materials/MaterialInstanceDynamic.h"

UPFViewerPanel::UPFViewerPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPFViewerPanel::SetTarget(UPFPointCloudComponent* InTarget)
{
	Target = InTarget;
}

void UPFViewerPanel::SetOwnerActor(APFPointCloudActor* InActor)
{
	OwnerActor = InActor;
}

TSharedRef<SWidget> UPFViewerPanel::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PFRoot"));
		Root->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f));
		Root->SetPadding(FMargin(10.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PFVBox"));
		Root->SetContent(VBox);

		const FSlateColor White(FLinearColor::White);

		auto AddLine = [&](const FString& Text) -> UTextBlock*
		{
			UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
			T->SetText(FText::FromString(Text));
			T->SetColorAndOpacity(White);
			VBox->AddChildToVerticalBox(T);
			return T;
		};

		UTextBlock* Title = AddLine(TEXT("PointForge"));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.8f, 1.0f)));

		StatsText = AddLine(TEXT(""));

		auto AddSlider = [&](const FString& Name, float Mn, float Mx, float Val,
			TObjectPtr<USlider>& OutSlider, TObjectPtr<UTextBlock>& OutLabel)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			OutLabel = WidgetTree->ConstructWidget<UTextBlock>();
			OutLabel->SetColorAndOpacity(White);
			OutLabel->SetText(FText::FromString(Name));
			OutLabel->SetMinDesiredWidth(150.f);
			if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(OutLabel))
			{
				LS->SetVerticalAlignment(VAlign_Center);
				LS->SetPadding(FMargin(0, 0, 8, 0));
			}
			OutSlider = WidgetTree->ConstructWidget<USlider>();
			OutSlider->SetMinValue(Mn);
			OutSlider->SetMaxValue(Mx);
			OutSlider->SetValue(Val);
			if (UHorizontalBoxSlot* SS = Row->AddChildToHorizontalBox(OutSlider))
			{
				SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				SS->SetVerticalAlignment(VAlign_Center);
			}
			VBox->AddChildToVerticalBox(Row);
		};

			AddSlider(TEXT("Point size"), 1.f, 16.f, 2.4f, PointSizeSlider, PointSizeLabel);
			AddSlider(TEXT("LOD budget (px)"), 0.3f, 8.f, 7.5f, SseSlider, SseLabel);
			AddSlider(TEXT("GPU budget (MB)"), 128.f, 8192.f, 4096.f, GpuSlider, GpuLabel);
			AddSlider(TEXT("Uploads/frame"), 1.f, 256.f, 128.f, UploadsSlider, UploadsLabel);
			AddSlider(TEXT("Point Limit (M)"), 0.0f, 500.f, 0.0f, PointCountLimitSlider, PointCountLimitLabel);

		// AddDynamic is a macro that stringifies the function name, so it must be
		// called with a LITERAL &UPFViewerPanel::Fn — not a function-pointer variable.
		PointSizeSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnPointSizeChanged);
		SseSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnSseChanged);
		GpuSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnGpuBudgetChanged);
		UploadsSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnUploadsChanged);
		PointCountLimitSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnPointCountLimitChanged);

		auto AddCheck = [&](const FString& Name, bool bVal, TObjectPtr<UCheckBox>& OutCheck)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			OutCheck = WidgetTree->ConstructWidget<UCheckBox>();
			OutCheck->SetIsChecked(bVal);
			Row->AddChildToHorizontalBox(OutCheck);
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(Name));
			L->SetColorAndOpacity(White);
			if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(L))
			{
				LS->SetVerticalAlignment(VAlign_Center);
				LS->SetPadding(FMargin(6, 0, 0, 0));
			}
			VBox->AddChildToVerticalBox(Row);
		};

		AddCheck(TEXT("Round points"), true, RoundCheck);
		AddCheck(TEXT("Attenuate"), true, AttenCheck);
		RoundCheck->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnRoundChanged);
		AttenCheck->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnAttenuateChanged);

		// --- Extended runtime controls (mirror UPFPointCloudComponent UPROPERTYs) ---
		UTextBlock* Section = AddLine(TEXT("— Render & Color —"));
		Section->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)));

		AddSlider(TEXT("Soft round"),       0.f,  0.5f,   0.0f, SoftRoundSlider, SoftRoundLabel);
		AddSlider(TEXT("Emissive boost"),   0.f,  2.0f,   0.4f, EmissiveSlider,  EmissiveLabel);
		AddSlider(TEXT("Unit scale"),       1.f,  1000.f, 100.f, UnitScaleSlider, UnitScaleLabel);
		AddSlider(TEXT("Elev min Z"),       -1000000.f, 1000000.f, 0.f,    ElevMinSlider, ElevMinLabel);
		AddSlider(TEXT("Elev max Z"),       -1000000.f, 1000000.f, 1000.f, ElevMaxSlider, ElevMaxLabel);

		SoftRoundSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnSoftRoundChanged);
		EmissiveSlider ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnEmissiveBoostChanged);
		UnitScaleSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnUnitScaleChanged);
		ElevMinSlider  ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnElevationMinChanged);
		ElevMaxSlider  ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnElevationMaxChanged);

		// Fit elevation button — one click copies CloudZMin/Max → sliders.
		{
			FitElevationBtn = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* FitLbl = WidgetTree->ConstructWidget<UTextBlock>();
			FitLbl->SetText(FText::FromString(TEXT("Fit Elevation to Cloud")));
			FitLbl->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			FitElevationBtn->SetContent(FitLbl);
			VBox->AddChildToVerticalBox(FitElevationBtn);
			FitElevationBtn->OnClicked.AddDynamic(this, &UPFViewerPanel::OnFitElevationClicked);
		}

		// ColorMode dropdown.
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(TEXT("Color mode")));
			L->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			L->SetMinDesiredWidth(150.f);
			if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(L))
			{
				LS->SetVerticalAlignment(VAlign_Center);
				LS->SetPadding(FMargin(0, 0, 8, 0));
			}
			ColorModeCombo = WidgetTree->ConstructWidget<UComboBoxString>();
			ColorModeCombo->AddOption(TEXT("RGB"));
			ColorModeCombo->AddOption(TEXT("Intensity"));
			ColorModeCombo->AddOption(TEXT("Elevation"));
			ColorModeCombo->AddOption(TEXT("Classification"));
			ColorModeCombo->SetSelectedIndex(0);
			ColorModeCombo->OnSelectionChanged.AddDynamic(this, &UPFViewerPanel::OnColorModeSelected);
			if (UHorizontalBoxSlot* SS = Row->AddChildToHorizontalBox(ColorModeCombo))
			{
				SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				SS->SetVerticalAlignment(VAlign_Center);
			}
			VBox->AddChildToVerticalBox(Row);
		}

		AddCheck(TEXT("Use clipping plane"),       false, UseClipCheck);
		AddCheck(TEXT("Color is 16-bit (reload)"), true,  Color16BitCheck);
		UseClipCheck   ->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnUseClipChanged);
		Color16BitCheck->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnColor16BitChanged);

		// --- EDL ---
		UTextBlock* EDLSection = AddLine(TEXT("— Eye-Dome Lighting —"));
		EDLSection->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.4f)));

		AddSlider(TEXT("EDL strength"),  0.f, 5.f,  0.5f, EDLStrengthSlider, EDLStrengthLabel);
		AddSlider(TEXT("EDL radius px"), 0.1f, 10.f, 1.0f, EDLRadiusSlider,  EDLRadiusLabel);
		AddCheck(TEXT("EDL enabled"), true, EDLEnableCheck);

		EDLStrengthSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnEDLStrengthChanged);
		EDLRadiusSlider  ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnEDLRadiusChanged);
		EDLEnableCheck   ->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnEDLEnableChanged);

		UTextBlock* Help = AddLine(TEXT("RMB drag: look   WASD: move   Q/E: down/up   Shift: fast"));
		Help->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)));
	}

	return Super::RebuildWidget();
}

void UPFViewerPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (PointSizeLabel && PointSizeSlider)
	{
		PointSizeLabel->SetText(FText::FromString(FString::Printf(TEXT("Point size: %.1f"), PointSizeSlider->GetValue())));
	}
	if (SseLabel && SseSlider)
	{
		SseLabel->SetText(FText::FromString(FString::Printf(TEXT("LOD budget: %.2f px"), SseSlider->GetValue())));
	}
	if (GpuLabel && GpuSlider)
	{
		GpuLabel->SetText(FText::FromString(FString::Printf(TEXT("GPU budget: %d MB"), FMath::RoundToInt(GpuSlider->GetValue()))));
	}
	if (UploadsLabel && UploadsSlider)
	{
		UploadsLabel->SetText(FText::FromString(FString::Printf(TEXT("Uploads/frame: %d"), FMath::RoundToInt(UploadsSlider->GetValue()))));
	}
	if (PointCountLimitLabel && PointCountLimitSlider)
	{
		float Val = PointCountLimitSlider->GetValue();
		if (Val > 0.0f)
			PointCountLimitLabel->SetText(FText::FromString(FString::Printf(TEXT("Point Limit: %.1f M"), Val)));
		else
			PointCountLimitLabel->SetText(FText::FromString(TEXT("Point Limit: off")));
	}
	if (SoftRoundLabel && SoftRoundSlider)
	{
		SoftRoundLabel->SetText(FText::FromString(FString::Printf(TEXT("Soft round: %.2f"), SoftRoundSlider->GetValue())));
	}
	if (EmissiveLabel && EmissiveSlider)
	{
		EmissiveLabel->SetText(FText::FromString(FString::Printf(TEXT("Emissive boost: %.2f"), EmissiveSlider->GetValue())));
	}
	if (UnitScaleLabel && UnitScaleSlider)
	{
		UnitScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("Unit scale: %d"), FMath::RoundToInt(UnitScaleSlider->GetValue()))));
	}
	if (ElevMinLabel && ElevMinSlider)
	{
		ElevMinLabel->SetText(FText::FromString(FString::Printf(TEXT("Elev min Z: %.0f"), ElevMinSlider->GetValue())));
	}
	if (ElevMaxLabel && ElevMaxSlider)
	{
		ElevMaxLabel->SetText(FText::FromString(FString::Printf(TEXT("Elev max Z: %.0f"), ElevMaxSlider->GetValue())));
	}
	if (EDLStrengthLabel && EDLStrengthSlider)
	{
		EDLStrengthLabel->SetText(FText::FromString(FString::Printf(TEXT("EDL strength: %.2f"), EDLStrengthSlider->GetValue())));
	}
	if (EDLRadiusLabel && EDLRadiusSlider)
	{
		EDLRadiusLabel->SetText(FText::FromString(FString::Printf(TEXT("EDL radius: %.1f px"), EDLRadiusSlider->GetValue())));
	}

	if (!StatsText || !Target.IsValid())
	{
		return;
	}

	const FPFViewerStatsBP S = Target->GetStats();
	const FString Txt = FString::Printf(
		TEXT("Cloud: %lld pts, %d nodes\n")
		TEXT("Z range: %.1f .. %.1f cm\n")
		TEXT("Visible nodes:  %d\n")
		TEXT("Drawn nodes:    %d\n")
		TEXT("Points on GPU:  %lld\n")
		TEXT("Drawn points:   %lld\n")
		TEXT("GPU resident:   %.1f MB\n")
		TEXT("Load queue:     %d\n")
		TEXT("FPS:            %.1f"),
		S.CloudPoints, S.CloudNodes, S.CloudZMin, S.CloudZMax,
		S.VisibleNodes, S.DrawnNodes,
		S.PointsOnGpu, S.DrawnPoints, S.ResidentMB, S.LoadQueue, S.FPS);
	StatsText->SetText(FText::FromString(Txt));
}

void UPFViewerPanel::OnPointSizeChanged(float Value)
{
	if (Target.IsValid()) { Target->PointSize = Value; }
}

void UPFViewerPanel::OnSseChanged(float Value)
{
	if (Target.IsValid()) { Target->SseBudgetPixels = Value; }
}

void UPFViewerPanel::OnGpuBudgetChanged(float Value)
{
	if (Target.IsValid()) { Target->GpuBudgetMB = FMath::RoundToInt(Value); }
}

void UPFViewerPanel::OnUploadsChanged(float Value)
{
	if (Target.IsValid()) { Target->UploadsPerFrame = FMath::RoundToInt(Value); }
}

void UPFViewerPanel::OnPointCountLimitChanged(float Value)
{
	if (Target.IsValid()) { Target->PointCountLimit = Value; }
}

void UPFViewerPanel::OnRoundChanged(bool bChecked)
{
	if (Target.IsValid()) { Target->bRoundPoints = bChecked; }
}

void UPFViewerPanel::OnAttenuateChanged(bool bChecked)
{
	if (Target.IsValid()) { Target->bAttenuate = bChecked; }
}

void UPFViewerPanel::OnSoftRoundChanged(float Value)
{
	if (Target.IsValid()) { Target->SoftRoundFalloff = Value; }
}

void UPFViewerPanel::OnEmissiveBoostChanged(float Value)
{
	// Component doesn't have EmissiveBoost as a UPROPERTY yet — push straight to the MID.
	if (!Target.IsValid()) { return; }
	if (UMaterialInstanceDynamic* MID = Target->PointMID)
	{
		MID->SetScalarParameterValue(TEXT("EmissiveBoost"), Value);
	}
}

void UPFViewerPanel::OnUnitScaleChanged(float Value)
{
	if (Target.IsValid()) { Target->UnitScale = Value; }
}

void UPFViewerPanel::OnElevationMinChanged(float Value)
{
	if (Target.IsValid()) { Target->ElevationMinZ = Value; }
}

void UPFViewerPanel::OnElevationMaxChanged(float Value)
{
	if (Target.IsValid()) { Target->ElevationMaxZ = Value; }
}

void UPFViewerPanel::OnFitElevationClicked()
{
	if (!Target.IsValid()) { return; }
	const FPFViewerStatsBP S = Target->GetStats();
	// Only apply if the cloud actually reported a range.
	if (S.CloudZMax <= S.CloudZMin) { return; }

	Target->ElevationMinZ = S.CloudZMin;
	Target->ElevationMaxZ = S.CloudZMax;
	// Also sync sliders so they visually reflect the new values.
	if (ElevMinSlider) { ElevMinSlider->SetValue(S.CloudZMin); }
	if (ElevMaxSlider) { ElevMaxSlider->SetValue(S.CloudZMax); }
}

void UPFViewerPanel::OnColorModeSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!Target.IsValid() || !ColorModeCombo) { return; }
	const int32 Idx = ColorModeCombo->GetSelectedIndex();
	if (Idx >= 0 && Idx <= static_cast<int32>(EPFColorMode::Classification))
	{
		Target->ColorMode = static_cast<EPFColorMode>(Idx);
	}
}

void UPFViewerPanel::OnUseClipChanged(bool bChecked)
{
	if (Target.IsValid()) { Target->bUseClippingPlane = bChecked; }
}

void UPFViewerPanel::OnColor16BitChanged(bool bChecked)
{
	// Note: takes effect on next OpenOctreeDir (colour is baked at decode time).
	if (Target.IsValid()) { Target->bColorIs16Bit = bChecked; }
}

void UPFViewerPanel::OnEDLStrengthChanged(float Value)
{
	if (OwnerActor.IsValid())
	{
		const float Radius = EDLRadiusSlider ? EDLRadiusSlider->GetValue() : OwnerActor->EDLRadius;
		OwnerActor->SetEDLParams(Value, Radius);
	}
}

void UPFViewerPanel::OnEDLRadiusChanged(float Value)
{
	if (OwnerActor.IsValid())
	{
		const float Strength = EDLStrengthSlider ? EDLStrengthSlider->GetValue() : OwnerActor->EDLStrength;
		OwnerActor->SetEDLParams(Strength, Value);
	}
}

void UPFViewerPanel::OnEDLEnableChanged(bool bChecked)
{
	if (OwnerActor.IsValid() && OwnerActor->PostProcess)
	{
		OwnerActor->PostProcess->bEnabled = bChecked;
		OwnerActor->bEnableEDL = bChecked;
	}
}
