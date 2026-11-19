cmake_minimum_required(VERSION 3.24)

# Ensure compatibility with CMake 4.x when building third-party FetchContent dependencies
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "Minimum policy version for subprojects")
endif()

project(Coconut
    VERSION 0.2.0
    DESCRIPTION "Coconut — Desktop Unity Mono/IL2CPP Decompiler, Scene/Bundle Editor, Project Exporter & Game Rebuilder"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COCONUT_BUILD_TESTS "Build Coconut unit and integration tests" ON)
option(COCONUT_BUILD_GUI "Build Coconut RmlUi + SDL3 desktop graphical application" ON)
option(COCONUT_BUILD_INTERNAL_RIPPER "Build internal forked AssetRipper (.NET 10 Coconut.RipperCore) when dotnet is available" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# -----------------------------------------------------------------------------
# Third-Party Dependencies (Reproducible pinned tags via FetchContent)
# -----------------------------------------------------------------------------

# 1. GLM (Header-only 3D math library)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# 2. nlohmann/json (JSON for ProjectManager, .coco DB, manifest.json, script.json, RipperCore reports)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# 3. LZ4 (UnityFS LZ4/LZ4HC block compression & decompression)
FetchContent_Declare(
    lz4_src
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.11.1
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(lz4_src)
if(NOT lz4_src_POPULATED)
    FetchContent_Populate(lz4_src)
endif()
add_library(coconut_lz4 STATIC
    ${lz4_src_SOURCE_DIR}/lib/lz4.c
    ${lz4_src_SOURCE_DIR}/lib/lz4hc.c
)
target_include_directories(coconut_lz4 PUBLIC ${lz4_src_SOURCE_DIR}/lib)

# 4. miniz (Zlib/Deflate + full ZIP/APK archive reader & writer for Android game builds)
FetchContent_Declare(
    miniz_src
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(miniz_src)
if(NOT miniz_src_POPULATED)
    FetchContent_Populate(miniz_src)
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/miniz_export.h" "#pragma once\n#define MINIZ_EXPORT\n")
add_library(coconut_miniz STATIC
    ${miniz_src_SOURCE_DIR}/miniz.c
    ${miniz_src_SOURCE_DIR}/miniz_tdef.c
    ${miniz_src_SOURCE_DIR}/miniz_tinfl.c
    ${miniz_src_SOURCE_DIR}/miniz_zip.c
)
target_include_directories(coconut_miniz PUBLIC
    ${miniz_src_SOURCE_DIR}
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
if(MSVC)
    target_compile_definitions(coconut_miniz PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# 5. stb (Header-only image loading & PNG/TGA writing for textures)
FetchContent_Declare(
    stb_src
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(stb_src)
if(NOT stb_src_POPULATED)
    FetchContent_Populate(stb_src)
endif()
add_library(coconut_stb INTERFACE)
target_include_directories(coconut_stb INTERFACE ${stb_src_SOURCE_DIR})

# 6. shine (Portable fixed-point C MPEG-1/2 Layer III encoder for native MP3 audio export)
FetchContent_Declare(
    shine_src
    GIT_REPOSITORY https://github.com/toots/shine.git
    GIT_TAG        3.1.1
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(shine_src)
if(NOT shine_src_POPULATED)
    FetchContent_Populate(shine_src)
endif()
add_library(coconut_shine STATIC
    ${shine_src_SOURCE_DIR}/src/lib/bitstream.c
    ${shine_src_SOURCE_DIR}/src/lib/huffman.c
    ${shine_src_SOURCE_DIR}/src/lib/l3bitstream.c
    ${shine_src_SOURCE_DIR}/src/lib/l3loop.c
    ${shine_src_SOURCE_DIR}/src/lib/l3mdct.c
    ${shine_src_SOURCE_DIR}/src/lib/l3subband.c
    ${shine_src_SOURCE_DIR}/src/lib/layer3.c
    ${shine_src_SOURCE_DIR}/src/lib/reservoir.c
    ${shine_src_SOURCE_DIR}/src/lib/tables.c
)
target_include_directories(coconut_shine PUBLIC ${shine_src_SOURCE_DIR}/src/lib)
if(MSVC)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/shine_msvc_compat.h" "#pragma once\n#define __attribute__(x)\n")
    target_compile_options(coconut_shine PRIVATE /W0 "/FI${CMAKE_CURRENT_BINARY_DIR}/generated/shine_msvc_compat.h")
    target_compile_definitions(coconut_shine PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(coconut_shine PRIVATE -w)
endif()

if(COCONUT_BUILD_GUI)
    # 7. SDL3 (Desktop windowing & OpenGL 3.3 Core for Windows, macOS, and Linux)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.20
        GIT_SHALLOW    TRUE
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)

    # 8. FreeType (Required by RmlUi for crisp modern sans-serif typography)
    FetchContent_Declare(
        freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE
    )
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(freetype)

    if(TARGET freetype AND NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(FREETYPE_FOUND TRUE CACHE BOOL "" FORCE)
    set(FREETYPE_INCLUDE_DIRS "${freetype_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(FREETYPE_LIBRARIES freetype CACHE STRING "" FORCE)

    # 9. RmlUi 6.3 (HTML/CSS-inspired C++ UI Engine)
    FetchContent_Declare(
        RmlUi
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG        6.3
        GIT_SHALLOW    TRUE
    )
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
    set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
    set(RMLUI_BACKEND "auto" CACHE STRING "" FORCE)
    set(RMLUI_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(RmlUi)
endif()

# -----------------------------------------------------------------------------
# Coconut Core Static Library (Headless Engine — Supports Android/Win/Linux/Mac Unity Games)
# -----------------------------------------------------------------------------
set(COCONUT_CORE_SOURCES
    src/core/Logger.cpp
    src/core/BinaryReader.cpp
    src/core/BinaryWriter.cpp
    src/core/LzmaDecoder.cpp
    src/core/Compression.cpp
    src/core/CommandHistory.cpp
    src/core/ProjectDatabase.cpp
    src/core/ProjectManager.cpp
    src/unity/ClassIDs.cpp
    src/unity/UnityFS.cpp
    src/unity/TypeTree.cpp
    src/unity/SerializedFile.cpp
    src/unity/GameLoader.cpp
    src/assets/StbImpl.cpp
    src/assets/TextureAsset.cpp
    src/assets/MeshAsset.cpp
    src/assets/AudioAsset.cpp
    src/assets/MaterialAsset.cpp
    src/assets/AssetManager.cpp
    src/decompiler/MonoAssembly.cpp
    src/decompiler/CilDecompiler.cpp
    src/decompiler/Il2CppMetadata.cpp
    src/decompiler/NativeDisassembler.cpp
    src/decompiler/DecompilerEngine.cpp
    src/scene/SceneDocument.cpp
    src/scene/GizmoMath.cpp
    src/export/AssetRipperClient.cpp
    src/export/UnityProjectExporter.cpp
    src/export/GameRebuilder.cpp
    src/fixup/MeshDecombiner.cpp
    src/fixup/PrefabReconstructor.cpp
    src/fixup/BuildErrorFixer.cpp
    src/fixup/ProjectFixupPipeline.cpp
)

if(WIN32)
    list(APPEND COCONUT_CORE_SOURCES src/platform/Platform_Win32.cpp)
elseif(APPLE)
    list(APPEND COCONUT_CORE_SOURCES src/platform/Platform_MacOS.mm)
else()
    list(APPEND COCONUT_CORE_SOURCES src/platform/Platform_Linux.cpp)
endif()

add_library(coconut_core STATIC ${COCONUT_CORE_SOURCES})

target_include_directories(coconut_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(coconut_core PUBLIC
    glm::glm
    nlohmann_json::nlohmann_json
    coconut_lz4
    coconut_miniz
    coconut_stb
    coconut_shine
)

if(WIN32)
    target_link_libraries(coconut_core PUBLIC comdlg32 shell32 ole32 winhttp)
    target_compile_definitions(coconut_core PUBLIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        COCONUT_PLATFORM_WINDOWS=1
    )
elseif(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(coconut_core PUBLIC ${COCOA_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
    target_compile_definitions(coconut_core PUBLIC COCONUT_PLATFORM_MACOS=1)
else()
    target_link_libraries(coconut_core PUBLIC pthread dl)
    target_compile_definitions(coconut_core PUBLIC COCONUT_PLATFORM_LINUX=1)
endif()

if(MSVC)
    target_compile_options(coconut_core PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(coconut_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# -----------------------------------------------------------------------------
# Internal Forked AssetRipper Engine (Coconut.RipperCore, .NET 10)
# -----------------------------------------------------------------------------
if(COCONUT_BUILD_INTERNAL_RIPPER)
    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/internal/Coconut.RipperCore/Coconut.RipperCore.csproj")
        add_custom_target(coconut_ripper_core
            COMMAND "${DOTNET_EXECUTABLE}" build
                "${CMAKE_CURRENT_SOURCE_DIR}/internal/Coconut.RipperCore/Coconut.RipperCore.csproj"
                -c Release --nologo
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Building internal forked AssetRipper engine (Coconut.RipperCore)"
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Coconut Desktop GUI Application (Windows, macOS, Linux — SDL3 + OpenGL 3.3 + RmlUi 6.3)
# -----------------------------------------------------------------------------
if(COCONUT_BUILD_GUI)
    set(RMLUI_BACKENDS_DIR "${rmlui_SOURCE_DIR}/Backends")

    set(COCONUT_GUI_SOURCES
        src/main.cpp
        src/scene/SceneRenderer.cpp
        src/ui/RmlBackend.cpp
        src/ui/SyntaxHighlighter.cpp
        src/ui/WorkbenchApp.cpp
        ${RMLUI_BACKENDS_DIR}/RmlUi_Platform_SDL.cpp
        ${RMLUI_BACKENDS_DIR}/RmlUi_Renderer_GL3.cpp
    )

    if(APPLE)
        add_executable(coconut MACOSX_BUNDLE ${COCONUT_GUI_SOURCES})
        set_target_properties(coconut PROPERTIES
            MACOSX_BUNDLE_BUNDLE_NAME "Coconut"
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.canyonsecho.coconut"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        )
    else()
        add_executable(coconut ${COCONUT_GUI_SOURCES})
    endif()

    target_include_directories(coconut PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${RMLUI_BACKENDS_DIR}
    )

    target_compile_definitions(coconut PRIVATE
        RMLUI_SDL_VERSION_MAJOR=3
        COCONUT_VERSION="${PROJECT_VERSION}"
    )

    find_package(OpenGL REQUIRED)
    if(EXISTS "${rmlui_SOURCE_DIR}/Dependencies/gl3w/gl3w.c")
        target_sources(coconut PRIVATE "${rmlui_SOURCE_DIR}/Dependencies/gl3w/gl3w.c")
        target_include_directories(coconut PRIVATE "${rmlui_SOURCE_DIR}/Dependencies/gl3w")
    elseif(EXISTS "${rmlui_SOURCE_DIR}/Dependencies/glad/src/gl.c")
        target_sources(coconut PRIVATE "${rmlui_SOURCE_DIR}/Dependencies/glad/src/gl.c")
        target_include_directories(coconut PRIVATE "${rmlui_SOURCE_DIR}/Dependencies/glad/include")
        target_compile_definitions(coconut PRIVATE RMLUI_GL3_CUSTOM_LOADER=<glad/gl.h>)
    elseif(EXISTS "${RMLUI_BACKENDS_DIR}/RmlUi_GL3/glad.c")
        target_sources(coconut PRIVATE "${RMLUI_BACKENDS_DIR}/RmlUi_GL3/glad.c")
        target_include_directories(coconut PRIVATE "${RMLUI_BACKENDS_DIR}/RmlUi_GL3")
    endif()

    target_link_libraries(coconut PRIVATE
        coconut_core
        RmlUi::Core
        RmlUi::Debugger
        SDL3::SDL3-static
        OpenGL::GL
    )

    if(MSVC)
        target_compile_options(coconut PRIVATE /W3 /utf-8)
        set_target_properties(coconut PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )
    endif()

    add_custom_command(TARGET coconut POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:coconut>/resources"
        COMMENT "Copying Coconut UI resources and fonts"
    )
endif()

# -----------------------------------------------------------------------------
# Unit & Integration Tests (Synthetic fixtures live strictly inside tests/)
# -----------------------------------------------------------------------------
if(COCONUT_BUILD_TESTS)
    enable_testing()
    add_executable(coconut_tests
        tests/test_main.cpp
        tests/TestFixtures.cpp
    )
    target_include_directories(coconut_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(coconut_tests PRIVATE coconut_core)
    if(MSVC)
        target_compile_options(coconut_tests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME coconut_unit_tests COMMAND coconut_tests)
endif()
