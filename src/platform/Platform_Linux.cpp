#include "platform/Platform.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace coconut::platform {

namespace {

std::string ShellEscape(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::optional<std::string> RunSimpleStdout(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::nullopt;
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    int status = pclose(pipe);
    if (status != 0) return std::nullopt;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (out.empty()) return std::nullopt;
    return out;
}

} // namespace

std::string_view HostPlatformName() { return "Linux"; }
bool IsMobilePlatform() { return false; }
void SetAndroidDataDir(const std::filesystem::path&) {}

std::filesystem::path GetExecutableDir() {
    char buf[4096]{};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
    return std::filesystem::current_path();
}

std::filesystem::path GetResourceDir() {
    auto exeRes = GetExecutableDir() / "resources";
    if (std::filesystem::exists(exeRes)) return exeRes;
    auto cwdRes = std::filesystem::current_path() / "resources";
    if (std::filesystem::exists(cwdRes)) return cwdRes;
    return "/usr/share/coconut/resources";
}

std::filesystem::path GetAppDataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base;
    if (xdg && *xdg) {
        base = std::filesystem::path(xdg) / "coconut";
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".local" / "share" / "coconut";
    } else {
        base = "/tmp/coconut";
    }
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
}

std::optional<std::filesystem::path> OpenFileDialog(
    const std::string& title,
    const std::vector<std::pair<std::string, std::string>>&) {
    if (auto p = RunSimpleStdout("zenity --file-selection --title=" + ShellEscape(title) + " 2>/dev/null")) {
        return std::filesystem::path(*p);
    }
    if (auto p = RunSimpleStdout("kdialog --getopenfilename . --title " + ShellEscape(title) + " 2>/dev/null")) {
        return std::filesystem::path(*p);
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> OpenFolderDialog(const std::string& title) {
    if (auto p = RunSimpleStdout("zenity --file-selection --directory --title=" + ShellEscape(title) + " 2>/dev/null")) {
        return std::filesystem::path(*p);
    }
    if (auto p = RunSimpleStdout("kdialog --getexistingdirectory . --title " + ShellEscape(title) + " 2>/dev/null")) {
        return std::filesystem::path(*p);
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> SaveFileDialog(
    const std::string& title,
    const std::string& defaultName,
    const std::vector<std::pair<std::string, std::string>>&) {
    if (auto p = RunSimpleStdout("zenity --file-selection --save --confirm-overwrite --filename=" +
                                 ShellEscape(defaultName) + " --title=" + ShellEscape(title) + " 2>/dev/null")) {
        return std::filesystem::path(*p);
    }
    if (auto p = RunSimpleStdout("kdialog --getsavefilename " + ShellEscape(defaultName) +
                                 " --title " + ShellEscape(title) + " 2>/dev/null")) {
        return std::filesystem::path(*p);
    }
    return std::nullopt;
}

ProcessResult RunProcess(
    const std::string& executable,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir,
    std::function<void(const std::string&)> onOutputLine) {
    ProcessResult result;
    std::string cmd;
    if (!workingDir.empty()) {
        cmd += "cd " + ShellEscape(workingDir.string()) + " && ";
    }
    cmd += ShellEscape(executable);
    for (const auto& a : args) {
        cmd += " " + ShellEscape(a);
    }
    cmd += " 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.stderrText = "popen failed for: " + executable;
        return result;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        result.stdoutText += line;
        if (onOutputLine) {
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            onOutputLine(line);
        }
    }
    int status = pclose(pipe);
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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
        resp.error = "getaddrinfo failed for " + host;
        return resp;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        resp.error = "socket creation failed";
        return resp;
    }

    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
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
    } else {
        resp.error = "Malformed HTTP response";
    }
    return resp;
}

Result<ByteBuffer> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return Result<ByteBuffer>::Err("Failed to open file: " + path.string());
    auto sz = ifs.tellg();
    if (sz < 0) return Result<ByteBuffer>::Err("Failed to size file: " + path.string());
    ByteBuffer buf(static_cast<size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    if (sz > 0 && !ifs.read(reinterpret_cast<char*>(buf.data()), sz)) {
        return Result<ByteBuffer>::Err("Failed to read file: " + path.string());
    }
    return Result<ByteBuffer>::Ok(std::move(buf));
}

Result<void> WriteFileBytes(const std::filesystem::path& path, ByteSpan data) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return Result<void>::Err("Failed to write file: " + path.string());
    if (!data.empty()) ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return ofs.good() ? Result<void>::Ok() : Result<void>::Err("Write error: " + path.string());
}

Result<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return Result<std::string>::Err("Failed to open text file: " + path.string());
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return Result<std::string>::Ok(ss.str());
}

Result<void> WriteFileText(const std::filesystem::path& path, std::string_view text) {
    return WriteFileBytes(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

} // namespace coconut::platform

#endif
