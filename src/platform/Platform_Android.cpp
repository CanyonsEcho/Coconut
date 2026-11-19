#include "platform/Platform.h"

#if defined(__ANDROID__)

#include <android/log.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <sstream>

namespace coconut::platform {

namespace {
std::filesystem::path g_androidDataDir = "/data/local/tmp/coconut";
}

std::string_view HostPlatformName() { return "Android"; }
bool IsMobilePlatform() { return true; }

void SetAndroidDataDir(const std::filesystem::path& dir) {
    if (!dir.empty()) {
        g_androidDataDir = dir;
        std::error_code ec;
        std::filesystem::create_directories(g_androidDataDir, ec);
    }
}

std::filesystem::path GetExecutableDir() {
    return g_androidDataDir;
}

std::filesystem::path GetResourceDir() {
    auto res = g_androidDataDir / "resources";
    if (std::filesystem::exists(res)) return res;
    return "resources";
}

std::filesystem::path GetAppDataDir() {
    return g_androidDataDir;
}

std::optional<std::filesystem::path> OpenFileDialog(
    const std::string&,
    const std::vector<std::pair<std::string, std::string>>&) {
    // On Android, file picking is routed through CoconutActivity SAF Intent / built-in path picker
    return std::nullopt;
}

std::optional<std::filesystem::path> OpenFolderDialog(const std::string&) {
    return std::nullopt;
}

std::optional<std::filesystem::path> SaveFileDialog(
    const std::string&,
    const std::string& defaultName,
    const std::vector<std::pair<std::string, std::string>>&) {
    return g_androidDataDir / "exports" / defaultName;
}

ProcessResult RunProcess(
    const std::string& executable,
    const std::vector<std::string>&,
    const std::filesystem::path&,
    std::function<void(const std::string&)>) {
    ProcessResult result;
    result.exitCode = -1;
    result.stderrText = "External subprocess execution (" + executable +
                        ") is sandboxed on Android; using native Coconut exporter.";
    return result;
}

HttpResponse HttpRequest(
    const std::string& method,
    const std::string& host,
    u16 port,
    const std::string& path,
    const std::string& body,
    const std::string& contentType,
    int timeoutMs) {
    HttpResponse resp;
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        resp.error = "getaddrinfo failed";
        return resp;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        resp.error = "socket failed";
        return resp;
    }
    struct timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        resp.error = "connect failed";
        return resp;
    }
    freeaddrinfo(res);

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Connection: close\r\n";
    if (!body.empty()) {
        req << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n" << body;
    std::string reqStr = req.str();
    send(fd, reqStr.data(), reqStr.size(), 0);

    std::string raw;
    char buf[4096];
    ssize_t n = 0;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, static_cast<size_t>(n));
    }
    close(fd);

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        std::string statusLine = raw.substr(0, raw.find("\r\n"));
        std::istringstream iss(statusLine);
        std::string httpVer;
        iss >> httpVer >> resp.statusCode;
        resp.body = raw.substr(headerEnd + 4);
    }
    return resp;
}

Result<ByteBuffer> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return Result<ByteBuffer>::Err("Failed to open file: " + path.string());
    auto sz = ifs.tellg();
    if (sz < 0) return Result<ByteBuffer>::Err("Failed to size file");
    ByteBuffer buf(static_cast<size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    if (sz > 0 && !ifs.read(reinterpret_cast<char*>(buf.data()), sz)) {
        return Result<ByteBuffer>::Err("Failed to read file");
    }
    return Result<ByteBuffer>::Ok(std::move(buf));
}

Result<void> WriteFileBytes(const std::filesystem::path& path, ByteSpan data) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return Result<void>::Err("Failed to write file");
    if (!data.empty()) ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return ofs.good() ? Result<void>::Ok() : Result<void>::Err("Write error");
}

Result<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return Result<std::string>::Err("Failed to open text file");
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return Result<std::string>::Ok(ss.str());
}

Result<void> WriteFileText(const std::filesystem::path& path, std::string_view text) {
    return WriteFileBytes(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

} // namespace coconut::platform

#endif
