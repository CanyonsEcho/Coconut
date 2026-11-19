#pragma once

#include "core/Types.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace coconut::platform {

struct ProcessResult {
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
};

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string error;
};

std::string_view HostPlatformName();
bool IsMobilePlatform();

std::filesystem::path GetExecutableDir();
std::filesystem::path GetResourceDir();
std::filesystem::path GetAppDataDir();

std::optional<std::filesystem::path> OpenFileDialog(
    const std::string& title,
    const std::vector<std::pair<std::string, std::string>>& filters = {});

std::optional<std::filesystem::path> OpenFolderDialog(const std::string& title);

std::optional<std::filesystem::path> SaveFileDialog(
    const std::string& title,
    const std::string& defaultName,
    const std::vector<std::pair<std::string, std::string>>& filters = {});

// Synchronous external process runner with optional streaming stdout callback.
ProcessResult RunProcess(
    const std::string& executable,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir = {},
    std::function<void(const std::string&)> onOutputLine = nullptr);

// Localhost/HTTP client (used for communicating with AssetRipper headless HTTP API).
HttpResponse HttpRequest(
    const std::string& method,
    const std::string& host,
    u16 port,
    const std::string& path,
    const std::string& body = {},
    const std::string& contentType = "application/x-www-form-urlencoded",
    int timeoutMs = 15000);

// Portable file I/O helpers.
Result<ByteBuffer> ReadFileBytes(const std::filesystem::path& path);
Result<void> WriteFileBytes(const std::filesystem::path& path, ByteSpan data);
Result<std::string> ReadFileText(const std::filesystem::path& path);
Result<void> WriteFileText(const std::filesystem::path& path, std::string_view text);

// Android-specific storage hook (called from JNI/SDL startup on Android).
void SetAndroidDataDir(const std::filesystem::path& dir);

} // namespace coconut::platform
