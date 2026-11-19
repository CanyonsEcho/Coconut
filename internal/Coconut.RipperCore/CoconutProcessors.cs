using AssetRipper.Assets.Bundles;
using AssetRipper.Assets.Collections;
using AssetRipper.Import.Logging;
using AssetRipper.Numerics;
using AssetRipper.Processing;
using AssetRipper.SourceGenerated;
using AssetRipper.SourceGenerated.Classes.ClassID_1;
using AssetRipper.SourceGenerated.Classes.ClassID_23;
using AssetRipper.SourceGenerated.Classes.ClassID_33;
using AssetRipper.SourceGenerated.Classes.ClassID_4;
using AssetRipper.SourceGenerated.Classes.ClassID_43;
using AssetRipper.SourceGenerated.Enums;
using AssetRipper.SourceGenerated.Extensions;
using AssetRipper.SourceGenerated.Subclasses.SubMesh;
using System.Numerics;
using System.Text.RegularExpressions;

namespace Coconut.RipperCore;

/// <summary>
/// Coconut's custom static-batch mesh separation processor, injected directly into
/// the forked AssetRipper processing pipeline (where AssetRipper leaves its placeholder).
/// Splits Unity "Combined Mesh (root scene)" batches back into local-space per-object meshes.
/// </summary>
public sealed class CoconutStaticMeshSeparationProcessor : IAssetProcessor
{
	public int DecombinedCount { get; private set; }

	public void Process(GameData gameData)
	{
		ProcessedBundle processedBundle = gameData.GameBundle.AddNewProcessedBundle("Coconut Decombined Meshes");
		ProcessedAssetCollection meshCollection = processedBundle.AddNewProcessedCollection("Decombined Meshes", gameData.ProjectVersion);

		Dictionary<IMesh, MeshData> decodedCache = new();

		foreach (IMeshRenderer renderer in gameData.GameBundle.FetchAssets().OfType<IMeshRenderer>().ToList())
		{
			IGameObject? go = renderer.GameObject_C25P;
			if (go is null || !go.TryGetComponent<IMeshFilter>(out IMeshFilter? meshFilter))
			{
				continue;
			}

			IMesh? sharedMesh = meshFilter.MeshP;
			if (sharedMesh is null)
			{
				continue;
			}

			int firstSubMesh = 0;
			int subMeshCount = 0;
			bool hasStaticBatch = false;

			if (renderer.Has_StaticBatchInfo_C25())
			{
				firstSubMesh = renderer.StaticBatchInfo_C25.FirstSubMesh;
				subMeshCount = renderer.StaticBatchInfo_C25.SubMeshCount;
				hasStaticBatch = subMeshCount > 0;
			}

			if (!hasStaticBatch && !sharedMesh.IsCombinedMesh())
			{
				continue;
			}

			if (!decodedCache.TryGetValue(sharedMesh, out MeshData srcData))
			{
				if (!MeshData.TryMakeFromMesh(sharedMesh, out srcData))
				{
					continue;
				}
				decodedCache[sharedMesh] = srcData;
			}

			if (!hasStaticBatch)
			{
				firstSubMesh = 0;
				subMeshCount = srcData.SubMeshes.Length;
			}

			if (firstSubMesh < 0 || subMeshCount <= 0 || firstSubMesh + subMeshCount > srcData.SubMeshes.Length)
			{
				continue;
			}

			// Compute world-to-local matrix so baked world vertices return to local GameObject space
			Matrix4x4 worldToLocal = ComputeWorldToLocalMatrix(go);
			Matrix4x4.Invert(Matrix4x4.Transpose(worldToLocal), out Matrix4x4 normalMatrix);

			// Remap used vertices into a tight per-object buffer
			Dictionary<uint, uint> remap = new();
			List<Vector3> outVerts = [];
			List<Vector3>? outNormals = srcData.HasNormals ? [] : null;
			List<Vector4>? outTangents = srcData.HasTangents ? [] : null;
			List<ColorFloat>? outColors = srcData.HasColors ? [] : null;
			List<Vector2>? outUv0 = srcData.HasUV0 ? [] : null;
			List<Vector2>? outUv1 = srcData.HasUV1 ? [] : null;
			List<uint> outIndices = [];
			List<SubMeshData> outSubMeshes = [];

			for (int s = 0; s < subMeshCount; s++)
			{
				SubMeshData sm = srcData.SubMeshes[firstSubMesh + s];
				int newFirstIndex = outIndices.Count;
				int start = sm.FirstIndex;
				int end = Math.Min(start + sm.IndexCount, srcData.ProcessedIndexBuffer.Length);

				Vector3 bMin = new(float.MaxValue);
				Vector3 bMax = new(float.MinValue);

				for (int i = start; i < end; i++)
				{
					uint oldIdx = srcData.ProcessedIndexBuffer[i] + sm.BaseVertex;
					if (oldIdx >= (uint)srcData.Vertices.Length)
					{
						continue;
					}

					if (!remap.TryGetValue(oldIdx, out uint newIdx))
					{
						newIdx = (uint)outVerts.Count;
						remap[oldIdx] = newIdx;

						Vector3 p = Vector3.Transform(srcData.Vertices[oldIdx], worldToLocal);
						outVerts.Add(p);
						bMin = Vector3.Min(bMin, p);
						bMax = Vector3.Max(bMax, p);

						if (outNormals is not null && srcData.Normals is not null)
						{
							Vector3 n = Vector3.TransformNormal(srcData.Normals[oldIdx], normalMatrix);
							float len = n.Length();
							outNormals.Add(len > 1e-6f ? n / len : Vector3.UnitY);
						}
						if (outTangents is not null && srcData.Tangents is not null)
						{
							Vector4 t = srcData.Tangents[oldIdx];
							Vector3 tv = Vector3.TransformNormal(new Vector3(t.X, t.Y, t.Z), normalMatrix);
							float len = tv.Length();
							if (len > 1e-6f) tv /= len;
							outTangents.Add(new Vector4(tv.X, tv.Y, tv.Z, t.W));
						}
						if (outColors is not null && srcData.Colors is not null)
						{
							outColors.Add(srcData.Colors[oldIdx]);
						}
						if (outUv0 is not null && srcData.UV0 is not null)
						{
							outUv0.Add(srcData.UV0[oldIdx]);
						}
						if (outUv1 is not null && srcData.UV1 is not null)
						{
							outUv1.Add(srcData.UV1[oldIdx]);
						}
					}

					outIndices.Add(newIdx);
				}

				int addedIndices = outIndices.Count - newFirstIndex;
				if (addedIndices <= 0)
				{
					continue;
				}

				Vector3 center = (bMin + bMax) * 0.5f;
				Vector3 extents = (bMax - bMin) * 0.5f;
				outSubMeshes.Add(new SubMeshData(
					0,
					newFirstIndex,
					0,
					addedIndices,
					addedIndices / 3,
					outVerts.Count,
					sm.Topology,
					new Bounds(center, extents)));
			}

			if (outVerts.Count == 0 || outIndices.Count == 0)
			{
				continue;
			}

			IMesh newMesh = meshCollection.CreateMesh();
			newMesh.Name = $"{SanitizeMeshName(go.Name)}_Decombined";
			newMesh.SetIndexFormat(outVerts.Count > ushort.MaxValue ? IndexFormat.UInt32 : IndexFormat.UInt16);

			MeshData splitData = new(
				outVerts.ToArray(),
				outNormals?.ToArray(),
				outTangents?.ToArray(),
				outColors?.ToArray(),
				outUv0?.ToArray(),
				outUv1?.ToArray(),
				null, null, null, null, null, null,
				null,
				null,
				outIndices.ToArray(),
				outSubMeshes.ToArray());

			VertexDataBlob blob = VertexDataBlob.Create(splitData, newMesh.Collection.Version, newMesh.Collection.EndianType);
			newMesh.VertexData.VertexCount = (uint)blob.VertexCount;
			newMesh.VertexData.Data = blob.Data;
			if (newMesh.VertexData.Has_Channels())
			{
				newMesh.VertexData.Channels.Clear();
				foreach (var ch in blob.Channels)
				{
					var dst = newMesh.VertexData.Channels.AddNew();
					dst.Stream = ch.Stream;
					dst.Offset = ch.Offset;
					dst.Format = ch.Format;
					dst.Dimension = ch.Dimension;
				}
			}

			newMesh.SetProcessedIndexBuffer(splitData.ProcessedIndexBuffer);
			newMesh.SubMeshes.Clear();
			IndexFormat idxFmt = newMesh.GetIndexFormat();
			foreach (SubMeshData sm in splitData.SubMeshes)
			{
				ISubMesh dst = newMesh.SubMeshes.AddNew();
				sm.CopyTo(dst, idxFmt);
			}

			// Compute overall local AABB
			Vector3 totalMin = new(float.MaxValue);
			Vector3 totalMax = new(float.MinValue);
			foreach (Vector3 v in splitData.Vertices)
			{
				totalMin = Vector3.Min(totalMin, v);
				totalMax = Vector3.Max(totalMax, v);
			}
			newMesh.LocalAABB.Center.CopyValues((totalMin + totalMax) * 0.5f);
			newMesh.LocalAABB.Extent.CopyValues((totalMax - totalMin) * 0.5f);

			meshFilter.MeshP = newMesh;
			if (renderer.Has_StaticBatchInfo_C25())
			{
				renderer.StaticBatchInfo_C25.FirstSubMesh = 0;
				renderer.StaticBatchInfo_C25.SubMeshCount = 0;
			}
			renderer.MarkGameObjectAsStatic();
			DecombinedCount++;
		}

		Logger.Info(LogCategory.Processing, $"[Coconut] Static mesh separation recovered {DecombinedCount} meshes.");
	}

	private static Matrix4x4 ComputeWorldToLocalMatrix(IGameObject go)
	{
		if (!go.TryGetComponent<ITransform>(out ITransform? transform))
		{
			return Matrix4x4.Identity;
		}

		// Accumulate local-to-world from root down to this transform
		List<ITransform> chain = [];
		ITransform? cur = transform;
		int guard = 0;
		while (cur is not null && guard++ < 64)
		{
			chain.Add(cur);
			cur = cur.Father_C4P;
		}

		Matrix4x4 localToWorld = Matrix4x4.Identity;
		for (int i = chain.Count - 1; i >= 0; i--)
		{
			ITransform t = chain[i];
			Vector3 pos = t.LocalPosition_C4.CastToStruct();
			Quaternion rot = t.LocalRotation_C4.CastToStruct();
			Vector3 scale = t.LocalScale_C4.CastToStruct();
			Matrix4x4 m = Matrix4x4.CreateScale(scale)
				* Matrix4x4.CreateFromQuaternion(rot)
				* Matrix4x4.CreateTranslation(pos);
			localToWorld = m * localToWorld;
		}

		return Matrix4x4.Invert(localToWorld, out Matrix4x4 inv) ? inv : Matrix4x4.Identity;
	}

	private static string SanitizeMeshName(string raw)
	{
		if (string.IsNullOrWhiteSpace(raw)) return "Mesh";
		return Regex.Replace(raw, @"[^A-Za-z0-9_\-]", "_");
	}
}

public sealed record CoconutPrefabBlueprint(
	string PrefabName,
	string Signature,
	int InstanceCount,
	List<string> ComponentNames,
	List<string> ChildNames,
	Vector3 DefaultScale);

/// <summary>
/// Analyzes scenes after AssetRipper's PrefabProcessor to detect repeated scene object
/// hierarchies (e.g., cloned props/enemies/pickups) and prepare clean reusable .prefab assets.
/// </summary>
public sealed partial class CoconutPrefabRecoveryProcessor : IAssetProcessor
{
	[GeneratedRegex(@"\s*\(\d+\)$|[_\-\s]+\d+$")]
	private static partial Regex InstanceSuffixRegex();

	public List<CoconutPrefabBlueprint> Blueprints { get; } = [];

	public void Process(GameData gameData)
	{
		Dictionary<string, (string BaseName, List<IGameObject> Instances, List<string> Components, List<string> Children)> clusters = new();

		foreach (IGameObject go in gameData.GameBundle.FetchAssets().OfType<IGameObject>())
		{
			if (!go.Collection.IsScene)
			{
				continue;
			}

			string baseName = InstanceSuffixRegex().Replace(go.Name.String, "").Trim();
			if (string.IsNullOrWhiteSpace(baseName))
			{
				baseName = "GameObject";
			}

			List<string> components = [];
			foreach (var comp in go.GetComponentAccessList())
			{
				if (comp is not null)
				{
					components.Add(comp.ClassName);
				}
			}
			components.Sort(StringComparer.Ordinal);

			List<string> children = [];
			if (go.TryGetComponent<ITransform>(out ITransform? tr))
			{
				foreach (ITransform? child in tr.Children_C4P)
				{
					if (child?.GameObject_C4P is { } childGo)
					{
						children.Add(InstanceSuffixRegex().Replace(childGo.Name.String, "").Trim());
					}
				}
			}
			children.Sort(StringComparer.Ordinal);

			string sig = $"{baseName}|{string.Join(",", components)}|{string.Join(",", children)}";
			if (!clusters.TryGetValue(sig, out var bucket))
			{
				bucket = (baseName, [], components, children);
				clusters[sig] = bucket;
			}
			bucket.Instances.Add(go);
		}

		HashSet<string> usedNames = new(StringComparer.OrdinalIgnoreCase);
		foreach (var (sig, bucket) in clusters)
		{
			// Promote clusters with >= 2 instances OR complex hierarchies with >= 3 components
			if (bucket.Instances.Count < 2 && bucket.Components.Count < 3)
			{
				continue;
			}

			string safeName = Regex.Replace(bucket.BaseName, @"[^A-Za-z0-9_\-]", "_");
			if (string.IsNullOrWhiteSpace(safeName)) safeName = "Prefab";
			string finalName = safeName;
			int suffix = 2;
			while (!usedNames.Add(finalName))
			{
				finalName = $"{safeName}_{suffix++}";
			}

			Vector3 scale = Vector3.One;
			if (bucket.Instances[0].TryGetComponent<ITransform>(out ITransform? t))
			{
				scale = t.LocalScale_C4.CastToStruct();
			}

			Blueprints.Add(new CoconutPrefabBlueprint(
				finalName,
				sig,
				bucket.Instances.Count,
				bucket.Components,
				bucket.Children,
				scale));
		}

		Logger.Info(LogCategory.Processing, $"[Coconut] Identified {Blueprints.Count} reusable Prefab blueprints across scenes.");
	}
}
