#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>
#include <variant>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace coconut {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

using ByteBuffer = std::vector<u8>;
using ByteSpan   = std::span<const u8>;

enum class Endian {
    Little,
    Big
};

enum class ScriptingBackend {
    Unknown,
    Mono,
    IL2CPP
};

inline std::string_view ToString(ScriptingBackend b) {
    switch (b) {
        case ScriptingBackend::Mono:   return "Mono";
        case ScriptingBackend::IL2CPP: return "IL2CPP";
        default:                       return "Unknown";
    }
}

enum class TargetPlatform : i32 {
    Unknown             = 0,
    StandaloneOSX       = 2,
    StandaloneWindows   = 5,
    iOS                 = 9,
    Android             = 13,
    StandaloneLinux     = 17,
    StandaloneWindows64 = 19,
    WebGL               = 20,
    StandaloneLinux64   = 24,
    PS4                 = 31,
    XboxOne             = 33,
    Switch              = 38,
    PS5                 = 44
};

inline std::string_view ToString(TargetPlatform p) {
    switch (p) {
        case TargetPlatform::StandaloneOSX:       return "macOS";
        case TargetPlatform::StandaloneWindows:   return "Windows (x86)";
        case TargetPlatform::StandaloneWindows64: return "Windows (x64)";
        case TargetPlatform::iOS:                 return "iOS";
        case TargetPlatform::Android:             return "Android";
        case TargetPlatform::StandaloneLinux:     return "Linux (x86)";
        case TargetPlatform::StandaloneLinux64:   return "Linux (x64)";
        case TargetPlatform::WebGL:               return "WebGL";
        case TargetPlatform::PS4:                 return "PS4";
        case TargetPlatform::XboxOne:             return "Xbox One";
        case TargetPlatform::Switch:              return "Nintendo Switch";
        case TargetPlatform::PS5:                 return "PS5";
        default:                                  return "Unknown";
    }
}

struct UnityVersion {
    i32 major = 0;
    i32 minor = 0;
    i32 patch = 0;
    char type = 'f'; // a, b, c, f, p, x
    i32 build = 1;

    static UnityVersion Parse(std::string_view str) {
        UnityVersion v;
        if (str.empty()) return v;
        size_t pos = 0;
        auto readInt = [&]() -> i32 {
            i32 val = 0;
            while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos]))) {
                val = val * 10 + (str[pos] - '0');
                ++pos;
            }
            return val;
        };
        v.major = readInt();
        if (pos < str.size() && str[pos] == '.') { ++pos; v.minor = readInt(); }
        if (pos < str.size() && str[pos] == '.') { ++pos; v.patch = readInt(); }
        if (pos < str.size() && std::isalpha(static_cast<unsigned char>(str[pos]))) {
            v.type = str[pos++];
            v.build = readInt();
        }
        return v;
    }

    std::string ToString() const {
        if (major == 0 && minor == 0 && patch == 0) return "Unknown";
        std::ostringstream oss;
        oss << major << '.' << minor << '.' << patch << type << build;
        return oss.str();
    }

    bool IsValid() const { return major > 0; }

    auto operator<=>(const UnityVersion& o) const {
        if (major != o.major) return major <=> o.major;
        if (minor != o.minor) return minor <=> o.minor;
        if (patch != o.patch) return patch <=> o.patch;
        if (type  != o.type)  return type  <=> o.type;
        return build <=> o.build;
    }
    bool operator==(const UnityVersion& o) const = default;
};

template <typename T>
class Result {
public:
    static Result Ok(T value) { return Result(std::move(value)); }
    static Result Err(std::string message) { return Result(ErrorTag{}, std::move(message)); }

    bool IsOk() const { return std::holds_alternative<T>(storage_); }
    bool IsErr() const { return !IsOk(); }
    explicit operator bool() const { return IsOk(); }

    T& Value() { return std::get<T>(storage_); }
    const T& Value() const { return std::get<T>(storage_); }
    const std::string& Error() const { return std::get<ErrorValue>(storage_).message; }

private:
    struct ErrorTag {};
    struct ErrorValue { std::string message; };
    explicit Result(T val) : storage_(std::move(val)) {}
    Result(ErrorTag, std::string msg) : storage_(ErrorValue{std::move(msg)}) {}
    std::variant<T, ErrorValue> storage_;
};

template <>
class Result<void> {
public:
    static Result Ok() { return Result(true, {}); }
    static Result Err(std::string message) { return Result(false, std::move(message)); }

    bool IsOk() const { return ok_; }
    bool IsErr() const { return !ok_; }
    explicit operator bool() const { return ok_; }
    const std::string& Error() const { return error_; }

private:
    Result(bool ok, std::string err) : ok_(ok), error_(std::move(err)) {}
    bool ok_ = false;
    std::string error_;
};

struct AssetKey {
    std::string fileName;
    i64 pathId = 0;

    bool operator==(const AssetKey&) const = default;
};

} // namespace coconut
