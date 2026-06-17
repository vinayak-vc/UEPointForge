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

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#endif

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

TSharedRef<SWidget> UPFConvertPanel::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PFCRoot"));
		Root->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 0.90f));
		Root->SetPadding(FMargin(10.f));
		WidgetTree->RootWidget = Root;

		UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PFCVBox"));
		Root->SetContent(VBox);

		const FSlateColor White(FLinearColor::White);
		const FSlateColor Grey(FLinearColor(0.7f, 0.7f, 0.7f));

		// Title
		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
		Title->SetText(FText::FromString(TEXT("PointForge Convert")));
		Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.8f, 1.0f)));
		VBox->AddChildToVerticalBox(Title);

		// --- Source file path row ---
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			PathBox = WidgetTree->ConstructWidget<UEditableTextBox>();
			PathBox->SetHintText(FText::FromString(TEXT("e.g. E:/Model/scan.laz")));
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(PathBox))
			{
				S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(FMargin(0, 0, 4, 0));
			}

			BrowseBtn = WidgetTree->ConstructWidget<UButton>();
			UTextBlock* BrowseLbl = WidgetTree->ConstructWidget<UTextBlock>();
			BrowseLbl->SetText(FText::FromString(TEXT("Browse...")));
			BrowseLbl->SetColorAndOpacity(White);
			BrowseBtn->SetContent(BrowseLbl);
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(BrowseBtn))
			{
				S->SetVerticalAlignment(VAlign_Center);
			}
			VBox->AddChildToVerticalBox(Row);
		}

		// --- Spin-box helper ---
		auto AddSpin = [&](const FString& Label, float Mn, float Mx, float Delta, int32 Frac, float InitVal,
			TObjectPtr<USpinBox>& OutSpin)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(Label));
			L->SetColorAndOpacity(Grey);
			L->SetMinDesiredWidth(140.f);
			if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(L))
			{
				LS->SetVerticalAlignment(VAlign_Center);
				LS->SetPadding(FMargin(0, 0, 6, 0));
			}
			OutSpin = WidgetTree->ConstructWidget<USpinBox>();
			OutSpin->SetMinValue(Mn); OutSpin->SetMaxValue(Mx);
			OutSpin->SetMinSliderValue(Mn); OutSpin->SetMaxSliderValue(Mx);
			OutSpin->SetDelta(Delta);
			OutSpin->SetMinFractionalDigits(Frac); OutSpin->SetMaxFractionalDigits(Frac);
			OutSpin->SetValue(InitVal);
			if (UHorizontalBoxSlot* SS = Row->AddChildToHorizontalBox(OutSpin))
			{
				SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				SS->SetVerticalAlignment(VAlign_Center);
			}
			VBox->AddChildToVerticalBox(Row);
		};

		const UPFConvertSettings* S = UPFConvertSettings::Get();
		AddSpin(TEXT("Spacing (0=auto)"),  0.0f,          100.0f,          0.001f,   4, S->Spacing,                          SpacingSpin);
		AddSpin(TEXT("Leaf size"),         1000.0f,  2'000'000.0f,       1000.0f,    0, static_cast<float>(S->LeafSize),     LeafSpin);
		AddSpin(TEXT("Max depth"),         1.0f,           30.0f,            1.0f,    0, static_cast<float>(S->MaxDepth),     MaxDepthSpin);
		AddSpin(TEXT("Chunk depth"),       0.0f,           10.0f,            1.0f,    0, static_cast<float>(S->ChunkDepth),   ChunkSpin);
		AddSpin(TEXT("Flush budget (pts)"),1'000'000.0f,   268'435'456.0f, 1'000'000.0f, 0, static_cast<float>(S->FlushBudget), FlushSpin);

		// --- Checkbox helper ---
		auto AddCheck = [&](const FString& Label, bool bVal, TObjectPtr<UCheckBox>& OutCheck)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			OutCheck = WidgetTree->ConstructWidget<UCheckBox>();
			OutCheck->SetIsChecked(bVal);
			Row->AddChildToHorizontalBox(OutCheck);
			UTextBlock* L = WidgetTree->ConstructWidget<UTextBlock>();
			L->SetText(FText::FromString(Label));
			L->SetColorAndOpacity(Grey);
			if (UHorizontalBoxSlot* LS = Row->AddChildToHorizontalBox(L))
			{
				LS->SetVerticalAlignment(VAlign_Center);
				LS->SetPadding(FMargin(6, 0, 0, 0));
			}
			VBox->AddChildToVerticalBox(Row);
		};

		AddCheck(TEXT("Keep chunks (debug)"), S->bKeepChunks, KeepChunksCheck);
		AddCheck(TEXT("Verbose log"),         S->bVerbose,    VerboseCheck);
		AddCheck(TEXT("Compress (zstd)"),     S->bCompress,   CompressCheck);

		// --- Action buttons row ---
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

			auto MakeBtn = [&](const FString& Lbl, TObjectPtr<UButton>& OutBtn)
			{
				OutBtn = WidgetTree->ConstructWidget<UButton>();
				UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
				T->SetText(FText::FromString(Lbl));
				T->SetColorAndOpacity(White);
				OutBtn->SetContent(T);
				if (UHorizontalBoxSlot* BS = Row->AddChildToHorizontalBox(OutBtn))
				{
					BS->SetPadding(FMargin(0, 0, 4, 0));
				}
			};

			MakeBtn(TEXT("Convert"), ConvertBtn);
			MakeBtn(TEXT("Cancel"),  CancelBtn);
			MakeBtn(TEXT("Save"),    SaveBtn);
			MakeBtn(TEXT("Reset"),   ResetBtn);
			VBox->AddChildToVerticalBox(Row);
		}

		// --- Cache management ---
		{
			UTextBlock* CacheHdr = WidgetTree->ConstructWidget<UTextBlock>();
			CacheHdr->SetText(FText::FromString(TEXT("— Cache —")));
			CacheHdr->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.9f, 0.6f)));
			VBox->AddChildToVerticalBox(CacheHdr);

			CacheSizeText = WidgetTree->ConstructWidget<UTextBlock>();
			CacheSizeText->SetText(FText::FromString(TEXT("Cache: — MB")));
			CacheSizeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)));
			VBox->AddChildToVerticalBox(CacheSizeText);

			UHorizontalBox* CacheRow = WidgetTree->ConstructWidget<UHorizontalBox>();

			auto MakeCacheBtn = [&](const FString& Lbl, TObjectPtr<UButton>& OutBtn)
			{
				OutBtn = WidgetTree->ConstructWidget<UButton>();
				UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>();
				T->SetText(FText::FromString(Lbl));
				T->SetColorAndOpacity(FSlateColor(FLinearColor::White));
				OutBtn->SetContent(T);
				if (UHorizontalBoxSlot* BS = CacheRow->AddChildToHorizontalBox(OutBtn))
				{
					BS->SetPadding(FMargin(0, 0, 4, 0));
				}
			};

			MakeCacheBtn(TEXT("Clear this file"), ClearThisBtn);
			MakeCacheBtn(TEXT("Clear all"),       ClearAllBtn);
			VBox->AddChildToVerticalBox(CacheRow);

			ClearThisBtn->OnClicked.AddDynamic(this, &UPFConvertPanel::OnClearThisClicked);
			ClearAllBtn ->OnClicked.AddDynamic(this, &UPFConvertPanel::OnClearAllClicked);
		}

		// --- Status text ---
		StatusText = WidgetTree->ConstructWidget<UTextBlock>();
		StatusText->SetText(FText::FromString(TEXT("")));
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.3f)));
		VBox->AddChildToVerticalBox(StatusText);

		// Wire delegates
		BrowseBtn->OnClicked.AddDynamic(this, &UPFConvertPanel::OnBrowseClicked);
		SpacingSpin->OnValueChanged.AddDynamic(this, &UPFConvertPanel::OnSpacingChanged);
		LeafSpin->OnValueChanged.AddDynamic(this, &UPFConvertPanel::OnLeafChanged);
		MaxDepthSpin->OnValueChanged.AddDynamic(this, &UPFConvertPanel::OnMaxDepthChanged);
		ChunkSpin->OnValueChanged.AddDynamic(this, &UPFConvertPanel::OnChunkDepthChanged);
		FlushSpin->OnValueChanged.AddDynamic(this, &UPFConvertPanel::OnFlushChanged);
		KeepChunksCheck->OnCheckStateChanged.AddDynamic(this, &UPFConvertPanel::OnKeepChunksChanged);
		VerboseCheck->OnCheckStateChanged.AddDynamic(this, &UPFConvertPanel::OnVerboseChanged);
		CompressCheck->OnCheckStateChanged.AddDynamic(this, &UPFConvertPanel::OnCompressChanged);
		ConvertBtn->OnClicked.AddDynamic(this, &UPFConvertPanel::OnConvertClicked);
		CancelBtn->OnClicked.AddDynamic(this, &UPFConvertPanel::OnCancelClicked);
		CancelBtn->SetVisibility(ESlateVisibility::Collapsed);
		SaveBtn->OnClicked.AddDynamic(this, &UPFConvertPanel::OnSaveClicked);
		ResetBtn->OnClicked.AddDynamic(this, &UPFConvertPanel::OnResetClicked);
	}

	return Super::RebuildWidget();
}

void UPFConvertPanel::NativeConstruct()
{
	Super::NativeConstruct();
	// NativeConstruct fires after RebuildWidget; all widgets exist by now.
	// BindWidget panels used to wire delegates here; RebuildWidget already wired them.
	RefreshFromSettings();
	RefreshConvertUi();
}

void UPFConvertPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshConvertUi();

	// Refresh cache size label every ~2 seconds (avoid per-frame disk stat).
	CacheLabelTimer += InDeltaTime;
	if (CacheLabelTimer >= 2.0f)
	{
		CacheLabelTimer = 0.f;
		if (CacheSizeText)
		{
			const FString Path = PathBox ? PathBox->GetText().ToString().TrimStartAndEnd() : FString();
			if (!Path.IsEmpty())
			{
				if (APFPointCloudActor* A = Target.Get())
				{
					const float MB = A->GetCacheSizeMB(Path);
					CacheSizeText->SetText(FText::FromString(FString::Printf(TEXT("Cache: %.1f MB"), MB)));
				}
			}
			else
			{
				CacheSizeText->SetText(FText::FromString(TEXT("Cache: (no path)")));
			}
		}
	}
}

void UPFConvertPanel::RefreshConvertUi()
{
	APFPointCloudActor* Actor = Target.Get();
	if (!Actor)
	{
		return;
	}

	const EPFConvertState St = Actor->GetConvertState();
	const FString Status = Actor->GetConvertStatus();
	const bool bRunning = (St == EPFConvertState::Running);

	// Status text — show phase while running; result line afterwards.
	if (StatusText)
	{
		FString Line;
		switch (St)
		{
		case EPFConvertState::Running:   Line = FString::Printf(TEXT("Converting — %s"), Status.IsEmpty() ? TEXT("starting...") : *Status); break;
		case EPFConvertState::Done:      Line = TEXT("Done."); break;
		case EPFConvertState::Failed:    Line = FString::Printf(TEXT("Failed: %s"), *Status); break;
		case EPFConvertState::Cancelled: Line = TEXT("Cancelled."); break;
		case EPFConvertState::Idle:      default: /* leave whatever was last set */ return;
		}
		StatusText->SetText(FText::FromString(Line));
	}

	// Cancel button visibility — running only.
	if (CancelBtn)
	{
		CancelBtn->SetVisibility(bRunning ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// Convert button disabled while one is in flight (avoids double-launching).
	if (ConvertBtn)
	{
		ConvertBtn->SetIsEnabled(!bRunning);
	}
}

void UPFConvertPanel::RefreshFromSettings()
{
	const UPFConvertSettings* S = UPFConvertSettings::Get();

	// Configure spin-box ranges so SetValue() below doesn't clamp to whatever
	// the WBP designer left. These match the ClampMin/ClampMax meta tags in
	// UPFConvertSettings (the source of truth for valid input).
	auto cfg = [](USpinBox* Sb, float Mn, float Mx, float Delta, int32 FracDigits)
	{
		if (!Sb) return;
		Sb->SetMinValue(Mn);
		Sb->SetMaxValue(Mx);
		Sb->SetMinSliderValue(Mn);
		Sb->SetMaxSliderValue(Mx);
		Sb->SetDelta(Delta);
		Sb->SetMinFractionalDigits(FracDigits);
		Sb->SetMaxFractionalDigits(FracDigits);
	};
	cfg(SpacingSpin,  0.0f,     100.0f,       0.001f, 4); // metres, 0 = auto
	cfg(LeafSpin,     1000.0f,  2'000'000.0f, 1000.0f, 0);
	cfg(MaxDepthSpin, 1.0f,     30.0f,        1.0f,    0);
	cfg(ChunkSpin,    0.0f,     10.0f,        1.0f,    0);
	cfg(FlushSpin,    1'000'000.0f, 268'435'456.0f, 1'000'000.0f, 0);

	if (SpacingSpin)  { SpacingSpin->SetValue(S->Spacing); }
	if (LeafSpin)     { LeafSpin->SetValue(static_cast<float>(S->LeafSize)); }
	if (MaxDepthSpin) { MaxDepthSpin->SetValue(static_cast<float>(S->MaxDepth)); }
	if (ChunkSpin)    { ChunkSpin->SetValue(static_cast<float>(S->ChunkDepth)); }
	if (FlushSpin)    { FlushSpin->SetValue(static_cast<float>(S->FlushBudget)); }
	if (KeepChunksCheck) { KeepChunksCheck->SetIsChecked(S->bKeepChunks); }
	if (VerboseCheck)    { VerboseCheck->SetIsChecked(S->bVerbose); }

	// Pre-fill source path with last used file (if any) + hint for empty case.
	if (PathBox)
	{
		if (PathBox->GetText().IsEmpty() && !S->LastSourceFile.IsEmpty())
		{
			PathBox->SetText(FText::FromString(S->LastSourceFile));
		}
		PathBox->SetHintText(FText::FromString(TEXT("e.g. E:/Model/scan.laz")));
	}
}

void UPFConvertPanel::OnSpacingChanged(float V)   { UPFConvertSettings* S = UPFConvertSettings::Get(); S->Spacing = V; S->SaveSettings(); }
void UPFConvertPanel::OnLeafChanged(float V)      { UPFConvertSettings* S = UPFConvertSettings::Get(); S->LeafSize = FMath::RoundToInt(V); S->SaveSettings(); }
void UPFConvertPanel::OnMaxDepthChanged(float V)  { UPFConvertSettings* S = UPFConvertSettings::Get(); S->MaxDepth = FMath::RoundToInt(V); S->SaveSettings(); }
void UPFConvertPanel::OnChunkDepthChanged(float V){ UPFConvertSettings* S = UPFConvertSettings::Get(); S->ChunkDepth = FMath::RoundToInt(V); S->SaveSettings(); }
void UPFConvertPanel::OnFlushChanged(float V)     { UPFConvertSettings* S = UPFConvertSettings::Get(); S->FlushBudget = static_cast<int64>(V); S->SaveSettings(); }
void UPFConvertPanel::OnKeepChunksChanged(bool b) { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bKeepChunks = b; S->SaveSettings(); }
void UPFConvertPanel::OnVerboseChanged(bool b)    { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bVerbose = b; S->SaveSettings(); }
void UPFConvertPanel::OnCompressChanged(bool b)   { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bCompress = b; S->SaveSettings(); }

void UPFConvertPanel::OnBrowseClicked()
{
#if WITH_EDITOR
	IDesktopPlatform* DP = FDesktopPlatformModule::Get();
	if (!DP) { return; }

	TArray<FString> OutFiles;
	const FString DefaultPath = TEXT("");
	const bool bOpened = DP->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Point Cloud File"),
		DefaultPath,
		TEXT(""),
		TEXT("Point Cloud Files (*.laz;*.las;*.ply;*.e57;*.pts;*.xyz)|*.laz;*.las;*.ply;*.e57;*.pts;*.xyz|All Files (*.*)|*.*"),
		EFileDialogFlags::None,
		OutFiles);

	if (bOpened && OutFiles.Num() > 0)
	{
		const FString& Picked = OutFiles[0];
		if (PathBox)
		{
			PathBox->SetText(FText::FromString(Picked));
		}
		// Persist so next open pre-fills the path.
		UPFConvertSettings* S = UPFConvertSettings::Get();
		S->LastSourceFile = Picked;
		S->SaveSettings();
	}
#else
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("File browser only available in Editor builds.")));
	}
#endif
}

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
		// Remember the path so subsequent panel opens pre-fill it.
		UPFConvertSettings* S = UPFConvertSettings::Get();
		S->LastSourceFile = Path;
		S->SaveSettings();

		Actor->LoadPointCloudFile(Path);
		RefreshConvertUi();   // pick up Running state immediately
	}
	else if (StatusText)
	{
		StatusText->SetText(FText::FromString(TEXT("No target actor set.")));
	}
}

void UPFConvertPanel::OnCancelClicked()
{
	if (APFPointCloudActor* Actor = Target.Get())
	{
		Actor->CancelConvert();
	}
}

void UPFConvertPanel::OnClearThisClicked()
{
	const FString Path = PathBox ? PathBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Path.IsEmpty())
	{
		if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Enter a source path first."))); }
		return;
	}
	if (APFPointCloudActor* Actor = Target.Get())
	{
		const bool bOk = Actor->ClearCacheForFile(Path);
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(bOk ? TEXT("Cache cleared for this file.") : TEXT("No cache found for this file.")));
		}
		CacheLabelTimer = 2.f; // force refresh next tick
	}
}

void UPFConvertPanel::OnClearAllClicked()
{
	const FString Path = PathBox ? PathBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Path.IsEmpty())
	{
		if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Enter a source path first."))); }
		return;
	}
	if (APFPointCloudActor* Actor = Target.Get())
	{
		const int32 Cleared = Actor->ClearAllCachesForDir(Path);
		if (StatusText)
		{
			StatusText->SetText(FText::FromString(FString::Printf(TEXT("Cleared %d cache(s)."), Cleared)));
		}
		CacheLabelTimer = 2.f; // force refresh next tick
	}
}
