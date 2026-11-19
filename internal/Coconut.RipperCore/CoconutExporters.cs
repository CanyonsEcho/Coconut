using AssetRipper.Assets;
using AssetRipper.Export.Configuration;
using AssetRipper.Export.Modules.Audio;
using AssetRipper.Export.Modules.Models;
using AssetRipper.Export.UnityProjects;
using AssetRipper.Export.UnityProjects.Audio;
using AssetRipper.Import.Logging;
using AssetRipper.IO.Files;
using AssetRipper.Processing;
using AssetRipper.SourceGenerated.Classes.ClassID_43;
using AssetRipper.SourceGenerated.Classes.ClassID_83;
using AssetRipper.SourceGenerated.Extensions;
using SharpGLTF.Scenes;
using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Coconut.RipperCore;

/// <summary>
/// Exports Unity IMesh assets as real FBX 7.4 ASCII (.fbx), glTF Binary (.glb), or Wavefront OBJ (.obj)
/// directly inside AssetRipper's ProjectExporter pipeline.
/// </summary>
public sealed class CoconutMeshExporter : BinaryAssetExporter
{
	private readonly string _format;
	public int ExportedCount { get; private set; }

	public CoconutMeshExporter(string format)
	{
		_format = format.ToLowerInvariant();
	}

	public override bool TryCreateCollection(IUnityObjectBase asset, [NotNullWhen(true)] out IExportCollection? exportCollection)
	{
		if (asset is IMesh mesh && mesh.IsSet())
		{
			exportCollection = new CoconutMeshExportCollection(this, mesh, _format);
			return true;
		}
		exportCollection = null;
		return false;
	}

	public override bool Export(IExportContainer container, IUnityObjectBase asset, string path, FileSystem fileSystem)
	{
		IMesh mesh = (IMesh)asset;
		if (!MeshData.TryMakeFromMesh(mesh, out MeshData data) || data.Vertices.Length == 0)
		{
			return false;
		}

		bool ok = _format switch
		{
			"fbx" => WriteFbxAscii(mesh.Name.String, data, path, fileSystem),
			"obj" => WriteObj(mesh.Name.String, data, path, fileSystem),
			"glb" => WriteGlb(mesh, path, fileSystem),
			_ => false,
		};

		if (ok) ExportedCount++;
		return ok;
	}

	private static bool WriteGlb(IMesh mesh, string path, FileSystem fileSystem)
	{
		SceneBuilder sceneBuilder = GlbMeshBuilder.Build(mesh);
		using Stream fileStream = fileSystem.File.Create(path);
		return GlbWriter.TryWrite(sceneBuilder, fileStream, out _);
	}

	private static bool WriteObj(string meshName, MeshData data, string path, FileSystem fileSystem)
	{
		StringBuilder sb = new(data.Vertices.Length * 48);
		sb.AppendLine($"# Exported by Coconut.RipperCore (Internal AssetRipper Fork)");
		sb.AppendLine($"o {(string.IsNullOrWhiteSpace(meshName) ? "Mesh" : meshName)}");
		foreach (Vector3 v in data.Vertices)
		{
			sb.AppendFormat(CultureInfo.InvariantCulture, "v {0:G7} {1:G7} {2:G7}\n", -v.X, v.Y, v.Z);
		}
		if (data.HasUV0)
		{
			foreach (Vector2 uv in data.UV0)
			{
				sb.AppendFormat(CultureInfo.InvariantCulture, "vt {0:G7} {1:G7}\n", uv.X, uv.Y);
			}
		}
		if (data.HasNormals)
		{
			foreach (Vector3 n in data.Normals)
			{
				sb.AppendFormat(CultureInfo.InvariantCulture, "vn {0:G7} {1:G7} {2:G7}\n", -n.X, n.Y, n.Z);
			}
		}

		for (int s = 0; s < data.SubMeshes.Length; s++)
		{
			SubMeshData sm = data.SubMeshes[s];
			sb.AppendLine($"g SubMesh_{s}");
			int end = Math.Min(sm.FirstIndex + sm.IndexCount, data.ProcessedIndexBuffer.Length);
			for (int i = sm.FirstIndex; i + 2 < end; i += 3)
			{
				uint a = data.ProcessedIndexBuffer[i] + sm.BaseVertex + 1;
				uint b = data.ProcessedIndexBuffer[i + 2] + sm.BaseVertex + 1;
				uint c = data.ProcessedIndexBuffer[i + 1] + sm.BaseVertex + 1;
				if (data.HasUV0 && data.HasNormals)
					sb.AppendLine($"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}");
				else if (data.HasNormals)
					sb.AppendLine($"f {a}//{a} {b}//{b} {c}//{c}");
				else if (data.HasUV0)
					sb.AppendLine($"f {a}/{a} {b}/{b} {c}/{c}");
				else
					sb.AppendLine($"f {a} {b} {c}");
			}
		}

		fileSystem.File.WriteAllText(path, sb.ToString());
		return true;
	}

	private static bool WriteFbxAscii(string meshName, MeshData data, string path, FileSystem fileSystem)
	{
		string safeName = string.IsNullOrWhiteSpace(meshName) ? "Mesh" : Regex.Replace(meshName, @"[^A-Za-z0-9_\-]", "_");
		List<int> polyIndices = [];
		foreach (SubMeshData sm in data.SubMeshes)
		{
			int end = Math.Min(sm.FirstIndex + sm.IndexCount, data.ProcessedIndexBuffer.Length);
			for (int i = sm.FirstIndex; i + 2 < end; i += 3)
			{
				int a = (int)(data.ProcessedIndexBuffer[i] + sm.BaseVertex);
				int b = (int)(data.ProcessedIndexBuffer[i + 1] + sm.BaseVertex);
				int c = (int)(data.ProcessedIndexBuffer[i + 2] + sm.BaseVertex);
				if (a < data.Vertices.Length && b < data.Vertices.Length && c < data.Vertices.Length)
				{
					polyIndices.Add(a);
					polyIndices.Add(c);
					polyIndices.Add(-(b + 1)); // FBX end-of-polygon bitwise NOT
				}
			}
		}
		if (polyIndices.Count == 0)
		{
			return false;
		}

		StringBuilder sb = new(data.Vertices.Length * 64 + polyIndices.Count * 12);
		sb.AppendLine("; FBX 7.4.0 project file");
		sb.AppendLine("; Exported by Coconut.RipperCore (Internal AssetRipper Fork)");
		sb.AppendLine("FBXHeaderExtension:  {");
		sb.AppendLine("\tFBXHeaderVersion: 1003");
		sb.AppendLine("\tFBXVersion: 7400");
		sb.AppendLine("\tCreator: \"Coconut Decompiler\"");
		sb.AppendLine("}");
		sb.AppendLine("GlobalSettings:  {");
		sb.AppendLine("\tVersion: 1000");
		sb.AppendLine("\tProperties70:  {");
		sb.AppendLine("\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1");
		sb.AppendLine("\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1");
		sb.AppendLine("\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2");
		sb.AppendLine("\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1");
		sb.AppendLine("\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0");
		sb.AppendLine("\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1");
		sb.AppendLine("\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100");
		sb.AppendLine("\t}");
		sb.AppendLine("}");
		sb.AppendLine("Objects:  {");
		sb.AppendLine($"\tGeometry: 100001, \"Geometry::{safeName}\", \"Mesh\" {{");
		sb.AppendLine($"\t\tVertices: *{data.Vertices.Length * 3} {{");
		sb.Append("\t\t\ta: ");
		for (int i = 0; i < data.Vertices.Length; i++)
		{
			if (i > 0) sb.Append(',');
			Vector3 v = data.Vertices[i];
			sb.AppendFormat(CultureInfo.InvariantCulture, "{0:G7},{1:G7},{2:G7}", -v.X, v.Y, v.Z);
		}
		sb.AppendLine();
		sb.AppendLine("\t\t}");
		sb.AppendLine($"\t\tPolygonVertexIndex: *{polyIndices.Count} {{");
		sb.Append("\t\t\ta: ");
		for (int i = 0; i < polyIndices.Count; i++)
		{
			if (i > 0) sb.Append(',');
			sb.Append(polyIndices[i]);
		}
		sb.AppendLine();
		sb.AppendLine("\t\t}");

		if (data.HasNormals)
		{
			sb.AppendLine("\t\tLayerElementNormal: 0 {");
			sb.AppendLine("\t\t\tVersion: 101");
			sb.AppendLine("\t\t\tName: \"\"");
			sb.AppendLine("\t\t\tMappingInformationType: \"ByVertice\"");
			sb.AppendLine("\t\t\tReferenceInformationType: \"Direct\"");
			sb.AppendLine($"\t\t\tNormals: *{data.Normals.Length * 3} {{");
			sb.Append("\t\t\t\ta: ");
			for (int i = 0; i < data.Normals.Length; i++)
			{
				if (i > 0) sb.Append(',');
				Vector3 n = data.Normals[i];
				sb.AppendFormat(CultureInfo.InvariantCulture, "{0:G7},{1:G7},{2:G7}", -n.X, n.Y, n.Z);
			}
			sb.AppendLine();
			sb.AppendLine("\t\t\t}");
			sb.AppendLine("\t\t}");
		}

		if (data.HasUV0)
		{
			sb.AppendLine("\t\tLayerElementUV: 0 {");
			sb.AppendLine("\t\t\tVersion: 101");
			sb.AppendLine("\t\t\tName: \"UVMap\"");
			sb.AppendLine("\t\t\tMappingInformationType: \"ByVertice\"");
			sb.AppendLine("\t\t\tReferenceInformationType: \"Direct\"");
			sb.AppendLine($"\t\t\tUV: *{data.UV0.Length * 2} {{");
			sb.Append("\t\t\t\ta: ");
			for (int i = 0; i < data.UV0.Length; i++)
			{
				if (i > 0) sb.Append(',');
				Vector2 uv = data.UV0[i];
				sb.AppendFormat(CultureInfo.InvariantCulture, "{0:G7},{1:G7}", uv.X, uv.Y);
			}
			sb.AppendLine();
			sb.AppendLine("\t\t\t}");
			sb.AppendLine("\t\t}");
		}

		sb.AppendLine("\t\tLayer: 0 {");
		sb.AppendLine("\t\t\tVersion: 100");
		if (data.HasNormals)
		{
			sb.AppendLine("\t\t\tLayerElement:  {");
			sb.AppendLine("\t\t\t\tType: \"LayerElementNormal\"");
			sb.AppendLine("\t\t\t\tTypedIndex: 0");
			sb.AppendLine("\t\t\t}");
		}
		if (data.HasUV0)
		{
			sb.AppendLine("\t\t\tLayerElement:  {");
			sb.AppendLine("\t\t\t\tType: \"LayerElementUV\"");
			sb.AppendLine("\t\t\t\tTypedIndex: 0");
			sb.AppendLine("\t\t\t}");
		}
		sb.AppendLine("\t\t}");
		sb.AppendLine("\t}");
		sb.AppendLine($"\tModel: 200001, \"Model::{safeName}\", \"Mesh\" {{");
		sb.AppendLine("\t\tVersion: 232");
		sb.AppendLine("\t\tShading: T");
		sb.AppendLine("\t\tCulling: \"CullingOff\"");
		sb.AppendLine("\t}");
		sb.AppendLine("}");
		sb.AppendLine("Connections:  {");
		sb.AppendLine("\tC: \"OO\",200001,0");
		sb.AppendLine("\tC: \"OO\",100001,200001");
		sb.AppendLine("}");

		fileSystem.File.WriteAllText(path, sb.ToString());
		return true;
	}

	private sealed class CoconutMeshExportCollection : AssetExportCollection<IMesh>
	{
		private readonly string _ext;
		public CoconutMeshExportCollection(IAssetExporter exporter, IMesh asset, string ext)
			: base(exporter, asset)
		{
			_ext = ext;
		}

		protected override string GetExportExtension(IUnityObjectBase asset) => _ext;
	}
}

/// <summary>
/// Exports AudioClips in WAV, M4A (ISO-BMFF ALAC), OGG, or Native format inside AssetRipper.
/// </summary>
public sealed class CoconutAudioExporter : BinaryAssetExporter
{
	private readonly string _format;
	public int ExportedCount { get; private set; }

	public CoconutAudioExporter(string format)
	{
		_format = format.ToLowerInvariant();
	}

	public override bool TryCreateCollection(IUnityObjectBase asset, [NotNullWhen(true)] out IExportCollection? exportCollection)
	{
		if (asset is IAudioClip audio &&
			AudioClipDecoder.TryDecode(audio, out byte[]? decoded, out string? ext, out _))
		{
			byte[] outBytes = decoded;
			string outExt = ext;

			if (_format == "wav" && ext == "ogg")
			{
				outBytes = AudioConverter.OggToWav(decoded);
				outExt = "wav";
			}
			else if (_format == "m4a")
			{
				byte[] wavBytes = ext == "ogg" ? AudioConverter.OggToWav(decoded) : decoded;
				if (TryEncodeWavToM4aAlac(wavBytes, out byte[] m4aBytes))
				{
					outBytes = m4aBytes;
					outExt = "m4a";
				}
			}

			ExportedCount++;
			exportCollection = new RawAudioCollection(this, audio, outBytes, outExt);
			return true;
		}

		exportCollection = null;
		return false;
	}

	/// <summary>
	/// Encodes 16-bit PCM WAV bytes into a standards-compliant ISO-BMFF M4A (Apple Lossless verbatim) container.
	/// </summary>
	public static bool TryEncodeWavToM4aAlac(byte[] wav, out byte[] m4a)
	{
		m4a = [];
		if (wav.Length < 44 || Encoding.ASCII.GetString(wav, 0, 4) != "RIFF")
		{
			return false;
		}

		int channels = 0;
		int sampleRate = 0;
		int bitsPerSample = 0;
		int dataOffset = -1;
		int dataLen = 0;

		int pos = 12;
		while (pos + 8 <= wav.Length)
		{
			string id = Encoding.ASCII.GetString(wav, pos, 4);
			int sz = BinaryPrimitives.ReadInt32LittleEndian(wav.AsSpan(pos + 4, 4));
			pos += 8;
			if (pos + sz > wav.Length) break;

			if (id == "fmt " && sz >= 16)
			{
				channels = BinaryPrimitives.ReadUInt16LittleEndian(wav.AsSpan(pos + 2, 2));
				sampleRate = BinaryPrimitives.ReadInt32LittleEndian(wav.AsSpan(pos + 4, 4));
				bitsPerSample = BinaryPrimitives.ReadUInt16LittleEndian(wav.AsSpan(pos + 14, 2));
			}
			else if (id == "data")
			{
				dataOffset = pos;
				dataLen = sz;
				break;
			}
			pos += sz + (sz & 1);
		}

		if (dataOffset < 0 || channels is < 1 or > 2 || sampleRate <= 0 || bitsPerSample != 16)
		{
			return false;
		}

		int frameBytes = channels * 2;
		int totalSamples = dataLen / frameBytes;
		if (totalSamples <= 0) return false;

		const int maxFrameSamples = 4096;
		List<byte[]> packets = [];
		List<uint> packetSampleCounts = [];

		int sampleCursor = 0;
		while (sampleCursor < totalSamples)
		{
			int count = Math.Min(maxFrameSamples, totalSamples - sampleCursor);
			byte[] pkt = EncodeAlacVerbatimPacket(wav, dataOffset + sampleCursor * frameBytes, count, channels);
			packets.Add(pkt);
			packetSampleCounts.Add((uint)count);
			sampleCursor += count;
		}

		// Build ISO-BMFF boxes: ftyp + moov + mdat
		byte[] ftyp = BuildBox("ftyp", bw =>
		{
			WriteFourCC(bw, "M4A ");
			WriteU32BE(bw, 0);
			WriteFourCC(bw, "M4A ");
			WriteFourCC(bw, "mp42");
			WriteFourCC(bw, "isom");
		});

		uint totalDuration = (uint)totalSamples;
		uint maxPacketSize = packets.Max(p => (uint)p.Length);
		uint avgBitrate = (uint)(sampleRate * channels * 16);

		byte[] alacConfig = BuildBox("alac", bw =>
		{
			WriteU32BE(bw, 0); // FullBox version+flags
			WriteU32BE(bw, (uint)maxFrameSamples);
			bw.Write((byte)0); // compatibleVersion
			bw.Write((byte)16); // bitDepth
			bw.Write((byte)40); // pb
			bw.Write((byte)10); // mb
			bw.Write((byte)14); // kb
			bw.Write((byte)channels);
			WriteU16BE(bw, 255); // maxRun
			WriteU32BE(bw, maxPacketSize);
			WriteU32BE(bw, avgBitrate);
			WriteU32BE(bw, (uint)sampleRate);
		});

		byte[] alacEntry = BuildBox("alac", bw =>
		{
			bw.Write(new byte[6]); // reserved
			WriteU16BE(bw, 1);     // data_reference_index
			bw.Write(new byte[8]); // reserved
			WriteU16BE(bw, (ushort)channels);
			WriteU16BE(bw, 16);    // sampleSize
			WriteU32BE(bw, 0);     // pre_defined + reserved
			WriteU32BE(bw, (uint)(sampleRate << 16));
			bw.Write(alacConfig);
		});

		byte[] stsd = BuildBox("stsd", bw =>
		{
			WriteU32BE(bw, 0);
			WriteU32BE(bw, 1);
			bw.Write(alacEntry);
		});

		byte[] stts = BuildBox("stts", bw =>
		{
			WriteU32BE(bw, 0);
			List<(uint count, uint delta)> runs = [];
			foreach (uint d in packetSampleCounts)
			{
				if (runs.Count > 0 && runs[^1].delta == d)
					runs[^1] = (runs[^1].count + 1, d);
				else
					runs.Add((1, d));
			}
			WriteU32BE(bw, (uint)runs.Count);
			foreach (var (c, d) in runs)
			{
				WriteU32BE(bw, c);
				WriteU32BE(bw, d);
			}
		});

		byte[] stsc = BuildBox("stsc", bw =>
		{
			WriteU32BE(bw, 0);
			WriteU32BE(bw, 1);
			WriteU32BE(bw, 1);
			WriteU32BE(bw, (uint)packets.Count);
			WriteU32BE(bw, 1);
		});

		byte[] stsz = BuildBox("stsz", bw =>
		{
			WriteU32BE(bw, 0);
			WriteU32BE(bw, 0);
			WriteU32BE(bw, (uint)packets.Count);
			foreach (byte[] p in packets) WriteU32BE(bw, (uint)p.Length);
		});

		byte[] BuildMoov(uint mdatPayloadOffset)
		{
			byte[] stco = BuildBox("stco", bw =>
			{
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 1);
				WriteU32BE(bw, mdatPayloadOffset);
			});

			byte[] stbl = BuildBox("stbl", bw =>
			{
				bw.Write(stsd);
				bw.Write(stts);
				bw.Write(stsc);
				bw.Write(stsz);
				bw.Write(stco);
			});

			byte[] dref = BuildBox("dref", bw =>
			{
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 1);
				bw.Write(BuildBox("url ", u => WriteU32BE(u, 1)));
			});
			byte[] dinf = BuildBox("dinf", bw => bw.Write(dref));
			byte[] smhd = BuildBox("smhd", bw => { WriteU32BE(bw, 0); WriteU32BE(bw, 0); });
			byte[] minf = BuildBox("minf", bw => { bw.Write(smhd); bw.Write(dinf); bw.Write(stbl); });

			byte[] mdhd = BuildBox("mdhd", bw =>
			{
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
				WriteU32BE(bw, (uint)sampleRate);
				WriteU32BE(bw, totalDuration);
				WriteU16BE(bw, 0x55C4);
				WriteU16BE(bw, 0);
			});

			byte[] hdlr = BuildBox("hdlr", bw =>
			{
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
				WriteFourCC(bw, "soun");
				bw.Write(new byte[12]);
				bw.Write(Encoding.ASCII.GetBytes("SoundHandler\0"));
			});

			byte[] mdia = BuildBox("mdia", bw => { bw.Write(mdhd); bw.Write(hdlr); bw.Write(minf); });

			byte[] tkhd = BuildBox("tkhd", bw =>
			{
				WriteU32BE(bw, 3); // enabled | in_movie
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 1); // track_ID
				WriteU32BE(bw, 0);
				WriteU32BE(bw, totalDuration);
				bw.Write(new byte[8]);
				WriteU16BE(bw, 0);
				WriteU16BE(bw, 0);
				WriteU16BE(bw, 0x0100); // volume 1.0
				WriteU16BE(bw, 0);
				WriteIdentityMatrix(bw);
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
			});

			byte[] trak = BuildBox("trak", bw => { bw.Write(tkhd); bw.Write(mdia); });

			byte[] mvhd = BuildBox("mvhd", bw =>
			{
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
				WriteU32BE(bw, 0);
				WriteU32BE(bw, (uint)sampleRate);
				WriteU32BE(bw, totalDuration);
				WriteU32BE(bw, 0x00010000); // rate 1.0
				WriteU16BE(bw, 0x0100);     // volume 1.0
				bw.Write(new byte[10]);
				WriteIdentityMatrix(bw);
				bw.Write(new byte[24]);
				WriteU32BE(bw, 2); // next_track_ID
			});

			return BuildBox("moov", bw => { bw.Write(mvhd); bw.Write(trak); });
		}

		// Two-pass layout so stco offset points directly to mdat payload
		byte[] dummyMoov = BuildMoov(0);
		uint mdatDataOffset = (uint)(ftyp.Length + dummyMoov.Length + 8);
		byte[] moov = BuildMoov(mdatDataOffset);

		int mdatPayloadSize = packets.Sum(p => p.Length);
		using MemoryStream ms = new(ftyp.Length + moov.Length + 8 + mdatPayloadSize);
		using BinaryWriter outBw = new(ms);
		outBw.Write(ftyp);
		outBw.Write(moov);
		WriteU32BE(outBw, (uint)(8 + mdatPayloadSize));
		WriteFourCC(outBw, "mdat");
		foreach (byte[] p in packets) outBw.Write(p);

		m4a = ms.ToArray();
		return true;
	}

	private static byte[] EncodeAlacVerbatimPacket(byte[] wav, int offset, int sampleCount, int channels)
	{
		// Bitstream writer (MSB first)
		List<byte> bytes = [];
		int curByte = 0;
		int bitPos = 0;
		void WriteBits(uint value, int bits)
		{
			for (int i = bits - 1; i >= 0; i--)
			{
				curByte = (curByte << 1) | (int)((value >> i) & 1u);
				bitPos++;
				if (bitPos == 8)
				{
					bytes.Add((byte)curByte);
					curByte = 0;
					bitPos = 0;
				}
			}
		}

		// Element type: ID_SCE = 0 (mono), ID_CPE = 1 (stereo)
		WriteBits(channels == 1 ? 0u : 1u, 3);
		WriteBits(0, 4);  // element instance tag
		WriteBits(0, 12); // unused header bits
		bool hasSize = sampleCount != 4096;
		WriteBits(hasSize ? 1u : 0u, 1); // hasSize
		WriteBits(0, 2);                 // unusedBytes
		WriteBits(1, 1);                 // isNotCompressed = 1 (verbatim PCM)
		if (hasSize)
		{
			WriteBits((uint)sampleCount, 32);
		}

		for (int i = 0; i < sampleCount; i++)
		{
			for (int c = 0; c < channels; c++)
			{
				int idx = offset + (i * channels + c) * 2;
				short sample = BinaryPrimitives.ReadInt16LittleEndian(wav.AsSpan(idx, 2));
				WriteBits((ushort)sample, 16);
			}
		}

		// ID_END = 7
		WriteBits(7, 3);
		if (bitPos > 0)
		{
			curByte <<= (8 - bitPos);
			bytes.Add((byte)curByte);
		}
		return bytes.ToArray();
	}

	private static byte[] BuildBox(string fourCC, Action<BinaryWriter> writePayload)
	{
		using MemoryStream payload = new();
		using BinaryWriter pbw = new(payload);
		writePayload(pbw);
		pbw.Flush();
		byte[] body = payload.ToArray();

		using MemoryStream box = new(8 + body.Length);
		using BinaryWriter bw = new(box);
		WriteU32BE(bw, (uint)(8 + body.Length));
		WriteFourCC(bw, fourCC);
		bw.Write(body);
		return box.ToArray();
	}

	private static void WriteFourCC(BinaryWriter bw, string s) => bw.Write(Encoding.ASCII.GetBytes(s));
	private static void WriteU32BE(BinaryWriter bw, uint v)
	{
		Span<byte> b = stackalloc byte[4];
		BinaryPrimitives.WriteUInt32BigEndian(b, v);
		bw.Write(b);
	}
	private static void WriteU16BE(BinaryWriter bw, ushort v)
	{
		Span<byte> b = stackalloc byte[2];
		BinaryPrimitives.WriteUInt16BigEndian(b, v);
		bw.Write(b);
	}
	private static void WriteIdentityMatrix(BinaryWriter bw)
	{
		uint[] m = [0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000];
		foreach (uint v in m) WriteU32BE(bw, v);
	}

	private sealed class RawAudioCollection : AudioExportCollection
	{
		private readonly byte[] _data;
		private readonly string _ext;
		public RawAudioCollection(IAssetExporter exporter, IAudioClip asset, byte[] data, string ext)
			: base(exporter, asset)
		{
			_data = data;
			_ext = ext;
		}

		protected override bool ExportInner(IExportContainer container, string filePath, string dirPath, FileSystem fileSystem)
		{
			fileSystem.File.WriteAllBytes(filePath, _data);
			return true;
		}

		protected override string GetExportExtension(IUnityObjectBase asset) => _ext;
	}
}

/// <summary>
/// Post-exporter that applies Coconut's target Unity version retargeting,
/// Packages/manifest.json upgrades, C# compile error fixes, shader fallbacks,
/// and reconstructed .prefab writing on top of the exported project.
/// </summary>
public sealed class CoconutProjectFixupPostExporter : IPostExporter
{
	private readonly CoconutExportOptions _options;
	private readonly CoconutPrefabRecoveryProcessor? _prefabProcessor;
	private readonly CoconutExportReport _report;

	public CoconutProjectFixupPostExporter(
		CoconutExportOptions options,
		CoconutPrefabRecoveryProcessor? prefabProcessor,
		CoconutExportReport report)
	{
		_options = options;
		_prefabProcessor = prefabProcessor;
		_report = report;
	}

	public void DoPostExport(GameData gameData, FullConfiguration settings, FileSystem fileSystem)
	{
		string projectRoot = settings.ProjectRootPath;
		string assetsRoot = settings.AssetsPath;
		string settingsRoot = settings.ProjectSettingsPath;

		// 1. Ensure ProjectVersion.txt matches target Unity version
		string targetVerStr = settings.Version.ToString();
		if (!string.IsNullOrWhiteSpace(_options.TargetUnityVersion) &&
			!_options.TargetUnityVersion.Equals("Match Source", StringComparison.OrdinalIgnoreCase))
		{
			targetVerStr = _options.TargetUnityVersion;
			Directory.CreateDirectory(settingsRoot);
			File.WriteAllText(
				Path.Combine(settingsRoot, "ProjectVersion.txt"),
				$"m_EditorVersion: {targetVerStr}\nm_EditorVersionWithRevision: {targetVerStr} (coconut00001)\n");
			_report.Messages.Add($"Retargeted ProjectVersion.txt -> {targetVerStr}");
		}

		_report.TargetUnityVersion = targetVerStr;

		// 2. Write reconstructed reusable .prefab templates
		if (_options.ReconstructPrefabs && _prefabProcessor is not null && _prefabProcessor.Blueprints.Count > 0)
		{
			string prefabsDir = Path.Combine(assetsRoot, "Prefabs", "Reconstructed");
			Directory.CreateDirectory(prefabsDir);
			foreach (CoconutPrefabBlueprint bp in _prefabProcessor.Blueprints)
			{
				string prefabPath = Path.Combine(prefabsDir, $"{bp.PrefabName}.prefab");
				WritePrefabYaml(prefabPath, bp);
				WriteSimpleMeta(prefabPath + ".meta", ComputeDeterministicGuid("prefab:" + bp.PrefabName));
				_report.PrefabsReconstructed++;
			}
			_report.Messages.Add($"Reconstructed {_report.PrefabsReconstructed} reusable .prefab assets in Assets/Prefabs/Reconstructed/");
		}

		// 3. Automated build error fixing (C# scripts, shaders, Packages/manifest.json)
		if (_options.FixBuildErrors)
		{
			FixScriptsAndShaders(projectRoot, assetsRoot, targetVerStr);
			UpgradePackagesManifest(projectRoot, assetsRoot, targetVerStr);
		}

		// 4. Tally exported files for the report
		if (Directory.Exists(assetsRoot))
		{
			foreach (string file in Directory.EnumerateFiles(assetsRoot, "*", SearchOption.AllDirectories))
			{
				string ext = Path.GetExtension(file).ToLowerInvariant();
				switch (ext)
				{
					case ".unity": _report.ScenesExported++; break;
					case ".cs": _report.ScriptsExported++; break;
					case ".fbx":
					case ".glb":
					case ".obj":
					case ".mesh": _report.MeshesExported++; break;
					case ".wav":
					case ".ogg":
					case ".m4a":
					case ".mp3": _report.AudioExported++; break;
					case ".png":
					case ".tga":
					case ".bmp":
					case ".jpg":
					case ".exr": _report.TexturesExported++; break;
				}
			}
		}

		// Write fixup report into project root
		File.WriteAllText(
			Path.Combine(projectRoot, "COCONUT_FIXUP_REPORT.json"),
			JsonSerializer.Serialize(_report, new JsonSerializerOptions { WriteIndented = true }));
	}

	private void FixScriptsAndShaders(string projectRoot, string assetsRoot, string targetVersion)
	{
		if (!Directory.Exists(assetsRoot)) return;
		bool isUnity6 = targetVersion.StartsWith("6000.", StringComparison.Ordinal);
		bool isUnity2020Plus = isUnity6 || (int.TryParse(targetVersion.Split('.')[0], out int yr) && yr >= 2020);

		int assemblyInfoSeen = 0;
		foreach (string csFile in Directory.EnumerateFiles(assetsRoot, "*.cs", SearchOption.AllDirectories).ToList())
		{
			string fileName = Path.GetFileName(csFile);
			if (fileName.Equals("AssemblyInfo.cs", StringComparison.OrdinalIgnoreCase))
			{
				assemblyInfoSeen++;
				if (assemblyInfoSeen > 1)
				{
					File.Delete(csFile);
					string meta = csFile + ".meta";
					if (File.Exists(meta)) File.Delete(meta);
					_report.BuildErrorsFixed++;
					continue;
				}
			}

			string original = File.ReadAllText(csFile);
			string updated = original;

			// Strip invalid decompiler / compiler-generated attributes that break Unity's Roslyn compile
			updated = Regex.Replace(updated, @"^\s*\[assembly:\s*AssemblyVersion\([^\)]*\)\]\s*$", "", RegexOptions.Multiline);
			updated = Regex.Replace(updated, @"\[(System\.Runtime\.CompilerServices\.)?(Nullable|NullableContext|IsReadOnly|NativeCppClass)\([^\]]*\)\]", "");
			updated = Regex.Replace(updated, @"\[(System\.Runtime\.CompilerServices\.)?(IsReadOnly|NativeCppClass)\]", "");

			if (isUnity2020Plus)
			{
				if (updated.Contains("Application.LoadLevel(", StringComparison.Ordinal))
				{
					updated = updated.Replace("Application.LoadLevel(", "UnityEngine.SceneManagement.SceneManager.LoadScene(");
				}
				if (updated.Contains("new WWW(", StringComparison.Ordinal))
				{
					updated = updated.Replace("new WWW(", "UnityEngine.Networking.UnityWebRequest.Get(");
				}
			}

			if (isUnity6)
			{
				updated = Regex.Replace(updated, @"\bFindObjectOfType<([^>]+)>\(\)", "FindFirstObjectByType<$1>()");
				updated = Regex.Replace(updated, @"\bFindObjectsOfType<([^>]+)>\(\)", "FindObjectsByType<$1>(UnityEngine.FindObjectsSortMode.None)");
			}

			if (!string.Equals(original, updated, StringComparison.Ordinal))
			{
				File.WriteAllText(csFile, updated);
				_report.BuildErrorsFixed++;
			}
		}

		// Replace broken / empty AssetRipper DummyShader stubs with compiling Standard/Unlit fallbacks
		foreach (string shaderFile in Directory.EnumerateFiles(assetsRoot, "*.shader", SearchOption.AllDirectories))
		{
			string src = File.ReadAllText(shaderFile);
			if (src.Contains("DummyShader", StringComparison.Ordinal) || src.Contains("//DummyShader", StringComparison.Ordinal))
			{
				Match m = Regex.Match(src, @"Shader\s+""([^""]+)""");
				string shaderName = m.Success ? m.Groups[1].Value : Path.GetFileNameWithoutExtension(shaderFile);
				string cleanShader = $$"""
Shader "{{shaderName}}"
{
    Properties
    {
        _Color ("Color", Color) = (1,1,1,1)
        _MainTex ("Albedo (RGB)", 2D) = "white" {}
        _Glossiness ("Smoothness", Range(0,1)) = 0.5
        _Metallic ("Metallic", Range(0,1)) = 0.0
    }
    SubShader
    {
        Tags { "RenderType"="Opaque" }
        LOD 200
        CGPROGRAM
        #pragma surface surf Standard fullforwardshadows
        #pragma target 3.0
        sampler2D _MainTex;
        struct Input { float2 uv_MainTex; };
        half _Glossiness;
        half _Metallic;
        fixed4 _Color;
        void surf (Input IN, inout SurfaceOutputStandard o)
        {
            fixed4 c = tex2D(_MainTex, IN.uv_MainTex) * _Color;
            o.Albedo = c.rgb;
            o.Metallic = _Metallic;
            o.Smoothness = _Glossiness;
            o.Alpha = c.a;
        }
        ENDCG
    }
    FallBack "Diffuse"
}
""";
				File.WriteAllText(shaderFile, cleanShader);
				_report.ShadersFixed++;
				_report.BuildErrorsFixed++;
			}
		}
	}

	private void UpgradePackagesManifest(string projectRoot, string assetsRoot, string targetVersion)
	{
		string packagesDir = Path.Combine(projectRoot, "Packages");
		Directory.CreateDirectory(packagesDir);
		string manifestPath = Path.Combine(packagesDir, "manifest.json");

		Dictionary<string, string> deps = new();
		if (File.Exists(manifestPath))
		{
			try
			{
				using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(manifestPath));
				if (doc.RootElement.TryGetProperty("dependencies", out JsonElement el))
				{
					foreach (JsonProperty p in el.EnumerateObject())
					{
						if (p.Value.ValueKind == JsonValueKind.String)
							deps[p.Name] = p.Value.GetString()!;
					}
				}
			}
			catch { /* rewrite fresh */ }
		}

		bool isUnity6 = targetVersion.StartsWith("6000.", StringComparison.Ordinal);
		bool is2022 = targetVersion.StartsWith("2022.", StringComparison.Ordinal);
		bool is2021 = targetVersion.StartsWith("2021.", StringComparison.Ordinal);

		deps.TryAdd("com.unity.ugui", isUnity6 ? "2.0.0" : "1.0.0");
		if (!isUnity6)
		{
			deps.TryAdd("com.unity.textmeshpro", is2022 ? "3.0.6" : is2021 ? "3.0.6" : "2.1.6");
		}
		deps.TryAdd("com.unity.timeline", isUnity6 ? "1.8.7" : "1.7.6");

		// Detect InputSystem / URP usage from scripts
		bool usesInputSystem = false;
		bool usesUrp = false;
		if (Directory.Exists(assetsRoot))
		{
			foreach (string cs in Directory.EnumerateFiles(assetsRoot, "*.cs", SearchOption.AllDirectories).Take(200))
			{
				string txt = File.ReadAllText(cs);
				if (txt.Contains("UnityEngine.InputSystem", StringComparison.Ordinal)) usesInputSystem = true;
				if (txt.Contains("UnityEngine.Rendering.Universal", StringComparison.Ordinal)) usesUrp = true;
			}
		}

		if (usesInputSystem)
			deps.TryAdd("com.unity.inputsystem", isUnity6 ? "1.11.2" : "1.7.0");
		if (usesUrp)
			deps.TryAdd("com.unity.render-pipelines.universal", isUnity6 ? "17.0.3" : is2022 ? "14.0.11" : "12.1.15");

		string json = JsonSerializer.Serialize(new { dependencies = deps }, new JsonSerializerOptions { WriteIndented = true });
		File.WriteAllText(manifestPath, json);
		_report.BuildErrorsFixed++;
	}

	private static void WritePrefabYaml(string path, CoconutPrefabBlueprint bp)
	{
		StringBuilder sb = new();
		sb.AppendLine("%YAML 1.1");
		sb.AppendLine("%TAG !u! tag:unity3d.com,2011:");
		sb.AppendLine("--- !u!1 &100000");
		sb.AppendLine("GameObject:");
		sb.AppendLine("  m_ObjectHideFlags: 0");
		sb.AppendLine("  serializedVersion: 6");
		sb.AppendLine("  m_Component:");
		sb.AppendLine("  - component: {fileID: 400000}");
		sb.AppendLine($"  m_Name: {bp.PrefabName}");
		sb.AppendLine("  m_TagString: Untagged");
		sb.AppendLine("  m_IsActive: 1");
		sb.AppendLine("--- !u!4 &400000");
		sb.AppendLine("Transform:");
		sb.AppendLine("  m_ObjectHideFlags: 0");
		sb.AppendLine("  m_GameObject: {fileID: 100000}");
		sb.AppendLine("  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}");
		sb.AppendLine("  m_LocalPosition: {x: 0, y: 0, z: 0}");
		sb.AppendFormat(CultureInfo.InvariantCulture, "  m_LocalScale: {{x: {0:G6}, y: {1:G6}, z: {2:G6}}}\n",
			bp.DefaultScale.X, bp.DefaultScale.Y, bp.DefaultScale.Z);
		sb.AppendLine("  m_Children: []");
		sb.AppendLine("  m_Father: {fileID: 0}");
		File.WriteAllText(path, sb.ToString());
	}

	private static void WriteSimpleMeta(string metaPath, string guid)
	{
		File.WriteAllText(metaPath,
			$"fileFormatVersion: 2\nguid: {guid}\nPrefabImporter:\n  externalObjects: {{}}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n");
	}

	private static string ComputeDeterministicGuid(string key)
	{
		byte[] hash = MD5.HashData(Encoding.UTF8.GetBytes(key));
		return Convert.ToHexString(hash).ToLowerInvariant();
	}
}
