// More builtins: running programs, HTTP, paths and folders, text, math,
// sets, dates, CSV, base64, hashing and ids.

#include "interpreter.hpp"
#include "numeric.hpp"
#include "process.hpp"
#include "stdlib.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ascend {

namespace {

using Args = std::vector<Value>;

[[noreturn]] void typeError(const char* fn, size_t index, const char* expected, const Value& got) {
    throw RuntimeError(std::string(fn) + "() expects " + expected + " as argument " + std::to_string(index + 1) +
                       ", not " + aTypeName(got));
}

const std::string& stringArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isString()) typeError(fn, i, "a string", args[i]);
    return args[i].asString();
}

int64_t intArg(const Args& args, size_t i, const char* fn) {
    int64_t value = 0;
    if (!args[i].isInt() || !toInt64(args[i], value)) typeError(fn, i, "an integer", args[i]);
    return value;
}

double numberArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isNumber()) typeError(fn, i, "a number", args[i]);
    return args[i].toDouble();
}

const ListObj& listArg(const Args& args, size_t i, const char* fn) {
    if (!args[i].isList()) typeError(fn, i, "a list", args[i]);
    return args[i].asList();
}

Value stringList(Interpreter& in, const std::vector<std::string>& items) {
    std::vector<Value> values;
    values.reserve(items.size());
    for (const auto& item : items) values.push_back(Value::fromString(item));
    return Value::fromList(in.newList(std::move(values)));
}

void setField(MapObj& map, const char* key, Value value) { map.set(Value::fromString(key), std::move(value)); }

std::string pathString(const fs::path& path) { return path.generic_string(); }

[[noreturn]] void fileError(const char* fn, const std::string& path, const std::error_code& error) {
    throw RuntimeError(std::string(fn) + "(): '" + path + "': " + error.message());
}

// ---------------------------------------------------------------------------
// Running programs

Value capturedResult(Interpreter& in, const CapturedOutput& output) {
    auto result = in.newMap();
    setField(*result, "code", Value::fromInt(output.code));
    setField(*result, "stdout", Value::fromString(output.out));
    setField(*result, "stderr", Value::fromString(output.err));
    setField(*result, "ok", Value::fromBool(output.code == 0));
    return Value::fromMap(result);
}

void registerProcesses(Interpreter& interp) {
    // run(["git", "status"]) runs a program directly; run("ls | wc -l") uses the shell.
    interp.defineNative("run", 1, 2, [](Interpreter& in, Args& args) {
        std::string input = args.size() > 1 ? stringArg(args, 1, "run") : "";
        CapturedOutput output;
        if (args[0].isString()) {
            if (!captureShell(args[0].asString(), input, output)) throw RuntimeError("run(): couldn't start the shell");
            return capturedResult(in, output);
        }
        if (!args[0].isList()) typeError("run", 0, "a command string or a list of arguments", args[0]);
        std::vector<std::string> command;
        for (const Value& part : args[0].asList().items) command.push_back(toString(part));
        if (command.empty()) throw RuntimeError("run(): the command list is empty");
        if (!captureProcess(command, input, output)) {
            throw RuntimeError("run(): couldn't start '" + command[0] + "' (is it installed and on your PATH?)");
        }
        return capturedResult(in, output);
    });
}

// ---------------------------------------------------------------------------
// HTTP, through the curl program

void addHeaders(std::vector<std::string>& command, const Value& headers, const char* fn, bool& hasContentType) {
    if (headers.isNull()) return;
    if (!headers.isMap()) throw RuntimeError(std::string(fn) + "(): headers must be a map like {\"Accept\": \"text/html\"}");
    for (const auto& entry : headers.asMap().entries) {
        std::string name = toString(entry.first);
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "content-type") hasContentType = true;
        command.push_back("-H");
        command.push_back(name + ": " + toString(entry.second));
    }
}

Value httpRequest(Interpreter& in, const char* fn, const std::string& method, const std::string& url, const Value& body,
                  const Value& headers) {
    std::vector<std::string> command{"curl", "--silent", "--show-error", "--location", "--max-time", "120",
                                     "--request", method, "--write-out", "\n%{http_code}"};
    bool hasContentType = false;
    addHeaders(command, headers, fn, hasContentType);
    std::string input;
    if (!body.isNull()) {
        if (body.isString()) {
            input = body.asString();
        } else {
            input = jsonStringify(body, 0);  // maps and lists are sent as JSON
            if (!hasContentType) {
                command.push_back("-H");
                command.push_back("Content-Type: application/json");
            }
        }
        command.push_back("--data-binary");
        command.push_back("@-");
    }
    command.push_back("--");
    command.push_back(url);

    CapturedOutput output;
    if (!captureProcess(command, input, output)) {
        throw RuntimeError(std::string(fn) + "() needs the curl program, which wasn't found; install curl and try again");
    }
    if (output.code != 0) {
        std::string why = output.err;
        while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
        if (why.compare(0, 5, "curl:") == 0) why = why.substr(5);
        while (!why.empty() && why[0] == ' ') why.erase(0, 1);
        throw RuntimeError(std::string(fn) + "(): request to " + url + " failed: " + (why.empty() ? "curl error" : why));
    }
    size_t split = output.out.rfind('\n');
    std::string text = split == std::string::npos ? "" : output.out.substr(0, split);
    int status = std::atoi(output.out.c_str() + (split == std::string::npos ? 0 : split + 1));
    auto result = in.newMap();
    setField(*result, "status", Value::fromInt(status));
    setField(*result, "ok", Value::fromBool(status >= 200 && status < 300));
    setField(*result, "body", Value::fromString(text));
    return Value::fromMap(result);
}

void registerHttp(Interpreter& interp) {
    interp.defineNative("http_get", 1, 2, [](Interpreter& in, Args& args) {
        return httpRequest(in, "http_get", "GET", stringArg(args, 0, "http_get"), Value(),
                           args.size() > 1 ? args[1] : Value());
    });
    interp.defineNative("http_post", 2, 3, [](Interpreter& in, Args& args) {
        return httpRequest(in, "http_post", "POST", stringArg(args, 0, "http_post"), args[1],
                           args.size() > 2 ? args[2] : Value());
    });
    interp.defineNative("http_request", 2, 4, [](Interpreter& in, Args& args) {
        std::string method = stringArg(args, 0, "http_request");
        for (char& c : method) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return httpRequest(in, "http_request", method, stringArg(args, 1, "http_request"),
                           args.size() > 2 ? args[2] : Value(), args.size() > 3 ? args[3] : Value());
    });
}

// ---------------------------------------------------------------------------
// Paths, folders and the environment

void registerPaths(Interpreter& interp) {
    interp.defineNative("path_join", 1, -1, [](Interpreter&, Args& args) {
        fs::path path;
        for (size_t i = 0; i < args.size(); ++i) path /= stringArg(args, i, "path_join");
        return Value::fromString(pathString(path));
    });
    interp.defineNative("basename", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(fs::path(stringArg(args, 0, "basename")).filename().string());
    });
    interp.defineNative("dirname", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(pathString(fs::path(stringArg(args, 0, "dirname")).parent_path()));
    });
    interp.defineNative("extension", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(fs::path(stringArg(args, 0, "extension")).extension().string());
    });
    interp.defineNative("abs_path", 1, 1, [](Interpreter&, Args& args) {
        std::error_code error;
        fs::path path = fs::absolute(stringArg(args, 0, "abs_path"), error);
        if (error) fileError("abs_path", args[0].asString(), error);
        return Value::fromString(pathString(path.lexically_normal()));
    });
    interp.defineNative("cwd", 0, 0, [](Interpreter&, Args&) {
        std::error_code error;
        fs::path path = fs::current_path(error);
        if (error) throw RuntimeError("cwd(): " + error.message());
        return Value::fromString(pathString(path));
    });
    interp.defineNative("chdir", 1, 1, [](Interpreter&, Args& args) {
        std::error_code error;
        fs::current_path(stringArg(args, 0, "chdir"), error);
        if (error) fileError("chdir", args[0].asString(), error);
        return Value();
    });
    interp.defineNative("temp_dir", 0, 0, [](Interpreter&, Args&) {
        std::error_code error;
        fs::path path = fs::temp_directory_path(error);
        if (error) throw RuntimeError("temp_dir(): " + error.message());
        return Value::fromString(pathString(path));
    });
    interp.defineNative("file_size", 1, 1, [](Interpreter&, Args& args) {
        std::error_code error;
        uintmax_t size = fs::file_size(stringArg(args, 0, "file_size"), error);
        if (error) fileError("file_size", args[0].asString(), error);
        return Value::fromInt(static_cast<int64_t>(size));
    });
    interp.defineNative("copy_file", 2, 2, [](Interpreter&, Args& args) {
        std::error_code error;
        fs::copy_file(stringArg(args, 0, "copy_file"), stringArg(args, 1, "copy_file"),
                      fs::copy_options::overwrite_existing, error);
        if (error) fileError("copy_file", args[0].asString(), error);
        return Value();
    });
    interp.defineNative("move_file", 2, 2, [](Interpreter&, Args& args) {
        std::error_code error;
        fs::rename(stringArg(args, 0, "move_file"), stringArg(args, 1, "move_file"), error);
        if (error) fileError("move_file", args[0].asString(), error);
        return Value();
    });
    interp.defineNative("remove_dir", 1, 1, [](Interpreter&, Args& args) {
        const std::string& path = stringArg(args, 0, "remove_dir");
        std::error_code error;
        if (!fs::is_directory(path, error)) return Value::fromBool(false);
        uintmax_t removed = fs::remove_all(path, error);
        if (error) fileError("remove_dir", path, error);
        return Value::fromBool(removed > 0);
    });
    interp.defineNative("walk_dir", 0, 1, [](Interpreter& in, Args& args) {
        // Every file below a folder, as paths relative to it, sorted.
        std::string root = args.empty() ? "." : stringArg(args, 0, "walk_dir");
        std::error_code error;
        std::vector<std::string> files;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
        for (; !error && it != end; it.increment(error)) {
            if (it->is_regular_file()) files.push_back(pathString(it->path().lexically_relative(root)));
        }
        if (error) fileError("walk_dir", root, error);
        std::sort(files.begin(), files.end());
        return stringList(in, files);
    });
    interp.defineNative("set_env", 2, 2, [](Interpreter&, Args& args) {
        setEnvironmentVariable(stringArg(args, 0, "set_env"), toString(args[1]));
        return Value();
    });
    interp.defineNative("platform", 0, 0, [](Interpreter&, Args&) {
#if defined(_WIN32)
        return Value::fromString("windows");
#elif defined(__APPLE__)
        return Value::fromString("macos");
#elif defined(__linux__)
        return Value::fromString("linux");
#else
        return Value::fromString("unix");
#endif
    });
}

// ---------------------------------------------------------------------------
// Text

void registerText(Interpreter& interp) {
    interp.defineNative("capitalize", 1, 1, [](Interpreter&, Args& args) {
        std::string text = stringArg(args, 0, "capitalize");
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return Value::fromString(text);
    });
    interp.defineNative("title", 1, 1, [](Interpreter&, Args& args) {
        std::string text = stringArg(args, 0, "title");
        bool start = true;
        for (char& c : text) {
            unsigned char byte = static_cast<unsigned char>(c);
            c = static_cast<char>(start ? std::toupper(byte) : std::tolower(byte));
            start = std::isspace(byte) != 0 || c == '-' || c == '_';
        }
        return Value::fromString(text);
    });
    interp.defineNative("trim_start", 1, 1, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "trim_start");
        size_t first = text.find_first_not_of(" \t\r\n\f\v");
        return Value::fromString(first == std::string::npos ? "" : text.substr(first));
    });
    interp.defineNative("trim_end", 1, 1, [](Interpreter&, Args& args) {
        const std::string& text = stringArg(args, 0, "trim_end");
        size_t last = text.find_last_not_of(" \t\r\n\f\v");
        return Value::fromString(last == std::string::npos ? "" : text.substr(0, last + 1));
    });
    interp.defineNative("last_index_of", 2, 2, [](Interpreter&, Args& args) {
        if (args[0].isList()) {
            const auto& items = args[0].asList().items;
            for (size_t i = items.size(); i > 0; --i) {
                if (valuesEqual(items[i - 1], args[1])) return Value::fromInt(static_cast<int64_t>(i - 1));
            }
            return Value::fromInt(-1);
        }
        const std::string& text = stringArg(args, 0, "last_index_of");
        std::string part = args[1].isChar() ? encodeUtf8(args[1].asChar()) : stringArg(args, 1, "last_index_of");
        size_t found = text.rfind(part);
        if (found == std::string::npos) return Value::fromInt(-1);
        return Value::fromInt(static_cast<int64_t>(utf8Length(text.substr(0, found))));
    });
}

// ---------------------------------------------------------------------------
// Math and sets

void registerMathExtras(Interpreter& interp) {
    interp.defineNative("lcm", 2, 2, [](Interpreter&, Args& args) {
        int64_t a = intArg(args, 0, "lcm");
        int64_t b = intArg(args, 1, "lcm");
        if (a == 0 || b == 0) return Value::fromInt(0);
        if (a == std::numeric_limits<int64_t>::min() || b == std::numeric_limits<int64_t>::min()) {
            throw RuntimeError("lcm(): the numbers are too large");
        }
        a = a < 0 ? -a : a;
        b = b < 0 ? -b : b;
        int64_t x = a;
        int64_t y = b;
        while (y != 0) {
            int64_t t = x % y;
            x = y;
            y = t;
        }
        int64_t factor = a / x;
        if (factor > std::numeric_limits<int64_t>::max() / b) throw RuntimeError("lcm(): the result is too large for an int");
        return Value::fromInt(factor * b);
    });
    interp.defineNative("factorial", 1, 1, [](Interpreter&, Args& args) {
        int64_t n = intArg(args, 0, "factorial");
        if (n < 0) throw RuntimeError("factorial() needs a number that isn't negative");
        if (n > 20) throw RuntimeError("factorial(" + std::to_string(n) + ") is too large for an int (the most is 20)");
        int64_t result = 1;
        for (int64_t i = 2; i <= n; ++i) result *= i;
        return Value::fromInt(result);
    });
    interp.defineNative("degrees", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromFloat(numberArg(args, 0, "degrees") * 180.0 / 3.14159265358979323846);
    });
    interp.defineNative("radians", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromFloat(numberArg(args, 0, "radians") * 3.14159265358979323846 / 180.0);
    });
    interp.defineNative("trunc", 1, 1, [](Interpreter&, Args& args) {
        if (args[0].isInt()) return args[0];
        double value = std::trunc(numberArg(args, 0, "trunc"));
        if (!(value >= -9.2233720368547758e18 && value < 9.2233720368547758e18)) {
            throw RuntimeError("trunc(): " + toString(args[0]) + " is too large for an int");
        }
        return Value::fromInt(static_cast<int64_t>(value));
    });

    // Set operations on lists. The results keep the order of the first list
    // and have no duplicates.
    auto setOperation = [](const char* name, int mode) {
        return [name, mode](Interpreter& in, Args& args) {
            const auto& a = listArg(args, 0, name).items;
            const auto& b = listArg(args, 1, name).items;
            auto has = [](const std::vector<Value>& items, const Value& value) {
                return std::any_of(items.begin(), items.end(), [&](const Value& item) { return valuesEqual(item, value); });
            };
            std::vector<Value> result;
            auto add = [&](const Value& value) {
                if (!has(result, value)) result.push_back(value);
            };
            for (const Value& item : a) {
                if (mode == 0 || (mode == 1 && has(b, item)) || (mode == 2 && !has(b, item))) add(item);
            }
            if (mode == 0) {
                for (const Value& item : b) add(item);
            }
            return Value::fromList(in.newList(std::move(result)));
        };
    };
    interp.defineNative("union", 2, 2, setOperation("union", 0));
    interp.defineNative("intersect", 2, 2, setOperation("intersect", 1));
    interp.defineNative("difference", 2, 2, setOperation("difference", 2));
}

// ---------------------------------------------------------------------------
// Dates

std::tm localTime(double seconds) {
    std::time_t time = static_cast<std::time_t>(std::floor(seconds));
    std::tm parts{};
#if defined(_WIN32)
    if (localtime_s(&parts, &time) != 0) throw RuntimeError("the time " + formatFloat(seconds) + " is out of range");
#else
    if (localtime_r(&time, &parts) == nullptr) throw RuntimeError("the time " + formatFloat(seconds) + " is out of range");
#endif
    return parts;
}

void registerDates(Interpreter& interp) {
    // now(): the local date and time as a map.
    interp.defineNative("now", 0, 0, [](Interpreter& in, Args&) {
        double seconds = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::tm parts = localTime(seconds);
        auto result = in.newMap();
        setField(*result, "year", Value::fromInt(parts.tm_year + 1900));
        setField(*result, "month", Value::fromInt(parts.tm_mon + 1));
        setField(*result, "day", Value::fromInt(parts.tm_mday));
        setField(*result, "hour", Value::fromInt(parts.tm_hour));
        setField(*result, "minute", Value::fromInt(parts.tm_min));
        setField(*result, "second", Value::fromInt(parts.tm_sec));
        setField(*result, "weekday", Value::fromInt(parts.tm_wday));  // 0 is Sunday
        setField(*result, "yearday", Value::fromInt(parts.tm_yday + 1));
        setField(*result, "timestamp", Value::fromFloat(seconds));
        return Value::fromMap(result);
    });
    // format_time(seconds, format?): a timestamp (like time()) as local time text.
    interp.defineNative("format_time", 1, 2, [](Interpreter&, Args& args) {
        double seconds = numberArg(args, 0, "format_time");
        std::string format = args.size() > 1 ? stringArg(args, 1, "format_time") : "%Y-%m-%d %H:%M:%S";
        if (format.empty()) return Value::fromString("");
        std::tm parts = localTime(seconds);
        char buffer[512];
        size_t length = std::strftime(buffer, sizeof buffer, format.c_str(), &parts);
        if (length == 0) throw RuntimeError("format_time(): the format gives no text, or too much");
        return Value::fromString(std::string(buffer, length));
    });
}

// ---------------------------------------------------------------------------
// CSV

Value parseCsv(Interpreter& in, const std::string& text, char separator) {
    std::vector<Value> rows;
    std::vector<Value> row;
    std::string field;
    bool quoted = false;
    bool fieldStarted = false;
    auto endField = [&] {
        row.push_back(Value::fromString(field));
        field.clear();
        fieldStarted = false;
    };
    auto endRow = [&] {
        endField();
        rows.push_back(Value::fromList(in.newList(std::move(row))));
        row.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
                field += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"' && !fieldStarted && field.empty()) {
            quoted = true;
            fieldStarted = true;
        } else if (c == separator) {
            endField();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            endRow();
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (quoted) throw RuntimeError("csv_parse(): a quoted field is never closed");
    if (fieldStarted || !field.empty() || !row.empty()) endRow();
    return Value::fromList(in.newList(std::move(rows)));
}

std::string csvField(const std::string& text, char separator) {
    bool needsQuotes = text.find_first_of(std::string("\"\r\n") + separator) != std::string::npos ||
                       (!text.empty() && (text.front() == ' ' || text.back() == ' '));
    if (!needsQuotes) return text;
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

char separatorArg(const Args& args, size_t i, const char* fn) {
    if (args.size() <= i) return ',';
    std::string text = args[i].isChar() ? encodeUtf8(args[i].asChar()) : stringArg(args, i, fn);
    if (text.size() != 1) throw RuntimeError(std::string(fn) + "(): the separator must be one character");
    return text[0];
}

void registerCsv(Interpreter& interp) {
    interp.defineNative("csv_parse", 1, 2, [](Interpreter& in, Args& args) {
        return parseCsv(in, stringArg(args, 0, "csv_parse"), separatorArg(args, 1, "csv_parse"));
    });
    interp.defineNative("csv_stringify", 1, 2, [](Interpreter&, Args& args) {
        char separator = separatorArg(args, 1, "csv_stringify");
        std::string out;
        for (const Value& row : listArg(args, 0, "csv_stringify").items) {
            if (!row.isList()) throw RuntimeError("csv_stringify() needs a list of rows, each a list of values");
            bool first = true;
            for (const Value& value : row.asList().items) {
                if (!first) out += separator;
                first = false;
                out += csvField(value.isNull() ? "" : toString(value), separator);
            }
            out += '\n';
        }
        return Value::fromString(out);
    });
}

// ---------------------------------------------------------------------------
// Encodings, hashing and ids

const char* kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& data) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) |
                     static_cast<unsigned char>(data[i + 2]);
        out += kBase64[(n >> 18) & 63];
        out += kBase64[(n >> 12) & 63];
        out += kBase64[(n >> 6) & 63];
        out += kBase64[n & 63];
    }
    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(data[i]) << 16);
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1]) << 8);
        out += kBase64[(n >> 18) & 63];
        out += kBase64[(n >> 12) & 63];
        out += i + 1 < data.size() ? kBase64[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string base64Decode(const std::string& text) {
    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const char* found = std::strchr(kBase64, c);
        if (found == nullptr || c == '\0') throw RuntimeError("base64_decode(): '" + std::string(1, c) + "' isn't base64");
        buffer = (buffer << 6) | static_cast<uint32_t>(found - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

std::string sha256(const std::string& message) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };

    std::string data = message;
    uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8;
    data += static_cast<char>(0x80);
    while (data.size() % 64 != 56) data += '\0';
    for (int i = 7; i >= 0; --i) data += static_cast<char>((bitLength >> (i * 8)) & 0xFF);

    for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(data[chunk + 4 * i])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(data[chunk + 4 * i + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(data[chunk + 4 * i + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<unsigned char>(data[chunk + 4 * i + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint32_t word : h) {
        for (int shift = 28; shift >= 0; shift -= 4) out += digits[(word >> shift) & 0xF];
    }
    return out;
}

void registerEncodings(Interpreter& interp) {
    interp.defineNative("base64_encode", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(base64Encode(stringArg(args, 0, "base64_encode")));
    });
    interp.defineNative("base64_decode", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(base64Decode(stringArg(args, 0, "base64_decode")));
    });
    interp.defineNative("sha256", 1, 1, [](Interpreter&, Args& args) {
        return Value::fromString(sha256(stringArg(args, 0, "sha256")));
    });
    interp.defineNative("uuid", 0, 0, [](Interpreter& in, Args&) {
        // A random (version 4) UUID.
        std::uniform_int_distribution<int> byte(0, 255);
        unsigned char bytes[16];
        for (auto& b : bytes) b = static_cast<unsigned char>(byte(in.random()));
        bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
        bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
            out += digits[bytes[i] >> 4];
            out += digits[bytes[i] & 0xF];
        }
        return Value::fromString(out);
    });
}

}  // namespace

void registerExtraLibrary(Interpreter& interpreter) {
    registerProcesses(interpreter);
    registerHttp(interpreter);
    registerPaths(interpreter);
    registerText(interpreter);
    registerMathExtras(interpreter);
    registerDates(interpreter);
    registerCsv(interpreter);
    registerEncodings(interpreter);
}

}  // namespace ascend
