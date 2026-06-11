#include "PFViewerPanel.h"

#include "PFPointCloudComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/SlateWrapperTypes.h"

UPFViewerPanel::UPFViewerPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPFViewerPanel::SetTarget(UPFPointCloudComponent* InTarget)
{
	Target = InTarget;
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

		AddSlider(TEXT("Point size"), 1.f, 16.f, 2.f, PointSizeSlider, PointSizeLabel);
		AddSlider(TEXT("LOD budget (px)"), 0.3f, 8.f, 1.5f, SseSlider, SseLabel);
		AddSlider(TEXT("GPU budget (MB)"), 128.f, 8192.f, 1024.f, GpuSlider, GpuLabel);
		AddSlider(TEXT("Uploads/frame"), 1.f, 256.f, 32.f, UploadsSlider, UploadsLabel);

		// AddDynamic is a macro that stringifies the function name, so it must be
		// called with a LITERAL &UPFViewerPanel::Fn — not a function-pointer variable.
		PointSizeSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnPointSizeChanged);
		SseSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnSseChanged);
		GpuSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnGpuBudgetChanged);
		UploadsSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnUploadsChanged);

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
		AddCheck(TEXT("Attenuate"), false, AttenCheck);
		RoundCheck->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnRoundChanged);
		AttenCheck->OnCheckStateChanged.AddDynamic(this, &UPFViewerPanel::OnAttenuateChanged);

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

	if (!StatsText || !Target.IsValid())
	{
		return;
	}

	const FPFViewerStatsBP S = Target->GetStats();
	const FString Txt = FString::Printf(
		TEXT("Cloud: %lld pts, %d nodes\n")
		TEXT("Visible nodes:  %d\n")
		TEXT("Drawn nodes:    %d\n")
		TEXT("Points on GPU:  %lld\n")
		TEXT("Drawn points:   %lld\n")
		TEXT("GPU resident:   %.1f MB\n")
		TEXT("Load queue:     %d\n")
		TEXT("FPS:            %.1f"),
		S.CloudPoints, S.CloudNodes, S.VisibleNodes, S.DrawnNodes,
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

void UPFViewerPanel::OnRoundChanged(bool bChecked)
{
	if (Target.IsValid()) { Target->bRoundPoints = bChecked; }
}

void UPFViewerPanel::OnAttenuateChanged(bool bChecked)
{
	if (Target.IsValid()) { Target->bAttenuate = bChecked; }
}
