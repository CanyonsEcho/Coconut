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

    std::string out;
    out.reserve(code.size() * 2);

    size_t i = 0;
    const size_t n = code.size();
    while (i < n) {
        // Line comment
        if (i + 1 < n && code[i] == '/' && code[i + 1] == '/') {
            size_t start = i;
            while (i < n && code[i] != '\n') ++i;
            out += "<span class=\"syn-comment\">" + EscapeRml(code.substr(start, i - start)) + "</span>";
            continue;
        }
        // String literal
        if (code[i] == '"') {
            size_t start = i++;
            while (i < n && code[i] != '"' && code[i] != '\n') {
                if (code[i] == '\\' && i + 1 < n) ++i;
                ++i;
            }
            if (i < n && code[i] == '"') ++i;
            out += "<span class=\"syn-string\">" + EscapeRml(code.substr(start, i - start)) + "</span>";
            continue;
        }
        // Number
        if (std::isdigit(static_cast<unsigned char>(code[i])) &&
            (i == 0 || !std::isalnum(static_cast<unsigned char>(code[i - 1])))) {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(code[i])) || code[i] == '.' || code[i] == '_')) ++i;
            out += "<span class=\"syn-number\">" + EscapeRml(code.substr(start, i - start)) + "</span>";
            continue;
        }
        // Identifier / keyword / type
        if (std::isalpha(static_cast<unsigned char>(code[i])) || code[i] == '_') {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(code[i])) || code[i] == '_')) ++i;
            std::string_view word = code.substr(start, i - start);
            if (kKeywords.contains(word)) {
                out += "<span class=\"syn-kw\">" + EscapeRml(word) + "</span>";
            } else if (!word.empty() && std::isupper(static_cast<unsigned char>(word[0]))) {
                out += "<span class=\"syn-type\">" + EscapeRml(word) + "</span>";
            } else {
                out += EscapeRml(word);
            }
            continue;
        }
        out += EscapeRml(code.substr(i, 1));
        ++i;
    }
    return out;
}

std::string SyntaxHighlighter::HighlightDisassembly(std::string_view listing) {
    std::string out;
    out.reserve(listing.size() * 2);
    std::istringstream iss{std::string(listing)};
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (!first) out += "\n";
        first = false;
        if (line.empty()) continue;
        if (line[0] == ';') {
            out += "<span class=\"syn-comment\">" + EscapeRml(line) + "</span>";
            continue;
        }
        // Format: "IL_0000 | 02       | ldarg.0" or "0x00001000 | 48 89 5c | mov ..."
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
    return out;
}

std::string SyntaxHighlighter::HighlightYaml(std::string_view yaml) {
    std::string out;
    out.reserve(yaml.size() * 2);
    std::istringstream iss{std::string(yaml)};
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (!first) out += "\n";
        first = false;
        if (line.rfind("---", 0) == 0 || line.rfind("%YAML", 0) == 0 || line.rfind("%TAG", 0) == 0) {
            out += "<span class=\"syn-addr\">" + EscapeRml(line) + "</span>";
            continue;
        }
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            out += "<span class=\"syn-kw\">" + EscapeRml(line.substr(0, colon + 1)) + "</span>";
            out += "<span class=\"syn-string\">" + EscapeRml(line.substr(colon + 1)) + "</span>";
        } else {
            out += EscapeRml(line);
        }
    }
    return out;
}

} // namespace coconut::ui
