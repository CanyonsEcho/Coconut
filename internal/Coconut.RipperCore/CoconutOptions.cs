using System.Text.Json.Serialization;

namespace Coconut.RipperCore;

public sealed class CoconutExportOptions
{
	public string InputPath { get; set; } = string.Empty;
	public string OutputPath { get; set; } = string.Empty;
	public string TargetUnityVersion { get; set; } = "Match Source";
	public string MeshFormat { get; set; } = "fbx";       // fbx | glb | obj | native
	public string AudioFormat { get; set; } = "wav";      // wav | m4a | ogg | native
	public string ImageFormat { get; set; } = "png";      // png | tga | bmp | jpg | exr
	public string ScriptMode { get; set; } = "hybrid";    // decompiled | hybrid | dll | stubs
	public bool DecombineMeshes { get; set; } = true;
	public bool ReconstructPrefabs { get; set; } = true;
	public bool FixBuildErrors { get; set; } = true;
	public string JsonReportPath { get; set; } = string.Empty;
}

public sealed class CoconutExportReport
{
	[JsonPropertyName("ok")]
	public bool Ok { get; set; }

	[JsonPropertyName("engine")]
	public string Engine { get; set; } = "Coconut.RipperCore (Internal AssetRipper 1.3.14 Fork)";

	[JsonPropertyName("sourceUnityVersion")]
	public string SourceUnityVersion { get; set; } = string.Empty;

	[JsonPropertyName("targetUnityVersion")]
	public string TargetUnityVersion { get; set; } = string.Empty;

	[JsonPropertyName("outputProjectPath")]
	public string OutputProjectPath { get; set; } = string.Empty;

	[JsonPropertyName("scenesExported")]
	public int ScenesExported { get; set; }

	[JsonPropertyName("scriptsExported")]
	public int ScriptsExported { get; set; }

	[JsonPropertyName("meshesExported")]
	public int MeshesExported { get; set; }

	[JsonPropertyName("audioExported")]
	public int AudioExported { get; set; }

	[JsonPropertyName("texturesExported")]
	public int TexturesExported { get; set; }

	[JsonPropertyName("meshesDecombined")]
	public int MeshesDecombined { get; set; }

	[JsonPropertyName("prefabsReconstructed")]
	public int PrefabsReconstructed { get; set; }

	[JsonPropertyName("buildErrorsFixed")]
	public int BuildErrorsFixed { get; set; }

	[JsonPropertyName("shadersFixed")]
	public int ShadersFixed { get; set; }

	[JsonPropertyName("messages")]
	public List<string> Messages { get; set; } = [];

	[JsonPropertyName("error")]
	public string? Error { get; set; }
}

public sealed class CoconutInspectReport
{
	[JsonPropertyName("ok")]
	public bool Ok { get; set; }

	[JsonPropertyName("unityVersion")]
	public string UnityVersion { get; set; } = string.Empty;

	[JsonPropertyName("platform")]
	public string Platform { get; set; } = string.Empty;

	[JsonPropertyName("sceneCount")]
	public int SceneCount { get; set; }

	[JsonPropertyName("assetCount")]
	public int AssetCount { get; set; }

	[JsonPropertyName("meshCount")]
	public int MeshCount { get; set; }

	[JsonPropertyName("textureCount")]
	public int TextureCount { get; set; }

	[JsonPropertyName("audioCount")]
	public int AudioCount { get; set; }

	[JsonPropertyName("scriptCount")]
	public int ScriptCount { get; set; }

	[JsonPropertyName("error")]
	public string? Error { get; set; }
}
