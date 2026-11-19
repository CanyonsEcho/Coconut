# Coconut

**Coconut** is a cross-platform **Unity Mono & IL2CPP game decompiler, 3D scene viewer/editor, asset editor, and automated Unity project recovery suite** built in **C++20** and **RmlUi**, combining a **Ghidra-style** reverse-engineering workbench with a **Unity Editor** workflow for built games.

> **Branch Policy**
> - **`stable` (default branch):** Verified, cleanly compiling, test-passing builds ready to clone and compile immediately.
> - **`dev`:** Active incremental development commits and experimental work-in-progress features.

---

## Features

### 1. Unity Binary & Container Parsing (Cross-Platform)
- **UnityFS AssetBundles** (`v6` / `v7` / `v8`): Uncompressed, **LZ4**, **LZ4HC**, and self-contained **LZMA1** decompression, plus UnityFS re-serialization.
- **Unity SerializedFiles** (`v9` – `v23`): Full header, metadata, external references, `.resS` / `.resource` stream loading, recursive **TypeTree** deserialization, and **live binary re-serialization & patching** back to disk.
- **Multi-Platform Game Auto-Detection**:
  - **Windows** (`*_Data/`, `Managed/*.dll`, `GameAssembly.dll`)
  - **Linux** (`*_Data/`, `GameAssembly.so` / `libil2cpp.so`)
  - **macOS** (`.app/Contents/Resources/Data/`, `GameAssembly.dylib`)
  - **Android** (native ZIP/APK reader for `.apk` / `.aab` / `.xapk`, `assets/bin/Data/`, `lib/arm64-v8a/libil2cpp.so`, `global-metadata.dat`)

### 2. Ghidra-Style Mono & IL2CPP Script Decompiler
- **Native ECMA-335 Mono PE/CLI Decompiler**: Parses PE32/PE64, COR20 CLI headers, `BSJB` metadata roots, `#~`/`#-` tables, `#Strings`, `#US`, `#Blob`, and `#GUID` heaps; disassembles CIL bytecode and reconstructs structured C# source code (`if`/`else`, fields, arithmetic, calls, returns).
- **Native IL2CPP Analyzer**: Parses `global-metadata.dat` (`0xFAB11BAF`, v20–v31+) and pairs it with **PE64**, **ELF64**, and **Mach-O 64** binary parsers + built-in **ARM64 (AArch64)** and **x86-64** disassemblers.
- **Synchronized Dual-Pane Code Browser**: Side-by-side Disassembly Listing (Offsets/Addresses | Raw Hex Bytes | Mnemonics | Comments) and syntax-highlighted C# view.
- **Cross-References (XRefs)**: Indexes `CALL`, `READ`, `WRITE`, and `STRING` references across assemblies and metadata.
- **Persistent `.coco` Project Database**: Ghidra-style symbol renaming, inline comments, bookmarks, and script overrides, plus **`dump.cs`** and **Ghidra `script.json`** export.

### 3. Interactive 3D Scene Viewer & Editor
- **Hardware-Accelerated OpenGL 3.3 Core / OpenGL ES 3.0 Viewport**: Renders Unity scene hierarchies directly inside RmlUi with lighting, floor grid, Shaded / Wireframe / Normals shading modes, and selection rim highlight.
- **3D Picking & Transform Gizmos**: Ray-AABB object selection and interactive X/Y/Z axis Translate, Rotate, and Scale gizmos.
- **Full Scene Editing + Undo/Redo**: Edit GameObject names, active flags, and Transforms live; Create, Duplicate, Reparent, and Delete GameObjects; sync edits directly back into Unity `SerializedFile` binaries or export clean `.unity` YAML scenes.

### 4. Asset Viewer, Editor & Ghidra-Style Hex Editor
- **Texture2D**: Native decoding for `RGBA32`, `RGB24`, `ARGB32`, `BGRA32`, `Alpha8`, `R8`, `R16`, `RGB565`, `RGBA4444`, `DXT1/BC1`, `DXT5/BC3`, `BC4`, `BC5`, `ETC1`, `ETC2`, and `ASTC` preview; live edits (Invert, Grayscale, Tint, Flip), PNG export/import, and binary write-back.
- **Mesh**: Unity `VertexData` + submesh + index buffer decoding, geometry bounds diagnostics, and **Wavefront `.OBJ`** & **glTF 2.0 (`.gltf`)** export.
- **AudioClip**: Metadata inspection, live amplitude waveform visualization, and **`.WAV`** export.
- **Material & TextAsset**: Live shader property / color editing with binary write-back and YAML/TypeTree inspection.
- **Hex Editor**: Live raw asset byte inspection (`Offset | Hex | ASCII`) and single-byte binary patching with dirty tracking.

### 5. AssetRipper Integration & Automated Unity Project Fixup Pipeline
Coconut integrates with **[AssetRipper](https://github.com/assetripper/assetripper)** (via its `--headless` HTTP API and the bundled `.NET 10` **`Coconut.RipperBridge`** CLI companion) while also shipping a **zero-dependency native C++ Unity Project Exporter** fallback for any Unity version (`5.6` through `2022.3 LTS` and **Unity 6 / `6000.x`**).

After exporting, Coconut runs a 3-pass automated recovery & fixup pipeline and generates `COCONUT_FIXUP_REPORT.md`:

1. **Static-Batch Mesh Decombiner (`MeshDecombiner`)**: Detects `Combined Mesh (root: scene)` and `m_StaticBatchInfo` renderers, splits submeshes / geometric connected-component islands, transforms vertices back into each GameObject's local coordinate space, writes clean `.obj` + `.meta` assets under `Assets/Meshes/Decombined/`, and rewires `.unity` scene `m_Mesh` GUIDs.
2. **Prefab Reconstructor (`PrefabReconstructor`)**: Computes structural hierarchy signatures across scene GameObjects, clusters repeated trees (props, crates, enemies, widgets), writes clean `.prefab` YAML + `.meta` files under `Assets/Prefabs/Reconstructed/`, and emits Unity `PrefabInstance` (`!u!1001`) modification overrides.
3. **Automated Build Error Fixer (`BuildErrorFixer`)**:
   - Strips duplicate compiler-generated stubs (`EmbeddedAttribute`, `NullableAttribute`, `IsReadOnlyAttribute`, `RefSafetyRulesAttribute`, `<PrivateImplementationDetails>`, duplicate `AssemblyInfo` attributes).
   - Sanitizes invalid decompiled identifiers (e.g. `<Prop>k__BackingField`).
   - Applies Unity version API migrations (`Application.LoadLevel` -> `SceneManager.LoadScene`, Unity 6 `FindObjectOfType` -> `FindFirstObjectByType`, `WWW` warning pragmas).
   - Auto-generates `Assets/csc.rsp` (`-unsafe`) when pointer/unsafe code is detected.
   - Scans script `using` directives and populates missing Unity UPM packages in `Packages/manifest.json` (`TextMeshPro`, `InputSystem`, `URP`, `HDRP`, `Newtonsoft.Json`, `Burst`, `Mathematics`).
   - Repairs placeholder/error shaders (`Hidden/InternalErrorShader`) and generates missing `.meta` files.

---

## Architecture

```
Coconut/
├── src/
│   ├── core/        # BinaryReader/Writer, LZ4/LZMA1/Zlib/ZIP-APK, Logger, Undo/Redo, .coco DB
│   ├── platform/    # Win32, Linux, macOS (Cocoa/ObjC++), Android NDK platform backends
│   ├── unity/       # UnityFS, TypeTree DOM, SerializedFile (v9-v23), GameLoader, Demo Generator
│   ├── assets/      # Texture2D/BC/ETC codecs, Mesh/OBJ/glTF, Audio/WAV, Material, AssetManager
│   ├── decompiler/  # Mono PE/CLI parser, CIL->C# decompiler, IL2CPP metadata, ARM64/x86-64 disasm
│   ├── scene/       # SceneDocument, OrbitCamera & GizmoMath, OpenGL 3.3 / GLES 3.0 SceneRenderer
│   ├── export/      # AssetRipper HTTP/CLI client + Native multi-version UnityProjectExporter
│   ├── fixup/       # MeshDecombiner, PrefabReconstructor, BuildErrorFixer, ProjectFixupPipeline
│   └── ui/          # SDL3+GL3/GLES3 RmlUi backend, SyntaxHighlighter, WorkbenchApp
├── resources/       # Rounded dark-gray purple-accent RmlUi RML/RCSS + OFL clean sans-serif fonts
├── tools/
│   └── Coconut.RipperBridge/  # .NET 10 headless AssetRipper automation bridge
├── platforms/
│   └── android/               # Gradle + NDK CMake + SDL3 Activity Android project
└── tests/                     # Headless CTest unit & end-to-end integration suite
```

All C++ dependencies (**SDL3**, **RmlUi 6.3**, **FreeType**, **GLM**, **LZ4**, **miniz**, **nlohmann/json**, **stb**) are automatically fetched at pinned release tags via CMake `FetchContent`.

---

## Building From Source

### Prerequisites
- **CMake 3.24+** & **Ninja** (or Visual Studio / Xcode / Make)
- **C++20 compiler** (MSVC 19.3+, GCC 11+, Clang 15+, or AppleClang 15+)
- **Git** (for CMake `FetchContent`)
- *(Optional)* **.NET 10 SDK** for `tools/Coconut.RipperBridge`

### Windows (MSVC + Ninja)
```powershell
# From a "x64 Native Tools Command Prompt for VS" or Developer PowerShell:
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure

# Run Coconut (launches directly with the built-in interactive demo Unity game!)
.\build\coconut.exe
```

### Linux (Ubuntu / Debian / Arch / Fedora)
```bash
# Install X11/Wayland/OpenGL development packages (Ubuntu/Debian example):
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libwayland-dev libxkbcommon-dev libdbus-1-dev \
  libudev-dev libasound2-dev

cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
./build/coconut
```

### macOS (Apple Silicon & Intel)
```bash
brew install cmake ninja
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
open build/coconut.app
```

### Android (NDK + Gradle, OpenGL ES 3.0)
You can build the Android app directly via Gradle in `platforms/android/` or cross-compile the native `libcoconut.so` library with the Android NDK:
```bash
cmake -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DANDROID_STL=c++_shared \
  -DCOCONUT_BUILD_TESTS=OFF \
  -DCOCONUT_BUILD_GUI=ON

cmake --build build-android --parallel
```

### Building the Optional AssetRipper Companion Bridge (.NET 10)
```bash
dotnet build tools/Coconut.RipperBridge/Coconut.RipperBridge.csproj -c Release
```

---

## Using AssetRipper With Coconut

1. Download or build **[AssetRipper](https://github.com/AssetRipper/AssetRipper)** (`AssetRipper.GUI.Free`).
2. Start AssetRipper in headless mode on port `52890` (or set `ASSETRIPPER_PATH`):
   ```bash
   AssetRipper.GUI.Free --headless --port 52890
   ```
3. In Coconut, open the **AssetRipper & Fixup** tab, pick your target Unity version (`2019.4 LTS`, `2021.3 LTS`, `2022.3 LTS`, `Unity 6`, or any custom version string), toggle the automated fixup passes, and click **Decompile, Fix Up & Export Unity Project**.
4. If AssetRipper is not running locally, Coconut seamlessly uses its built-in native C++ exporter and still runs the complete **MeshDecombiner**, **PrefabReconstructor**, and **BuildErrorFixer** pipeline.

---

## Viewport & Workbench Controls

| Control | Action |
|---|---|
| **Left Click (Viewport)** | Ray-pick GameObject or drag active X/Y/Z Transform Gizmo |
| **Right Drag (Viewport)** | Orbit 3D Camera |
| **Middle Drag (Viewport)** | Pan 3D Camera |
| **Mouse Wheel (Viewport)** | Zoom 3D Camera |
| **Move / Rotate / Scale** | Switch 3D Transform Gizmo mode |
| **`Ctrl + Z` / `Ctrl + Y`** | Undo / Redo Scene & Property edits |
| **`F8`** | Toggle RmlUi Live Inspector / Debugger |

---

## Platform Support Matrix

| Platform | Target Game Parsing | Native Coconut App | Status |
|---|---|---|---|
| **Windows (x64)** | Mono & IL2CPP | OpenGL 3.3 + SDL3 + RmlUi | Supported & CI-verified |
| **Linux (x64 / arm64)** | Mono & IL2CPP | OpenGL 3.3 + SDL3 + RmlUi | Supported & CI-verified |
| **macOS (arm64 / x64)** | Mono & IL2CPP | OpenGL 3.3 + SDL3 + RmlUi | Supported & CI-verified |
| **Android (arm64-v8a / armv7 / x86_64)** | APK / AAB / IL2CPP / Mono | OpenGL ES 3.0 + SDL3 + RmlUi | Supported & NDK-verified |
| **iOS / Consoles (Switch, PS4/PS5, Xbox)** | Planned | Planned | Roadmap |

---

## License

MIT License — see [LICENSE](LICENSE). Bundled fonts (`PT Sans`) are licensed under the SIL Open Font License 1.1 (`resources/fonts/OFL-PTSans.txt`).
