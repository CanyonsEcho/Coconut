#include "ui/SyntaxHighlighter.h"
#include <unordered_set>
#include <cctype>
#include <sstream>

namespace coconut::ui {

std::string SyntaxHighlighter::EscapeRml(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + raw.size() / 8);
    for (char c : raw) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:  out.push_back(c); break;
        }
    }
    return out;
}

std::string SyntaxHighlighter::HighlightCSharp(std::string_view code) {
    static const std::unordered_set<std::string_view> kKeywords = {
        "using", "namespace", "public", "private", "protected", "internal", "static",
        "readonly", "const", "class", "struct", "enum", "interface", "sealed", "abstract",
        "virtual", "override", "extern", "unsafe", "partial", "void", "bool", "byte",
        "sbyte", "short", "ushort", "int", "uint", "long", "ulong", "float", "double",
        "decimal", "string", "char", "object", "var", "new", "return", "if", "else",
        "for", "foreach", "while", "do", "switch", "case", "default", "break", "continue",
        "try", "catch", "finally", "throw", "true", "false", "null", "this", "base",
        "ref", "out", "in", "params", "get", "set", "async", "await", "is", "as", "typeof"
    };

    std::istringstream iss{std::string(code)};
    std::string line;
    std::string result;
    result.reserve(code.size() * 2);

    while (std::getline(iss, line)) {
        result += "<div class=\"code-line\">";
        size_t i = 0;
        const size_t n = line.size();
        while (i < n) {
            if (i + 1 < n && line[i] == '/' && line[i + 1] == '/') {
                result += "<span class=\"syn-comment\">" + EscapeRml(std::string_view(line).substr(i)) + "</span>";
                break;
            }
            if (line[i] == '"') {
                size_t start = i++;
                while (i < n && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < n) ++i;
                    ++i;
                }
                if (i < n && line[i] == '"') ++i;
                result += "<span class=\"syn-string\">" + EscapeRml(std::string_view(line).substr(start, i - start)) + "</span>";
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(line[i])) &&
                (i == 0 || !std::isalnum(static_cast<unsigned char>(line[i - 1])))) {
                size_t start = i;
                while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '.' || line[i] == '_')) ++i;
                result += "<span class=\"syn-number\">" + EscapeRml(std::string_view(line).substr(start, i - start)) + "</span>";
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
                size_t start = i;
                while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
                std::string_view word = std::string_view(line).substr(start, i - start);
                if (kKeywords.contains(word)) {
                    result += "<span class=\"syn-kw\">" + EscapeRml(word) + "</span>";
                } else if (!word.empty() && std::isupper(static_cast<unsigned char>(word[0]))) {
                    result += "<span class=\"syn-type\">" + EscapeRml(word) + "</span>";
                } else {
                    result += EscapeRml(word);
                }
                continue;
            }
            if (line[i] == ' ') {
                result += "&#160;";
                ++i;
                continue;
            }
            result += EscapeRml(std::string_view(line).substr(i, 1));
            ++i;
        }
        result += "</div>";
    }
    return result;
}

std::string SyntaxHighlighter::HighlightDisassembly(std::string_view listing) {
    std::string out;
    out.reserve(listing.size() * 2);
    std::istringstream iss{std::string(listing)};
    std::string line;
    while (std::getline(iss, line)) {
        out += "<div class=\"code-line\">";
        if (!line.empty()) {
            if (line[0] == ';') {
                out += "<span class=\"syn-comment\">" + EscapeRml(line) + "</span>";
            } else {
                size_t p1 = line.find('|');
                if (p1 != std::string::npos) {
                    size_t p2 = line.find('|', p1 + 1);
                    out += "<span class=\"syn-addr\">" + EscapeRml(line.substr(0, p1)) + "</span>|";
                    if (p2 != std::string::npos) {
                        out += "<span class=\"syn-bytes\">" + EscapeRml(line.substr(p1 + 1, p2 - p1 - 1)) + "</span>|";
                        std::string rest = line.substr(p2 + 1);
                        size_t commentPos = rest.find("//");
                        if (commentPos != std::string::npos) {
                            out += "<span class=\"syn-op\">" + EscapeRml(rest.substr(0, commentPos)) + "</span>";
                            out += "<span class=\"syn-comment\">" + EscapeRml(rest.substr(commentPos)) + "</span>";
                        } else {
                            out += "<span class=\"syn-op\">" + EscapeRml(rest) + "</span>";
                        }
                    } else {
                        out += "<span class=\"syn-op\">" + EscapeRml(line.substr(p1 + 1)) + "</span>";
                    }
                } else {
                    out += EscapeRml(line);
                }
            }
        }
        out += "</div>";
    }
    return out;
}

std::string SyntaxHighlighter::HighlightYaml(std::string_view yaml) {
    std::string out;
    out.reserve(yaml.size() * 2);
    std::istringstream iss{std::string(yaml)};
    std::string line;
    while (std::getline(iss, line)) {
        out += "<div class=\"code-line\">";
        if (line.rfind("---", 0) == 0 || line.rfind("%YAML", 0) == 0 || line.rfind("%TAG", 0) == 0) {
            out += "<span class=\"syn-addr\">" + EscapeRml(line) + "</span>";
        } else {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                out += "<span class=\"syn-kw\">" + EscapeRml(line.substr(0, colon + 1)) + "</span>";
                out += "<span class=\"syn-string\">" + EscapeRml(line.substr(colon + 1)) + "</span>";
            } else {
                out += EscapeRml(line);
            }
        }
        out += "</div>";
    }
    return out;
}

} // namespace coconut::ui
