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
#include "Components/SlateWrapperTypes.h"

UPFViewerPanel::UPFViewerPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPFViewerPanel::SetTarget(UPFPointCloudComponent* InTarget)
{
	Target = InTarget;
	if (InTarget)
	{
		InTarget->bRoundPoints     = true;
		InTarget->bAttenuate       = true;
		InTarget->SoftRoundFalloff = 0.5f;
		InTarget->UnitScale        = 100.f;
		InTarget->ColorMode        = EPFColorMode::RGB;
		InTarget->bColorIs16Bit    = true;
	}
}

void UPFViewerPanel::SetOwnerActor(APFPointCloudActor* /*InActor*/)
{
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

		AddSlider(TEXT("Point size"),      1.f,   16.f,   2.4f,   PointSizeSlider,       PointSizeLabel);
		AddSlider(TEXT("LOD budget (px)"), 0.3f,  8.f,    7.5f,   SseSlider,             SseLabel);
		AddSlider(TEXT("GPU budget (MB)"), 128.f, 8192.f, 4096.f, GpuSlider,             GpuLabel);
		AddSlider(TEXT("Uploads/frame"),   1.f,   256.f,  128.f,  UploadsSlider,         UploadsLabel);
		AddSlider(TEXT("Point Limit (M)"), 0.0f,  500.f,  0.0f,   PointCountLimitSlider, PointCountLimitLabel);

		PointSizeSlider      ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnPointSizeChanged);
		SseSlider            ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnSseChanged);
		GpuSlider            ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnGpuBudgetChanged);
		UploadsSlider        ->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnUploadsChanged);
		PointCountLimitSlider->OnValueChanged.AddDynamic(this, &UPFViewerPanel::OnPointCountLimitChanged);

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
		const float Val = PointCountLimitSlider->GetValue();
		if (Val > 0.0f)
			PointCountLimitLabel->SetText(FText::FromString(FString::Printf(TEXT("Point Limit: %.1f M"), Val)));
		else
			PointCountLimitLabel->SetText(FText::FromString(TEXT("Point Limit: off")));
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
		S.CloudPoints, S.CloudNodes,
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
