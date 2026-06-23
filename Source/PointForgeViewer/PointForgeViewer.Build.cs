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
			"UMG",       // viewer panel (UUserWidget + controls)
			"Slate",
			"SlateCore",
			"DeveloperSettings", // UPFConvertSettings (persistent convert params)
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("comdlg32.lib");
		}

		
		string VcpkgDir = Path.Combine(PointForgeRoot, "build", "vcpkg_installed", "x64-windows");
		string ZstdInc = Path.Combine(VcpkgDir, "include");
		string ZstdLib = Path.Combine(VcpkgDir, "lib", "zstd.lib");
		
		if (Directory.Exists(ZstdInc) && File.Exists(ZstdLib))
		{
			PrivateIncludePaths.Add(ZstdInc);
			PublicAdditionalLibraries.Add(ZstdLib);
			PublicDefinitions.Add("WITH_ZSTD=1");
			
			string ZstdDll = Path.Combine(VcpkgDir, "bin", "zstd.dll");
			if (File.Exists(ZstdDll))
			{
				RuntimeDependencies.Add("$(BinaryOutputDir)/zstd.dll", ZstdDll);
				PublicDelayLoadDLLs.Add("zstd.dll");
			}
		}
		else
		{
			Console.WriteLine("PointForgeViewer: Zstd library not found at " + ZstdLib);
		}

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

		// Stage pfconvert.exe and companion DLLs for standalone/packaged builds.
		// Two-arg RuntimeDependencies.Add(dest, src) lets UBT copy from the build
		// dir even when the file doesn't yet live under the plugin tree.
		// Destination uses the absolute ThirdParty path so pfconvert's DLL search
		// (same directory as the exe) finds laszip3.dll / zstd.dll next to it.
		string ThirdPartyDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Binaries", "ThirdParty"));
		string PfReleaseDir  = Path.Combine(PointForgeRoot, "build", "Release");
		string VcpkgBinDir   = Path.Combine(PointForgeRoot, "build", "vcpkg_installed", "x64-windows", "bin");

		// Ordered: pfconvert.exe first so the log is easy to read.
		var BundledFiles = new System.Collections.Generic.Dictionary<string, string[]>
		{
			// filename → candidate source dirs (first existing file wins)
			{ "pfconvert.exe",    new[] { PfReleaseDir, ThirdPartyDir } },
			{ "laszip3.dll",      new[] { PfReleaseDir, ThirdPartyDir } },
			{ "zstd.dll",         new[] { VcpkgBinDir,  PfReleaseDir, ThirdPartyDir } },
			{ "E57Format.dll",    new[] { PfReleaseDir, ThirdPartyDir } },
			{ "xerces-c_3_3.dll", new[] { PfReleaseDir, ThirdPartyDir } },
		};

		foreach (var kvp in BundledFiles)
		{
			string FileName = kvp.Key;
			string SrcPath  = "";
			foreach (string Dir in kvp.Value)
			{
				string Candidate = Path.Combine(Dir, FileName);
				if (File.Exists(Candidate)) { SrcPath = Candidate; break; }
			}
			if (!string.IsNullOrEmpty(SrcPath))
			{
					// $(PluginDir) resolves correctly in both editor and packaged builds.
				RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/" + FileName, SrcPath);
				Console.WriteLine("PointForgeViewer: bundling " + FileName + " from " + SrcPath);
			}
			else
			{
				Console.WriteLine("PointForgeViewer: standalone bundle not found, skipping: " + FileName);
			}
		}
	}
}
