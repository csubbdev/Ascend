// json_parse() and json_stringify().

#include "interpreter.hpp"
#include "numeric.hpp"
#include "stdlib.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ascend {

namespace {

constexpr int kMaxJsonDepth = 500;

class JsonParser {
public:
    JsonParser(Interpreter& interpreter, const std::string& text) : interpreter_(interpreter), text_(text) {}

    Value parse() {
        skipSpace();
        Value value = parseValue(0);
        skipSpace();
        if (pos_ < text_.size()) fail("unexpected text after the JSON value");
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& message) const {
        int line = 1;
        int column = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw RuntimeError("json_parse(): " + message + " (line " + std::to_string(line) + ", column " +
                           std::to_string(column) + ")");
    }

    void skipSpace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consumeWord(const char* word) {
        size_t length = std::char_traits<char>::length(word);
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        return true;
    }

    Value parseValue(int depth) {
        if (depth > kMaxJsonDepth) fail("the JSON is nested too deeply");
        if (pos_ >= text_.size()) fail("unexpected end of input");
        char c = text_[pos_];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return Value::fromString(parseString());
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        if (consumeWord("true")) return Value::fromBool(true);
        if (consumeWord("false")) return Value::fromBool(false);
        if (consumeWord("null")) return Value();
        fail(std::string("unexpected character '") + c + "'");
    }

    Value parseObject(int depth) {
        ++pos_;  // {
        auto map = interpreter_.newMap();
        Value result = Value::fromMap(map);
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return result;
        }
        while (true) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected a string key");
            std::string key = parseString();
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') fail("expected ':' after the key");
            ++pos_;
            skipSpace();
            map->set(Value::fromString(key), parseValue(depth + 1));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return result;
            }
            fail("expected ',' or '}' in the object");
        }
    }

    Value parseArray(int depth) {
        ++pos_;  // [
        auto list = interpreter_.newList();
        Value result = Value::fromList(list);
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return result;
        }
        while (true) {
            skipSpace();
            list->items.push_back(parseValue(depth + 1));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return result;
            }
            fail("expected ',' or ']' in the array");
        }
    }

    unsigned hex4() {
        if (pos_ + 4 > text_.size()) fail("incomplete \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("invalid \\u escape");
            }
        }
        return value;
    }

    std::string parseString() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) fail("unterminated string");
            char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    char32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && text_.compare(pos_, 2, "\\u") == 0) {
                        pos_ += 2;
                        char32_t low = hex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            cp = kReplacementChar;
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail(std::string("invalid escape '\\") + e + "'");
            }
        }
    }

    Value parseNumber() {
        size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        bool isFloat = false;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || ((c == '+' || c == '-') && isFloat)) {
                isFloat = true;
                ++pos_;
            } else {
                break;
            }
        }
        std::string number = text_.substr(start, pos_ - start);
        if (number == "-") fail("invalid number");
        if (!isFloat) {
            errno = 0;
            char* end = nullptr;
            long long value = std::strtoll(number.c_str(), &end, 10);
            if (errno != ERANGE && end == number.c_str() + number.size()) return Value::fromInt(value);
        }
        char* end = nullptr;
        double value = std::strtod(number.c_str(), &end);
        if (end != number.c_str() + number.size()) fail("invalid number '" + number + "'");
        return Value::fromFloat(value);
    }

    Interpreter& interpreter_;
    const std::string& text_;
    size_t pos_ = 0;
};

void writeJsonString(std::string& out, const std::string& text) {
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned char>(c));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void writeJson(std::string& out, const Value& value, int indent, int level, std::vector<const void*>& active) {
    auto newline = [&](int depth) {
        if (indent <= 0) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * depth), ' ');
    };
    switch (value.type()) {
        case Value::Type::Null: out += "null"; return;
        case Value::Type::Bool: out += value.asBool() ? "true" : "false"; return;
        case Value::Type::Int: out += intToString(value); return;
        case Value::Type::Float:
            if (!std::isfinite(value.asFloat())) {
                throw RuntimeError("json_stringify(): JSON can't represent " + toString(value));
            }
            out += formatFloat(value.asFloat(), value.numKind() == NumKind::F32);
            return;
        case Value::Type::Char: writeJsonString(out, encodeUtf8(value.asChar())); return;
        case Value::Type::String: writeJsonString(out, value.asString()); return;
        case Value::Type::Function: throw RuntimeError("json_stringify(): functions can't be converted to JSON");
        case Value::Type::List:
        case Value::Type::Map: break;
    }

    const void* identity = value.isList() ? static_cast<const void*>(value.listPtr().get())
                                          : static_cast<const void*>(value.mapPtr().get());
    if (std::find(active.begin(), active.end(), identity) != active.end()) {
        throw RuntimeError("json_stringify(): the value contains itself, which JSON can't represent");
    }
    if (active.size() > static_cast<size_t>(kMaxJsonDepth)) throw RuntimeError("json_stringify(): value is nested too deeply");
    active.push_back(identity);

    if (value.isList()) {
        const auto& items = value.asList().items;
        out += '[';
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) out += ',';
            newline(level + 1);
            writeJson(out, items[i], indent, level + 1, active);
        }
        if (!items.empty()) newline(level);
        out += ']';
    } else {
        const auto& entries = value.asMap().entries;
        out += '{';
        for (size_t i = 0; i < entries.size(); ++i) {
            const Value& key = entries[i].first;
            if (!key.isString() && !key.isChar() && !key.isNumber() && !key.isBool()) {
                throw RuntimeError(std::string("json_stringify(): a ") + key.typeName() + " can't be a JSON object key");
            }
            if (i > 0) out += ',';
            newline(level + 1);
            writeJsonString(out, toString(key));
            out += indent > 0 ? ": " : ":";
            writeJson(out, entries[i].second, indent, level + 1, active);
        }
        if (!entries.empty()) newline(level);
        out += '}';
    }
    active.pop_back();
}

}  // namespace

Value jsonParse(Interpreter& interpreter, const std::string& text) { return JsonParser(interpreter, text).parse(); }

std::string jsonStringify(const Value& value, int indent) {
    std::string out;
    std::vector<const void*> active;
    writeJson(out, value, indent, 0, active);
    return out;
}

}  // namespace ascend
