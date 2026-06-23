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
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"

#include "Framework/Application/SlateApplication.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <commdlg.h>
#include "Windows/HideWindowsPlatformTypes.h"
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

void UPFConvertPanel::NativeConstruct()
{
	Super::NativeConstruct();

	if (BrowseBtn) BrowseBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnBrowseClicked);
	if (SpacingSpin) SpacingSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnSpacingChanged);
	if (LeafSpin) LeafSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnLeafChanged);
	if (MaxDepthSpin) MaxDepthSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnMaxDepthChanged);
	if (ChunkSpin) ChunkSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnChunkDepthChanged);
	if (FlushSpin) FlushSpin->OnValueChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnFlushChanged);
	if (KeepChunksCheck) KeepChunksCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnKeepChunksChanged);
	if (VerboseCheck) VerboseCheck->OnCheckStateChanged.AddUniqueDynamic(this, &UPFConvertPanel::OnVerboseChanged);
	if (ConvertBtn) ConvertBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnConvertClicked);
	if (CancelBtn)
	{
		CancelBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnCancelClicked);
		CancelBtn->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (SaveBtn) SaveBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnSaveClicked);
	if (ResetBtn) ResetBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnResetClicked);
	if (PresetHQBtn) PresetHQBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnPresetHighQuality);
	if (PresetMedBtn) PresetMedBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnPresetMedium);
	if (PresetLowBtn) PresetLowBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnPresetLow);
	if (PresetFastBtn) PresetFastBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnPresetFast);
	if (PresetSlowBtn) PresetSlowBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnPresetSlow);
	if (ClearThisBtn) ClearThisBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnClearThisClicked);
	if (ClearAllBtn) ClearAllBtn->OnClicked.AddUniqueDynamic(this, &UPFConvertPanel::OnClearAllClicked);

	// Add text blocks to newly injected empty buttons dynamically
	auto AddTextToButton = [this](UButton* Btn, const FString& TextStr)
	{
		if (Btn && Btn->GetContent() == nullptr && WidgetTree)
		{
			UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), NAME_None);
			if (Txt)
			{
				Txt->SetText(FText::FromString(TextStr));
				Txt->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
				Btn->SetContent(Txt);
			}
		}
	};

	AddTextToButton(BrowseBtn, TEXT("Browse..."));
	AddTextToButton(PresetHQBtn, TEXT("High Qual"));
	AddTextToButton(PresetMedBtn, TEXT("Medium"));
	AddTextToButton(PresetLowBtn, TEXT("Low"));
	AddTextToButton(PresetFastBtn, TEXT("Fast"));
	AddTextToButton(PresetSlowBtn, TEXT("Slow"));
	AddTextToButton(ClearThisBtn, TEXT("Clear this file"));
	AddTextToButton(ClearAllBtn, TEXT("Clear all"));

	RefreshFromSettings();
	RefreshConvertUi();

	// Create progress bar + verbose log widgets if not already bound from the WBP designer.
	// They are injected into the same parent panel as StatusText.
	if (WidgetTree)
	{
		UPanelWidget* Host = StatusText ? Cast<UPanelWidget>(StatusText->GetParent()) : nullptr;
		if (!Host) Host = Cast<UPanelWidget>(WidgetTree->RootWidget);

		if (Host && !ConvertProgressBar)
		{
			ConvertProgressBar = WidgetTree->ConstructWidget<UProgressBar>(
				UProgressBar::StaticClass(), FName("ConvertProgressBar"));
			if (ConvertProgressBar)
			{
				ConvertProgressBar->SetPercent(0.f);
				ConvertProgressBar->SetVisibility(ESlateVisibility::Collapsed);
				Host->AddChild(ConvertProgressBar);
				if (UVerticalBoxSlot* VBSlot = Cast<UVerticalBoxSlot>(ConvertProgressBar->Slot))
					VBSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			}
		}

		if (Host && !LogScroll)
		{
			LogSizeBox     = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),       FName("LogSizeBox"));
			LogScroll      = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),   FName("LogScroll"));
			VerboseLogText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),   FName("VerboseLogText"));

			if (VerboseLogText)
			{
				VerboseLogText->SetAutoWrapText(true);
				FSlateFontInfo Font = VerboseLogText->GetFont();
				Font.Size = 8;
				VerboseLogText->SetFont(Font);
				VerboseLogText->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 1.0f, 0.55f)));
			}
			if (LogScroll && VerboseLogText)
				LogScroll->AddChild(VerboseLogText);

			if (LogSizeBox && LogScroll)
			{
				LogSizeBox->SetMaxDesiredHeight(180.f);
				LogSizeBox->AddChild(LogScroll);
				LogSizeBox->SetVisibility(ESlateVisibility::Collapsed);
				Host->AddChild(LogSizeBox);
				if (UVerticalBoxSlot* VBSlot = Cast<UVerticalBoxSlot>(LogSizeBox->Slot))
					VBSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			}
			else if (LogScroll)
			{
				LogScroll->SetVisibility(ESlateVisibility::Collapsed);
				Host->AddChild(LogScroll);
			}
		}
	}
}

void UPFConvertPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshConvertUi();

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
	if (!Actor) return;

	const EPFConvertState St      = Actor->GetConvertState();
	const FString         Status  = Actor->GetConvertStatus();
	const bool            bRunning = (St == EPFConvertState::Running);
	const bool            bActive  = (St != EPFConvertState::Idle);
	const bool            bVerbose = VerboseCheck && VerboseCheck->IsChecked();

	// --- Button state (always update) ---
	if (CancelBtn)  CancelBtn->SetVisibility(bRunning ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (ConvertBtn) ConvertBtn->SetIsEnabled(!bRunning);

	if (!bActive) return;

	// --- Progress bar ---
	if (ConvertProgressBar)
	{
		if (bRunning)
		{
			float Prog = 0.f;
			if (Actor->CurrentConvert.IsValid())
				Prog = Actor->CurrentConvert->Progress.load();
			ConvertProgressBar->SetPercent(Prog);
			ConvertProgressBar->SetVisibility(ESlateVisibility::Visible);
		}
		else if (St == EPFConvertState::Done)
		{
			ConvertProgressBar->SetPercent(1.f);
			ConvertProgressBar->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			// Failed / Cancelled — hide bar
			ConvertProgressBar->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Helper: the outer container for the verbose log (SizeBox wrapper if present, else ScrollBox)
	UWidget* LogContainer = LogSizeBox ? static_cast<UWidget*>(LogSizeBox.Get())
	                                   : static_cast<UWidget*>(LogScroll.Get());

	// --- Verbose log vs simple status ---
	if (bVerbose)
	{
		if (StatusText)    StatusText->SetVisibility(ESlateVisibility::Collapsed);
		if (LogContainer)  LogContainer->SetVisibility(ESlateVisibility::Visible);

		if (VerboseLogText && Actor->CurrentConvert.IsValid())
		{
			const TArray<FString> Lines = Actor->CurrentConvert->GetLogLines();
			if (Lines.Num() != LastLogLineCount)
			{
				LastLogLineCount = Lines.Num();

				FString AllText;
				AllText.Reserve(Lines.Num() * 80);
				for (const FString& Line : Lines)
				{
					AllText += Line;
					AllText += TEXT("\n");
				}
				// Terminal state suffix
				switch (St)
				{
				case EPFConvertState::Done:      AllText += TEXT(">>> Done."); break;
				case EPFConvertState::Failed:    AllText += FString::Printf(TEXT(">>> Failed: %s"), *Status); break;
				case EPFConvertState::Cancelled: AllText += TEXT(">>> Cancelled."); break;
				default: break;
				}

				VerboseLogText->SetText(FText::FromString(AllText));
				if (LogScroll) LogScroll->ScrollToEnd();
			}
		}
	}
	else
	{
		// Simple one-line status
		if (LogContainer) LogContainer->SetVisibility(ESlateVisibility::Collapsed);
		if (StatusText)   StatusText->SetVisibility(ESlateVisibility::Visible);

		FString Line;
		switch (St)
		{
		case EPFConvertState::Running:   Line = FString::Printf(TEXT("Converting — %s"), Status.IsEmpty() ? TEXT("starting...") : *Status); break;
		case EPFConvertState::Done:      Line = TEXT("Done."); break;
		case EPFConvertState::Failed:    Line = FString::Printf(TEXT("Failed: %s"), *Status); break;
		case EPFConvertState::Cancelled: Line = TEXT("Cancelled."); break;
		default: return;
		}
		StatusText->SetText(FText::FromString(Line));
	}
}

void UPFConvertPanel::RefreshFromSettings()
{
	const UPFConvertSettings* S = UPFConvertSettings::Get();

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
	cfg(SpacingSpin,  0.0f,          100.0f,          0.001f, 4);
	cfg(LeafSpin,     1000.0f,        2'000'000.0f,   1000.0f, 0);
	cfg(MaxDepthSpin, 1.0f,           30.0f,            1.0f,  0);
	cfg(ChunkSpin,    0.0f,           10.0f,            1.0f,  0);
	cfg(FlushSpin,    1'000'000.0f,   268'435'456.0f, 1'000'000.0f, 0);

	if (SpacingSpin)     { SpacingSpin->SetValue(S->Spacing); }
	if (LeafSpin)        { LeafSpin->SetValue(static_cast<float>(S->LeafSize)); }
	if (MaxDepthSpin)    { MaxDepthSpin->SetValue(static_cast<float>(S->MaxDepth)); }
	if (ChunkSpin)       { ChunkSpin->SetValue(static_cast<float>(S->ChunkDepth)); }
	if (FlushSpin)       { FlushSpin->SetValue(static_cast<float>(S->FlushBudget)); }
	if (KeepChunksCheck) { KeepChunksCheck->SetIsChecked(S->bKeepChunks); }
	if (VerboseCheck)    { VerboseCheck->SetIsChecked(S->bVerbose); }
	
	if (PathBox)
	{
		if (PathBox->GetText().IsEmpty() && !S->LastSourceFile.IsEmpty())
		{
			PathBox->SetText(FText::FromString(S->LastSourceFile));
		}
		PathBox->SetHintText(FText::FromString(TEXT("e.g. E:/Model/scan.laz")));
	}
}

void UPFConvertPanel::ApplyPreset(float Spacing, int32 Leaf, int32 Depth, int32 Chunk, int64 Flush)
{
	UPFConvertSettings* S = UPFConvertSettings::Get();
	S->Spacing     = Spacing;
	S->LeafSize    = Leaf;
	S->MaxDepth    = Depth;
	S->ChunkDepth  = Chunk;
	S->FlushBudget = Flush;
	S->SaveSettings();
	RefreshFromSettings();
	if (StatusText) { StatusText->SetText(FText::FromString(TEXT("Preset applied."))); }
}

//                    Spacing  Leaf    Depth  Chunk  Flush (pts)      Compress
void UPFConvertPanel::OnPresetHighQuality() { ApplyPreset(0.f, 100000, 28, 4,  67108864LL);  } // 64 M pts RAM
void UPFConvertPanel::OnPresetMedium()      { ApplyPreset(0.f,  50000, 24, 4,  16777216LL);  } // 16 M — default
void UPFConvertPanel::OnPresetLow()         { ApplyPreset(0.f,  20000, 18, 3,   8388608LL); } //  8 M, shallow
void UPFConvertPanel::OnPresetFast()        { ApplyPreset(0.f,  20000, 14, 3,   8388608LL); } // shallow + no compress
void UPFConvertPanel::OnPresetSlow()        { ApplyPreset(0.f, 100000, 30, 5, 134217728LL);  } // 128 M, max depth

void UPFConvertPanel::OnSpacingChanged(float V)    { UPFConvertSettings* S = UPFConvertSettings::Get(); S->Spacing    = V;                          S->SaveSettings(); }
void UPFConvertPanel::OnLeafChanged(float V)       { UPFConvertSettings* S = UPFConvertSettings::Get(); S->LeafSize   = FMath::RoundToInt(V);        S->SaveSettings(); }
void UPFConvertPanel::OnMaxDepthChanged(float V)   { UPFConvertSettings* S = UPFConvertSettings::Get(); S->MaxDepth   = FMath::RoundToInt(V);        S->SaveSettings(); }
void UPFConvertPanel::OnChunkDepthChanged(float V) { UPFConvertSettings* S = UPFConvertSettings::Get(); S->ChunkDepth = FMath::RoundToInt(V);        S->SaveSettings(); }
void UPFConvertPanel::OnFlushChanged(float V)      { UPFConvertSettings* S = UPFConvertSettings::Get(); S->FlushBudget = static_cast<int64>(V);      S->SaveSettings(); }
void UPFConvertPanel::OnKeepChunksChanged(bool b)  { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bKeepChunks = b;                          S->SaveSettings(); }
void UPFConvertPanel::OnVerboseChanged(bool b)     { UPFConvertSettings* S = UPFConvertSettings::Get(); S->bVerbose    = b;                          S->SaveSettings(); }

void UPFConvertPanel::OnBrowseClicked()
{
#if PLATFORM_WINDOWS
	HWND ParentHwnd = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> TopWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (TopWindow.IsValid() && TopWindow->GetNativeWindow().IsValid())
			ParentHwnd = static_cast<HWND>(TopWindow->GetNativeWindow()->GetOSWindowHandle());
	}

	wchar_t szFile[MAX_PATH] = { 0 };
	OPENFILENAMEW ofn       = {};
	ofn.lStructSize         = sizeof(ofn);
	ofn.hwndOwner           = ParentHwnd;
	ofn.lpstrFile           = szFile;
	ofn.nMaxFile            = MAX_PATH;
	ofn.lpstrFilter         = L"Point Cloud Files\0*.laz;*.las;*.ply;*.e57;*.pts;*.xyz\0All Files\0*.*\0";
	ofn.Flags               = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	// Load comdlg32 dynamically — avoids needing comdlg32.lib in Build.cs
	typedef BOOL(WINAPI* FnGetOpenFileName)(LPOPENFILENAMEW);
	HMODULE hDll = LoadLibraryW(L"comdlg32.dll");
	FnGetOpenFileName pfn = hDll ? (FnGetOpenFileName)GetProcAddress(hDll, "GetOpenFileNameW") : nullptr;
	const bool bPicked = pfn && pfn(&ofn);
	if (hDll) FreeLibrary(hDll);

	if (bPicked)
	{
		const FString Picked = FString(szFile);
		if (PathBox) { PathBox->SetText(FText::FromString(Picked)); }
		UPFConvertSettings* S = UPFConvertSettings::Get();
		S->LastSourceFile = Picked;
		S->SaveSettings();
	}
#else
	if (StatusText)
		StatusText->SetText(FText::FromString(TEXT("Browse unavailable on this platform — type the path.")));
#endif
}

void UPFConvertPanel::SetStatusText(const FString& text)
{
	if (StatusText)
		StatusText->SetText(FText::FromString(text));
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
		// Reset verbose log for new convert
		LastLogLineCount = 0;
		if (VerboseLogText) VerboseLogText->SetText(FText::GetEmpty());
		UWidget* LogContainer = LogSizeBox ? static_cast<UWidget*>(LogSizeBox.Get())
		                                   : static_cast<UWidget*>(LogScroll.Get());
		if (LogContainer) LogContainer->SetVisibility(ESlateVisibility::Collapsed);
		if (ConvertProgressBar) ConvertProgressBar->SetPercent(0.f);

		UPFConvertSettings* S = UPFConvertSettings::Get();
		S->LastSourceFile = Path;
		S->SaveSettings();

		Actor->LoadPointCloudFile(Path);
		RefreshConvertUi();
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
		CacheLabelTimer = 2.f;
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
		CacheLabelTimer = 2.f;
	}
}
