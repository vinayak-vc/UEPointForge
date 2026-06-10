// Copyright. PointForge Viewer module build rules.
using System;
using System.IO;
using UnrealBuildTool;

public class PointForgeViewer : ModuleRules
{
	// Flip to TRUE to compile pfcore's convert pipeline (pf::buildOctree) directly
	// into this module. Requires a prebuilt pfcore.lib (+ laszip3.lib) from the
	// PointForge CMake build, with a CRT matching UE (Development/Shipping use /MD).
	//
	// Default FALSE => the convert step shells out to a bundled pfconvert.exe
	// (no extra link deps, runs out-of-process: easy to cancel, can't corrupt the
	// UE heap, and a 20-90 min batch job belongs in its own process anyway).
	private readonly bool bLinkPfcoreInProcess = false;

	// Root of the PointForge C++ repo. Adjust if you relocate it.
	private static string PointForgeRoot => @"C:\UnrealProject\PointForge";

	public PointForgeViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Inherit UE 5.5's default C++ standard (C++20). Do NOT force Cpp17 — the
		// engine headers (SceneView.h, InstanceDataTypes.h) use C++20 features.

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"Projects", // IPluginManager (locate bundled pfconvert.exe)
		});

		// The on-disk format structs are mirrored in PFOctreeFormat.h (size-asserted),
		// so the viewer does NOT need pfcore headers for streaming. The include path
		// below is only needed for the optional in-process convert (OctreeIndexer.h).
		if (bLinkPfcoreInProcess)
		{
			string Src = Path.Combine(PointForgeRoot, "src");
			if (Directory.Exists(Src))
			{
				PrivateIncludePaths.Add(Src);
			}

			// Prebuilt by:  cmake --build build --config Release   (in PointForgeRoot)
			string LibDir = Path.Combine(PointForgeRoot, "build", "Release");
			string PfcoreLib = Path.Combine(LibDir, "pfcore.lib");
			if (File.Exists(PfcoreLib))
			{
				PublicAdditionalLibraries.Add(PfcoreLib);
				PublicDefinitions.Add("PF_LINK_PFCORE=1");

				// pfcore links LASzip transitively when built with PF_WITH_LAS.
				string LaszipLib = Path.Combine(LibDir, "laszip3.lib");
				if (File.Exists(LaszipLib))
				{
					PublicAdditionalLibraries.Add(LaszipLib);
					string LaszipDll = Path.Combine(LibDir, "laszip3.dll");
					if (File.Exists(LaszipDll))
					{
						RuntimeDependencies.Add("$(BinaryOutputDir)/laszip3.dll", LaszipDll);
					}
				}

				// pfcore may use C++ exceptions internally; enable if the linker complains.
				// bEnableExceptions = true;
			}
			else
			{
				Console.WriteLine("PointForgeViewer: bLinkPfcoreInProcess=true but pfcore.lib not found at "
					+ PfcoreLib + " -- will fall back to pfconvert.exe at runtime.");
			}
		}

		if (!PublicDefinitions.Contains("PF_LINK_PFCORE=1"))
		{
			PublicDefinitions.Add("PF_LINK_PFCORE=0");
		}
	}
}
