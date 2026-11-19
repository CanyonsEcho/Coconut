#pragma once

#include "core/Types.h"
#include <filesystem>
#include <string>
#include <vector>

namespace coconut::fixup {

struct BuildFixReport {
    size_t scriptsScanned = 0;
    size_t scriptsFixed = 0;
    size_t compilerGeneratedStripped = 0;
    size_t unityApiMigrations = 0;
    size_t invalidIdentifiersFixed = 0;
    size_t shadersRepaired = 0;
    size_t missingMetasCreated = 0;
    bool cscUnsafeGenerated = false;
    std::vector<std::string> packagesAdded;
    std::vector<std::string> logs;
};

// Automated Unity C# / Shader / Package / ProjectSettings Build Error Fixer
// Supports any target Unity version (5.6 through 2022.3 LTS and Unity 6 / 6000.x).
class BuildErrorFixer {
public:
    static Result<BuildFixReport> RunOnProject(
        const std::filesystem::path& unityProjectDir,
        const std::string& targetUnityVersion = "2022.3.15f1");
};

} // namespace coconut::fixup
