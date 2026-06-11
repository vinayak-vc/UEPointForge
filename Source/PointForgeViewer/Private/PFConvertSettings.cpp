#include "PFConvertSettings.h"

UPFConvertSettings* UPFConvertSettings::Get()
{
	return GetMutableDefault<UPFConvertSettings>();
}

void UPFConvertSettings::SaveSettings()
{
	SaveConfig();
}

void UPFConvertSettings::ResetToDefaults()
{
	Spacing = 0.0f;
	LeafSize = 50000;
	MaxDepth = 24;
	ChunkDepth = 4;
	FlushBudget = 16 * 1024 * 1024;
	bKeepChunks = false;
	bVerbose = false;
	SaveConfig();
}

FString UPFConvertSettings::ToArgs() const
{
	FString Args;
	Args += FString::Printf(TEXT(" --chunk-depth %d"), ChunkDepth);
	Args += FString::Printf(TEXT(" --spacing %f"), Spacing);
	Args += FString::Printf(TEXT(" --leaf %d"), LeafSize);
	Args += FString::Printf(TEXT(" --max-depth %d"), MaxDepth);
	Args += FString::Printf(TEXT(" --flush %lld"), FlushBudget);
	if (bKeepChunks) { Args += TEXT(" --keep-chunks"); }
	if (bVerbose)    { Args += TEXT(" --verbose"); }
	return Args;
}

FString UPFConvertSettings::KeyString() const
{
	// Only parameters that change the produced octree (keep-chunks/verbose excluded).
	return FString::Printf(TEXT("cd%d_sp%.4f_lf%d_md%d_fl%lld"),
		ChunkDepth, Spacing, LeafSize, MaxDepth, FlushBudget);
}
