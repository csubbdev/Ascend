#include "error.hpp"

#include <sstream>

namespace ascend {

namespace {

std::string sourceLine(const std::string& source, int line) {
    size_t start = 0;
    for (int current = 1; current < line; ++current) {
        size_t newline = source.find('\n', start);
        if (newline == std::string::npos) return "";
        start = newline + 1;
    }
    size_t end = source.find('\n', start);
    std::string text = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

}  // namespace

std::string formatDiagnostic(const std::string& source, const std::string& filename, int line,
                             int column, const std::string& kind, const std::string& message) {
    std::ostringstream out;
    out << filename;
    if (line > 0) {
        out << ':' << line;
        if (column > 0) out << ':' << column;
    }
    out << ": " << kind << ": " << message << '\n';
    if (line <= 0) return out.str();

    std::string text = sourceLine(source, line);
    std::string gutter = std::to_string(line);
    out << ' ' << gutter << " | " << text << '\n';
    if (column > 0) {
        out << ' ' << std::string(gutter.size(), ' ') << " | ";
        // Columns count characters, not bytes, so skip UTF-8 continuation bytes.
        // Tabs are copied so the caret lines up with the source line above it.
        int current = 1;
        for (size_t i = 0; i < text.size() && current < column; ++i) {
            unsigned char ch = static_cast<unsigned char>(text[i]);
            if ((ch & 0xC0) == 0x80) continue;
            out << (ch == '\t' ? '\t' : ' ');
            ++current;
        }
        for (; current < column; ++current) out << ' ';
        out << "^\n";
    }
    return out.str();
}

}  // namespace ascend
