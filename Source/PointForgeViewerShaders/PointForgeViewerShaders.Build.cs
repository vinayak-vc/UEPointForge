// Copyright. PointForge Viewer — GPU splat shaders module.
//
// Global (compute) shaders must be registered before the engine compiles its
// global shader map, so this module loads at PostConfigInit. It holds only the
// shader classes, the /Plugin/PointForgeViewer shader-directory mapping and thin
// dispatch helpers; everything that touches UObjects stays in PointForgeViewer.
using UnrealBuildTool;

public class PointForgeViewerShaders : ModuleRules
{
	public PointForgeViewerShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"RHI",
			"RenderCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",   // IPluginManager: locate the plugin's Shaders directory
		});
	}
}
