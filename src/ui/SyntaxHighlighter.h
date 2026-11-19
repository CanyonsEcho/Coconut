#pragma once

#include <string>
#include <string_view>

namespace coconut::ui {

class SyntaxHighlighter {
public:
    static std::string EscapeRml(std::string_view raw);
    static std::string HighlightCSharp(std::string_view code);
    static std::string HighlightDisassembly(std::string_view listing);
    static std::string HighlightYaml(std::string_view yaml);
};

} // namespace coconut::ui
