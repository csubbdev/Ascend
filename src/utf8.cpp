#include "utf8.hpp"

namespace ascend {

namespace {

bool isAscii(const std::string& text) {
    for (char c : text) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
    return true;
}

}  // namespace

bool isValidCodePoint(char32_t cp) { return cp <= 0x10FFFF && (cp < 0xD800 || cp > 0xDFFF); }

void appendUtf8(std::string& out, char32_t cp) {
    if (!isValidCodePoint(cp)) cp = kReplacementChar;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string encodeUtf8(char32_t cp) {
    std::string out;
    appendUtf8(out, cp);
    return out;
}

char32_t decodeUtf8(const std::string& text, size_t& pos) {
    unsigned char lead = static_cast<unsigned char>(text[pos]);
    if (lead < 0x80) {
        ++pos;
        return lead;
    }

    int length;
    char32_t cp;
    char32_t minimum;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        cp = lead & 0x1F;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        cp = lead & 0x0F;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        cp = lead & 0x07;
        minimum = 0x10000;
    } else {
        ++pos;
        return kReplacementChar;
    }

    if (pos + static_cast<size_t>(length) > text.size()) {
        ++pos;
        return kReplacementChar;
    }
    for (int i = 1; i < length; ++i) {
        unsigned char next = static_cast<unsigned char>(text[pos + static_cast<size_t>(i)]);
        if ((next & 0xC0) != 0x80) {
            ++pos;
            return kReplacementChar;
        }
        cp = (cp << 6) | (next & 0x3F);
    }
    if (cp < minimum || !isValidCodePoint(cp)) {
        ++pos;
        return kReplacementChar;
    }
    pos += static_cast<size_t>(length);
    return cp;
}

size_t utf8Length(const std::string& text) {
    if (isAscii(text)) return text.size();
    size_t count = 0;
    for (size_t pos = 0; pos < text.size(); ++count) decodeUtf8(text, pos);
    return count;
}

size_t utf8Offset(const std::string& text, size_t index) {
    if (isAscii(text)) return index < text.size() ? index : text.size();
    size_t pos = 0;
    for (size_t i = 0; i < index && pos < text.size(); ++i) decodeUtf8(text, pos);
    return pos;
}

}  // namespace ascend
