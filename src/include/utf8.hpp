#ifndef ASCEND_UTF8_HPP
#define ASCEND_UTF8_HPP

#include <cstddef>
#include <string>

namespace ascend {

// Strings are stored as UTF-8. Lengths, indexes and slices count characters
// (Unicode code points), not bytes. Invalid bytes count as one character each
// and decode as U+FFFD.

constexpr char32_t kReplacementChar = 0xFFFD;

bool isValidCodePoint(char32_t cp);
void appendUtf8(std::string& out, char32_t cp);
std::string encodeUtf8(char32_t cp);

// Decodes the character starting at byte `pos` and moves `pos` past it.
char32_t decodeUtf8(const std::string& text, size_t& pos);

size_t utf8Length(const std::string& text);

// Byte offset of character number `index` (text.size() if index == length).
size_t utf8Offset(const std::string& text, size_t index);

}  // namespace ascend

#endif  // ASCEND_UTF8_HPP
