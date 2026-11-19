// Copyright. PointForge Viewer — GPU splat compute shaders + module.

#include "PFSplatGpuShaders.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"          // FComputeShaderUtils
#include "RHICommandList.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"                // AddShaderSourceDirectoryMapping

static constexpr uint32 PFSplatThreadGroupSize = 64;

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

class FPFSplatProjectCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPFSplatProjectCS);
	SHADER_USE_PARAMETER_STRUCT(FPFSplatProjectCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, LocalToView)
		SHADER_PARAMETER(FMatrix44f, ViewToClip)
		SHADER_PARAMETER(FVector2f, Focal)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		SHADER_PARAMETER(FVector2f, JacobianLimit)
		SHADER_PARAMETER(FVector2f, DepthKeyRange)
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER(float, UnitScale)
		SHADER_PARAMETER(float, MaxAxisScale)
		SHADER_PARAMETER(float, MinPixelRadius)
		SHADER_PARAMETER(float, NearCull)
		SHADER_PARAMETER(uint32, bOrtho)
		SHADER_PARAMETER(float, MaxRadiusPx)
		SHADER_PARAMETER_SRV(StructuredBuffer<FSplatRec>, Splats)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutKeys)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutValues)
		SHADER_PARAMETER_UAV(RWBuffer<float4>, OutConic)
		SHADER_PARAMETER_UAV(RWBuffer<float>, OutWorldRadius)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutVisibleCount)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), PFSplatThreadGroupSize);
	}
};
IMPLEMENT_GLOBAL_SHADER(FPFSplatProjectCS, "/Plugin/PointForgeViewer/Private/PFSplatGPU.usf", "ProjectCS", SF_Compute);

class FPFSplatBuildQuadsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPFSplatBuildQuadsCS);
	SHADER_USE_PARAMETER_STRUCT(FPFSplatBuildQuadsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, RightLocal)
		SHADER_PARAMETER(uint32, NumQuadSlots)
		SHADER_PARAMETER(FVector3f, UpLocal)
		SHADER_PARAMETER(float, UnitScale)
		SHADER_PARAMETER_SRV(StructuredBuffer<FSplatRec>, Splats)
		SHADER_PARAMETER_SRV(Buffer<uint>, SortedValues)
		SHADER_PARAMETER_SRV(Buffer<float4>, Conic)
		SHADER_PARAMETER_SRV(Buffer<float>, WorldRadius)
		SHADER_PARAMETER_SRV(Buffer<uint>, VisibleCount)
		SHADER_PARAMETER_UAV(RWBuffer<float>, OutPositions)
		SHADER_PARAMETER_UAV(RWBuffer<float2>, OutTexCoords)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutColors)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), PFSplatThreadGroupSize);
	}
};
IMPLEMENT_GLOBAL_SHADER(FPFSplatBuildQuadsCS, "/Plugin/PointForgeViewer/Private/PFSplatGPU.usf", "BuildQuadsCS", SF_Compute);

// ---------------------------------------------------------------------------
// Dispatch helpers
// ---------------------------------------------------------------------------

bool PFSplatGpu_IsSupported(ERHIFeatureLevel::Type FeatureLevel)
{
	return FeatureLevel >= ERHIFeatureLevel::SM5;
}

void PFSplatGpu_DispatchProject(FRHICommandList& RHICmdList, const FPFSplatProjectArgs& Args, ERHIFeatureLevel::Type FeatureLevel)
{
	if (Args.NumSplats == 0)
	{
		return;
	}
	TShaderMapRef<FPFSplatProjectCS> Shader(GetGlobalShaderMap(FeatureLevel));

	FPFSplatProjectCS::FParameters P;
	P.LocalToView = Args.LocalToView;
	P.ViewToClip = Args.ViewToClip;
	P.Focal = Args.Focal;
	P.ViewportSize = Args.Viewport;
	P.JacobianLimit = Args.JacobianLimit;
	P.DepthKeyRange = Args.DepthKeyRange;
	P.NumSplats = Args.NumSplats;
	P.UnitScale = Args.UnitScale;
	P.MaxAxisScale = Args.MaxAxisScale;
	P.MinPixelRadius = Args.MinPixelRadius;
	P.NearCull = Args.NearCull;
	P.bOrtho = Args.bOrtho;
	P.MaxRadiusPx = Args.MaxRadiusPx;
	P.Splats = Args.Splats;
	P.OutKeys = Args.OutKeys;
	P.OutValues = Args.OutValues;
	P.OutConic = Args.OutConic;
	P.OutWorldRadius = Args.OutWorldRadius;
	P.OutVisibleCount = Args.OutVisibleCount;

	// Wrapped group count: > 4M splats would exceed 65535 groups in X.
	const FIntVector Groups = FComputeShaderUtils::GetGroupCountWrapped(Args.NumSplats, PFSplatThreadGroupSize);
	FComputeShaderUtils::Dispatch(RHICmdList, Shader, P, Groups);
}

void PFSplatGpu_DispatchBuildQuads(FRHICommandList& RHICmdList, const FPFSplatQuadArgs& Args, ERHIFeatureLevel::Type FeatureLevel)
{
	if (Args.NumQuadSlots == 0)
	{
		return;
	}
	TShaderMapRef<FPFSplatBuildQuadsCS> Shader(GetGlobalShaderMap(FeatureLevel));

	FPFSplatBuildQuadsCS::FParameters P;
	P.RightLocal = Args.RightLocal;
	P.NumQuadSlots = Args.NumQuadSlots;
	P.UpLocal = Args.UpLocal;
	P.UnitScale = Args.UnitScale;
	P.Splats = Args.Splats;
	P.SortedValues = Args.SortedValues;
	P.Conic = Args.Conic;
	P.WorldRadius = Args.WorldRadius;
	P.VisibleCount = Args.VisibleCount;
	P.OutPositions = Args.OutPositions;
	P.OutTexCoords = Args.OutTexCoords;
	P.OutColors = Args.OutColors;

	const FIntVector Groups = FComputeShaderUtils::GetGroupCountWrapped(Args.NumQuadSlots, PFSplatThreadGroupSize);
	FComputeShaderUtils::Dispatch(RHICmdList, Shader, P, Groups);
}

// ---------------------------------------------------------------------------
// Module: map /Plugin/PointForgeViewer -> <plugin>/Shaders
// ---------------------------------------------------------------------------

class FPointForgeViewerShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PointForgeViewer"));
		if (Plugin.IsValid())
		{
			const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/PointForgeViewer"), ShaderDir);
		}
	}
};

IMPLEMENT_MODULE(FPointForgeViewerShadersModule, PointForgeViewerShaders)
