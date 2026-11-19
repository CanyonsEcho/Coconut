#include "platform/Platform.h"

#ifdef _WIN32

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <winhttp.h>

#include <fstream>
#include <sstream>

namespace coconut::platform {

namespace {

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring QuoteArg(const std::string& arg) {
    std::wstring w = ToWide(arg);
    if (w.find_first_of(L" \t\"") == std::wstring::npos && !w.empty()) {
        return w;
    }
    std::wstring quoted = L"\"";
    for (wchar_t c : w) {
        if (c == L'"') quoted += L"\\\"";
        else quoted += c;
    }
    quoted += L"\"";
    return quoted;
}

} // namespace

std::string_view HostPlatformName() { return "Windows"; }
bool IsMobilePlatform() { return false; }
void SetAndroidDataDir(const std::filesystem::path&) {}

std::filesystem::path GetExecutableDir() {
    wchar_t buf[MAX_PATH * 4]{};
    DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (len == 0) return std::filesystem::current_path();
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path GetResourceDir() {
    auto exeRes = GetExecutableDir() / "resources";
    if (std::filesystem::exists(exeRes)) return exeRes;
    auto cwdRes = std::filesystem::current_path() / "resources";
    if (std::filesystem::exists(cwdRes)) return cwdRes;
    auto parentRes = GetExecutableDir().parent_path() / "resources";
    if (std::filesystem::exists(parentRes)) return parentRes;
    return exeRes;
}

std::filesystem::path GetAppDataDir() {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        std::filesystem::path result = std::filesystem::path(path) / "Coconut";
        CoTaskMemFree(path);
        std::error_code ec;
        std::filesystem::create_directories(result, ec);
        return result;
    }
    return GetExecutableDir() / "data";
}

std::optional<std::filesystem::path> OpenFileDialog(
    const std::string& title,
    const std::vector<std::pair<std::string, std::string>>& filters) {
    wchar_t fileBuf[4096]{};
    std::wstring filterStr;
    for (const auto& [label, pattern] : filters) {
        filterStr += ToWide(label);
        filterStr.push_back(L'\0');
        filterStr += ToWide(pattern);
        filterStr.push_back(L'\0');
    }
    if (filterStr.empty()) {
        filterStr = L"All Files (*.*)";
        filterStr.push_back(L'\0');
        filterStr += L"*.*";
        filterStr.push_back(L'\0');
    }
    filterStr.push_back(L'\0');

    std::wstring wtitle = ToWide(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        return std::filesystem::path(fileBuf);
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> OpenFolderDialog(const std::string& title) {
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    std::optional<std::filesystem::path> result;

    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dialog)))) {
        DWORD opts = 0;
        dialog->GetOptions(&opts);
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        std::wstring wtitle = ToWide(title);
        dialog->SetTitle(wtitle.c_str());
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (SUCCEEDED(hrInit)) {
        CoUninitialize();
    }
    return result;
}

std::optional<std::filesystem::path> SaveFileDialog(
    const std::string& title,
    const std::string& defaultName,
    const std::vector<std::pair<std::string, std::string>>& filters) {
    wchar_t fileBuf[4096]{};
    std::wstring wdef = ToWide(defaultName);
    if (wdef.size() < std::size(fileBuf)) {
        std::copy(wdef.begin(), wdef.end(), fileBuf);
    }

    std::wstring filterStr;
    for (const auto& [label, pattern] : filters) {
        filterStr += ToWide(label);
        filterStr.push_back(L'\0');
        filterStr += ToWide(pattern);
        filterStr.push_back(L'\0');
    }
    if (filterStr.empty()) {
        filterStr = L"All Files (*.*)";
        filterStr.push_back(L'\0');
        filterStr += L"*.*";
        filterStr.push_back(L'\0');
    }
    filterStr.push_back(L'\0');

    std::wstring wtitle = ToWide(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filterStr.c_str();
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
    ofn.lpstrTitle = wtitle.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameW(&ofn)) {
        return std::filesystem::path(fileBuf);
    }
    return std::nullopt;
}

ProcessResult RunProcess(
    const std::string& executable,
    const std::vector<std::string>& args,
    const std::filesystem::path& workingDir,
    std::function<void(const std::string&)> onOutputLine) {
    ProcessResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.stderrText = "Failed to create process pipe";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmdLine = QuoteArg(executable);
    for (const auto& a : args) {
        cmdLine += L" ";
        cmdLine += QuoteArg(a);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring wdir = workingDir.empty() ? L"" : workingDir.wstring();
    BOOL created = CreateProcessW(
        nullptr,
        cmdLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        wdir.empty() ? nullptr : wdir.c_str(),
        &si,
        &pi);

    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        result.stderrText = "Failed to launch process: " + executable;
        return result;
    }

    char buf[4096];
    DWORD bytesRead = 0;
    std::string lineBuf;
    while (ReadFile(readPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        result.stdoutText.append(buf, bytesRead);
        if (onOutputLine) {
            lineBuf.append(buf, bytesRead);
            size_t pos = 0;
            while ((pos = lineBuf.find('\n')) != std::string::npos) {
                std::string line = lineBuf.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                onOutputLine(line);
                lineBuf.erase(0, pos + 1);
            }
        }
    }
    if (onOutputLine && !lineBuf.empty()) {
        onOutputLine(lineBuf);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    CloseHandle(readPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
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
    HINTERNET hSession = WinHttpOpen(L"Coconut/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    std::wstring wHost = ToWide(host);
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        resp.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    std::wstring wMethod = ToWide(method);
    std::wstring wPath = ToWide(path);
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, wMethod.c_str(), wPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        resp.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    std::wstring headers;
    if (!contentType.empty() && !body.empty()) {
        headers = L"Content-Type: " + ToWide(contentType) + L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(
        hRequest,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);

    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
        resp.statusCode = static_cast<int>(statusCode);

        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (WinHttpReadData(hRequest, chunk.data(), avail, &read)) {
                resp.body.append(chunk.data(), read);
            }
        }
    } else {
        resp.error = "WinHttp request failed with error " + std::to_string(GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

Result<ByteBuffer> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        return Result<ByteBuffer>::Err("Failed to open file: " + path.string());
    }
    auto sz = ifs.tellg();
    if (sz < 0) {
        return Result<ByteBuffer>::Err("Failed to determine file size: " + path.string());
    }
    ByteBuffer buf(static_cast<size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    if (sz > 0 && !ifs.read(reinterpret_cast<char*>(buf.data()), sz)) {
        return Result<ByteBuffer>::Err("Failed to read file: " + path.string());
    }
    return Result<ByteBuffer>::Ok(std::move(buf));
}

Result<void> WriteFileBytes(const std::filesystem::path& path, ByteSpan data) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        return Result<void>::Err("Failed to open file for writing: " + path.string());
    }
    if (!data.empty()) {
        ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!ofs.good()) {
        return Result<void>::Err("Failed to write file: " + path.string());
    }
    return Result<void>::Ok();
}

Result<std::string> ReadFileText(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return Result<std::string>::Err("Failed to open text file: " + path.string());
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return Result<std::string>::Ok(ss.str());
}

Result<void> WriteFileText(const std::filesystem::path& path, std::string_view text) {
    ByteSpan span(reinterpret_cast<const u8*>(text.data()), text.size());
    return WriteFileBytes(path, span);
}

} // namespace coconut::platform

#endif // _WIN32
