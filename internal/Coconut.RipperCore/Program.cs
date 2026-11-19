using AssetRipper.Export.Configuration;
using AssetRipper.Export.UnityProjects;
using AssetRipper.Import.Configuration;
using AssetRipper.Import.Logging;
using AssetRipper.IO.Files;
using AssetRipper.Primitives;
using AssetRipper.Processing;
using AssetRipper.SourceGenerated.Classes.ClassID_115;
using AssetRipper.SourceGenerated.Classes.ClassID_28;
using AssetRipper.SourceGenerated.Classes.ClassID_43;
using AssetRipper.SourceGenerated.Classes.ClassID_83;
using System.Text.Json;

namespace Coconut.RipperCore;

internal static class Program
{
	private static readonly JsonSerializerOptions PrettyJson = new() { WriteIndented = true };

	public static int Main(string[] args)
	{
		if (args.Length == 0 || args[0] is "--version" or "-v")
		{
			Console.WriteLine("Coconut.RipperCore 0.2.0 (Internal AssetRipper 1.3.14 Fork)");
			return 0;
		}

		string cmd = args[0].ToLowerInvariant();
		return cmd switch
		{
			"inspect" => RunInspect(args[1..]),
			"export" => RunExport(args[1..]),
			_ => Usage(),
		};
	}

	private static int Usage()
	{
		Console.Error.WriteLine("Usage:");
		Console.Error.WriteLine("  Coconut.RipperCore --version");
		Console.Error.WriteLine("  Coconut.RipperCore inspect --input <gamePath>");
		Console.Error.WriteLine("  Coconut.RipperCore export --input <gamePath> --output <outDir> [options]");
		return 2;
	}

	private static int RunInspect(string[] args)
	{
		string input = GetArg(args, "--input", "");
		CoconutInspectReport report = new();
		if (string.IsNullOrWhiteSpace(input))
		{
			report.Ok = false;
			report.Error = "Missing --input path";
			Console.WriteLine(JsonSerializer.Serialize(report, PrettyJson));
			return 1;
		}

		try
		{
			FullConfiguration config = new();
			ExportHandler handler = new(config);
			GameData data = handler.Load([input], LocalFileSystem.Instance);
			report.Ok = true;
			report.UnityVersion = data.ProjectVersion.ToString();
			report.Platform = data.PlatformStructure?.GetType().Name ?? "Generic";
			report.SceneCount = data.GameBundle.Scenes.Count();
			foreach (var asset in data.GameBundle.FetchAssets())
			{
				report.AssetCount++;
				if (asset is IMesh) report.MeshCount++;
				else if (asset is ITexture2D) report.TextureCount++;
				else if (asset is IAudioClip) report.AudioCount++;
				else if (asset is IMonoScript) report.ScriptCount++;
			}
			Console.WriteLine(JsonSerializer.Serialize(report, PrettyJson));
			return 0;
		}
		catch (Exception ex)
		{
			report.Ok = false;
			report.Error = ex.Message;
			Console.WriteLine(JsonSerializer.Serialize(report, PrettyJson));
			return 1;
		}
	}

	private static int RunExport(string[] args)
	{
		CoconutExportOptions opt = new()
		{
			InputPath = GetArg(args, "--input", ""),
			OutputPath = GetArg(args, "--output", ""),
			TargetUnityVersion = GetArg(args, "--target-version", "Match Source"),
			MeshFormat = GetArg(args, "--mesh-format", "fbx"),
			AudioFormat = GetArg(args, "--audio-format", "wav"),
			ImageFormat = GetArg(args, "--image-format", "png"),
			ScriptMode = GetArg(args, "--script-mode", "hybrid"),
			DecombineMeshes = GetArg(args, "--decombine-meshes", "1") != "0",
			ReconstructPrefabs = GetArg(args, "--reconstruct-prefabs", "1") != "0",
			FixBuildErrors = GetArg(args, "--fix-build-errors", "1") != "0",
			JsonReportPath = GetArg(args, "--json-report", ""),
		};

		CoconutExportReport report = new()
		{
			TargetUnityVersion = opt.TargetUnityVersion,
		};

		if (string.IsNullOrWhiteSpace(opt.InputPath) || string.IsNullOrWhiteSpace(opt.OutputPath))
		{
			report.Ok = false;
			report.Error = "Missing --input or --output";
			EmitReport(report, opt.JsonReportPath);
			return 1;
		}

		try
		{
			Logger.Add(new ConsoleLogger(false));

			FullConfiguration config = new();
			config.ProcessingSettings.EnableStaticMeshSeparation = opt.DecombineMeshes;
			config.ProcessingSettings.EnablePrefabOutlining = opt.ReconstructPrefabs;
			config.ProcessingSettings.RemoveNullableAttributes = opt.FixBuildErrors;

			config.ExportSettings.ImageExportFormat = opt.ImageFormat.ToLowerInvariant() switch
			{
				"tga" => ImageExportFormat.Tga,
				"bmp" => ImageExportFormat.Bmp,
				"jpg" or "jpeg" => ImageExportFormat.Jpeg,
				"exr" => ImageExportFormat.Exr,
				_ => ImageExportFormat.Png,
			};

			config.ExportSettings.AudioExportFormat = opt.AudioFormat.ToLowerInvariant() switch
			{
				"native" => AudioExportFormat.Native,
				"wav" or "m4a" => AudioExportFormat.PreferWav,
				_ => AudioExportFormat.Default,
			};

			config.ExportSettings.ScriptExportMode = opt.ScriptMode.ToLowerInvariant() switch
			{
				"decompiled" => ScriptExportMode.Decompiled,
				"dll" => ScriptExportMode.DllExportWithoutRenaming,
				_ => ScriptExportMode.Hybrid,
			};

			config.ImportSettings.ScriptContentLevel = opt.ScriptMode.Equals("stubs", StringComparison.OrdinalIgnoreCase)
				? ScriptContentLevel.Level1
				: ScriptContentLevel.Level2;

			if (!string.IsNullOrWhiteSpace(opt.TargetUnityVersion) &&
				!opt.TargetUnityVersion.Equals("Match Source", StringComparison.OrdinalIgnoreCase) &&
				UnityVersion.TryParse(opt.TargetUnityVersion, out UnityVersion parsedTarget, out _))
			{
				config.ImportSettings.TargetVersion = parsedTarget;
			}

			Directory.CreateDirectory(opt.OutputPath);

			CoconutExportHandler handler = new(config, opt, report);
			GameData data = handler.LoadAndProcess([opt.InputPath], LocalFileSystem.Instance);
			report.SourceUnityVersion = data.ProjectVersion.ToString();
			report.MeshesDecombined = handler.StaticMeshSeparator?.DecombinedCount ?? 0;

			handler.Export(data, opt.OutputPath, LocalFileSystem.Instance);

			report.OutputProjectPath = config.ProjectRootPath;
			report.Ok = true;
			EmitReport(report, opt.JsonReportPath);
			return 0;
		}
		catch (Exception ex)
		{
			report.Ok = false;
			report.Error = ex.ToString();
			EmitReport(report, opt.JsonReportPath);
			return 1;
		}
	}

	private static void EmitReport(CoconutExportReport report, string reportPath)
	{
		string json = JsonSerializer.Serialize(report, PrettyJson);
		if (!string.IsNullOrWhiteSpace(reportPath))
		{
			Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(reportPath))!);
			File.WriteAllText(reportPath, json);
		}
		Console.WriteLine(json);
	}

	private static string GetArg(string[] args, string flag, string fallback)
	{
		for (int i = 0; i + 1 < args.Length; i++)
		{
			if (args[i].Equals(flag, StringComparison.OrdinalIgnoreCase))
			{
				return args[i + 1];
			}
		}
		return fallback;
	}
}
