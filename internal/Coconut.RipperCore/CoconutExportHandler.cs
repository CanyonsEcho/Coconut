using AssetRipper.Export.Configuration;
using AssetRipper.Export.UnityProjects;
using AssetRipper.Import.Configuration;
using AssetRipper.Primitives;
using AssetRipper.Processing;
using AssetRipper.Processing.Editor;
using AssetRipper.Processing.Prefabs;
using AssetRipper.SourceGenerated.Classes.ClassID_43;
using AssetRipper.SourceGenerated.Classes.ClassID_83;

namespace Coconut.RipperCore;

/// <summary>
/// Coconut's internal subclass of AssetRipper's ExportHandler.
/// Adds real static-mesh separation, prefab reconstruction, FBX/OBJ/GLB/M4A exporters,
/// target Unity version retargeting, and automated build-error fixups.
/// </summary>
public sealed class CoconutExportHandler : ExportHandler
{
	private readonly CoconutExportOptions _options;
	private readonly CoconutExportReport _report;

	public CoconutStaticMeshSeparationProcessor? StaticMeshSeparator { get; private set; }
	public CoconutPrefabRecoveryProcessor? PrefabRecovery { get; private set; }
	public CoconutMeshExporter? CustomMeshExporter { get; private set; }
	public CoconutAudioExporter? CustomAudioExporter { get; private set; }

	public CoconutExportHandler(FullConfiguration settings, CoconutExportOptions options, CoconutExportReport report)
		: base(settings)
	{
		_options = options;
		_report = report;
	}

	protected override IEnumerable<IAssetProcessor> GetProcessors()
	{
		foreach (IAssetProcessor processor in base.GetProcessors())
		{
			// Inject Coconut's static-mesh separator right before LightingDataProcessor
			if (_options.DecombineMeshes && processor is LightingDataProcessor)
			{
				StaticMeshSeparator = new CoconutStaticMeshSeparationProcessor();
				yield return StaticMeshSeparator;
			}

			yield return processor;

			// Inject Coconut's prefab recovery analyzer right after AssetRipper's PrefabProcessor
			if (_options.ReconstructPrefabs && processor is PrefabProcessor)
			{
				PrefabRecovery = new CoconutPrefabRecoveryProcessor();
				yield return PrefabRecovery;
			}
		}
	}

	protected override void BeforeExport(ProjectExporter projectExporter)
	{
		base.BeforeExport(projectExporter);

		// Retarget settings.Version if the user selected a specific target Unity version
		if (!string.IsNullOrWhiteSpace(_options.TargetUnityVersion) &&
			!_options.TargetUnityVersion.Equals("Match Source", StringComparison.OrdinalIgnoreCase) &&
			UnityVersion.TryParse(_options.TargetUnityVersion, out UnityVersion targetVersion, out _))
		{
			Settings.SetProjectSettings(targetVersion);
		}

		// Install Coconut's custom mesh exporter (FBX, GLB, OBJ) when not native YAML
		string meshFmt = _options.MeshFormat.ToLowerInvariant();
		if (meshFmt is "fbx" or "glb" or "obj")
		{
			CustomMeshExporter = new CoconutMeshExporter(meshFmt);
			projectExporter.OverrideExporter<IMesh>(CustomMeshExporter);
		}

		// Install Coconut's custom audio exporter (M4A/WAV) when M4A requested
		string audioFmt = _options.AudioFormat.ToLowerInvariant();
		if (audioFmt is "m4a" or "wav")
		{
			CustomAudioExporter = new CoconutAudioExporter(audioFmt);
			projectExporter.OverrideExporter<IAudioClip>(CustomAudioExporter);
		}
	}

	protected override IEnumerable<IPostExporter> GetPostExporters()
	{
		foreach (IPostExporter post in base.GetPostExporters())
		{
			// Skip DllPostExporter when full decompiled scripts are requested (avoids AsmResolver crashes on obfuscated/minimal DLLs)
			if (_options.ScriptMode.Equals("decompiled", StringComparison.OrdinalIgnoreCase) &&
				post.GetType().Name.Contains("DllPostExporter", StringComparison.Ordinal))
			{
				continue;
			}
			yield return post;
		}
		yield return new CoconutProjectFixupPostExporter(_options, PrefabRecovery, _report);
	}
}
