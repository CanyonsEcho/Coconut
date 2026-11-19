# Coconut

**Coconut** is a cross-platform desktop **Unity Mono & IL2CPP decompiler, scene/bundle editor, internal AssetRipper project recovery suite, and patched game rebuilder** written in **C++20** and **RmlUi 6.3**, designed around the workflows of **Ghidra** and the **Unity Editor**.

> **Desktop Host Platforms:** Windows, macOS, and Linux  
> **Supported Unity Game Builds:** Android (`.apk` / `.aab` / IL2CPP & Mono), Windows (`*_Data`), Linux (`*_Data`), and macOS (`.app`)

---

## Architecture & Launchable Utilities

Coconut opens into a **Ghidra-style Project Manager** where each project is a named mapping to a real Unity game build folder or Android APK, paired with a **Tool Chest** to launch five dedicated utilities:

| Utility | Style | Capabilities |
|---|---|---|
| **Project Manager** | Ghidra Main Window | Register named Unity game projects, inspect full build file trees (`*_Data`, `level*`, `Managed/*.dll`, `libil2cpp.so`, `global-metadata.dat`, `.bundle`s), auto-detect Unity version/platform/backend, and launch tools |
| **CodeBrowser** | Ghidra RE Suite | Program Trees, searchable Symbol Tree (`Classes`, `Methods`, `Fields`, `Strings`), tabular Address/Bytes/Instruction Listing (CIL, ARM64, x86-64), syntax-highlighted C# Decompiler, XRefs, `.coco` renames/annotations, `dump.cs`, and Ghidra `script.json` |
| **Scene Editor** | Unity Editor | Hierarchy tree, hardware-accelerated OpenGL 3.3 Core 3D Scene Viewport (orbit/pan/zoom, ray picking, Move/Rotate/Scale gizmos, Shaded/Wireframe/Normals, Grid), collapsible Component Inspector with colored X/Y/Z Transform controls, Undo/Redo, and Project Assets + Console |
| **Bundle & Asset Editor** | UnityFS Workshop | Inspect & repack UnityFS bundles (`LZ4HC`/`LZ4`/`None`), preview & edit Texture2D (live Flip/Invert/Grayscale/Tint, PNG replacement; export **PNG / TGA / DDS**), Meshes (**FBX 7.4 / glTF 2.0 / OBJ**), AudioClips (waveform + FSB5 detection; export **MP3 / M4A ALAC / WAV / OGG**), Materials, TextAssets, TypeTree, and aligned Hex Byte Editor + Data Type Inspector |
| **Project Exporter** | Internal AssetRipper Fork | Powered by Coconut's **internal fork of AssetRipper 1.3.14** (`internal/AssetRipper` + `internal/Coconut.RipperCore`) with native C++ fallback, **Target Unity Version dropdown** (`Match Source`, Unity `5.6` through `2022.3 LTS` and `Unity 6 / 6000.x`), format selectors (`FBX/glTF/OBJ`, `WAV/MP3/M4A/OGG`, `PNG/TGA/DDS`), **Static-Batch Mesh Decombining**, **Prefab Reconstruction**, and **Automated Script/Shader/Package Build Error Fixing** |
| **Game Exporter** | Patched Build Studio | Rebuilds the modified Unity game directly from inside Coconut — syncing Scene Editor transforms, patched Textures/Materials/Hex bytes, re-serializing modified `SerializedFile`s, repacking modified `UnityFS` bundles, and outputting a patched desktop game directory (Windows/Linux/macOS) or patched Android `.apk` |

---

## Internal AssetRipper Fork (`internal/AssetRipper` & `internal/Coconut.RipperCore`)

Rather than relying on an external unmodified AssetRipper executable, Coconut vendors and extends **AssetRipper 1.3.14** (`.NET 10`) internally under `internal/AssetRipper/` and `internal/Coconut.RipperCore/`:

- **`CoconutStaticMeshSeparationProcessor`**: Injected directly into AssetRipper's processing pipeline (before `LightingDataProcessor`) to split Unity `Combined Mesh (root scene)` static batches back into local-space per-object meshes.
- **`CoconutPrefabRecoveryProcessor`**: Identifies repeated scene hierarchies and emits reusable `.prefab` YAML assets + `.meta` files.
- **`CoconutMeshExporter` & `CoconutAudioExporter`**: Native pipeline exporters for **FBX 7.4 (`.fbx`)**, **glTF Binary (`.glb`)**, **OBJ (`.obj`)**, **WAV (`.wav`)**, and **M4A Lossless ALAC (`.m4a`)**.
- **`CoconutProjectFixupPostExporter`**: Retargets `ProjectSettings/ProjectVersion.txt`, upgrades `Packages/manifest.json` for the chosen target Unity version, strips compiler-generated attributes that break Roslyn compilation, migrates deprecated Unity APIs (e.g., `FindObjectOfType` -> `FindFirstObjectByType` on Unity 6, `Application.LoadLevel` -> `SceneManager.LoadScene`), and repairs `DummyShader` stubs into compiling Unity shaders.
- **Zero-Dependency Native C++ Fallback**: If `.NET 10` is not installed on the host machine, Coconut seamlessly executes its built-in C++20 exporter, mesh decombiner, prefab reconstructor, and build error fixer.

---

## Building From Source

### Prerequisites
- **C++20 Compiler**: Visual Studio 2022/2026 (MSVC), GCC 12+, or Clang 15+
- **CMake 3.24+** & **Ninja**
- **OpenGL 3.3+** development headers (`libgl1-mesa-dev` + X11/Wayland libs on Linux; built-in on Windows & macOS)
- **Optional (Recommended)**: [.NET 10 SDK](https://dotnet.microsoft.com/) to compile the internal `Coconut.RipperCore` AssetRipper fork

### Build Commands

```bash
# 1. Configure & build Coconut + unit/integration tests (Windows / Linux / macOS)
cmake --preset default
cmake --build --preset default

# 2. (Optional) Build the internal forked AssetRipper engine (.NET 10)
dotnet build internal/Coconut.RipperCore/Coconut.RipperCore.csproj -c Release

# 3. Run the automated test suite
ctest --preset default --output-on-failure

# 4. Launch Coconut
./build/coconut
```

### Linux System Packages (Ubuntu / Debian)
```bash
sudo apt-get install -y ninja-build pkg-config libgl1-mesa-dev libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libwayland-dev libxkbcommon-dev \
  libdbus-1-dev libudev-dev libasound2-dev
```

---

## CLI Options

```bash
coconut --open <path-to-unity-game-or-apk> --view <manager|codebrowser|scene|bundle|exporter|builder>
```

---

## Branching Model
- **`stable`** *(default)*: Known working, compiled, and test-verified releases.
- **`dev`**: Active incremental development and work-in-progress commits.

---

## Third-Party Libraries & Licenses
- **RmlUi 6.3** (MIT) — Desktop UI rendering & layout engine
- **SDL3** (zlib) — Cross-platform windowing & input
- **FreeType** (FTL) + **PT Sans** (SIL OFL) — Clean modern sans-serif typography
- **AssetRipper 1.3.14** (GPLv3 — see [internal/AssetRipper/LICENSE.md](internal/AssetRipper/LICENSE.md)) — Internal forked Unity asset extraction libraries
- **GLM**, **nlohmann/json**, **LZ4**, **miniz**, **stb**, **shine** (MPEG-1/2 Layer III encoder)
