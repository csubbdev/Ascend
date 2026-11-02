#include "lexer.hpp"

#include "utf8.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace ascend {

namespace {

const std::unordered_map<std::string, TokenType>& keywordTable() {
    static const std::unordered_map<std::string, TokenType> table = {
        {"and", TokenType::And},           {"or", TokenType::Or},
        {"not", TokenType::Not},           {"if", TokenType::If},
        {"else", TokenType::Else},         {"while", TokenType::While},
        {"for", TokenType::For},           {"in", TokenType::In},
        {"break", TokenType::Break},       {"continue", TokenType::Continue},
        {"return", TokenType::Return},     {"func", TokenType::Func},
        {"true", TokenType::True},         {"false", TokenType::False},
        {"null", TokenType::Null},         {"try", TokenType::Try},
        {"catch", TokenType::Catch},       {"throw", TokenType::Throw},
        {"const", TokenType::Const},       {"match", TokenType::Match},
    };
    return table;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

int digitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}

}  // namespace

std::string describeToken(const Token& token) {
    switch (token.type) {
        case TokenType::Newline: return "end of line";
        case TokenType::Eof: return "end of input";
        case TokenType::Identifier: return "identifier '" + token.lexeme + "'";
        case TokenType::Int:
        case TokenType::Float: return "number " + token.lexeme;
        case TokenType::String: return "string " + token.lexeme;
        case TokenType::Char: return "char " + token.lexeme;
        case TokenType::FStringStart: return "f-string";
        case TokenType::FStringEnd: return "end of the f-string";
        case TokenType::InterpStart: return "'{'";
        case TokenType::InterpEnd: return "'}'";
        case TokenType::FormatSpec: return "format spec";
        default: return "'" + token.lexeme + "'";
    }
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

std::vector<Token> Lexer::tokenize() {
    tokens_.clear();
    errors_.clear();
    brackets_.clear();
    pos_ = 0;
    line_ = 1;
    column_ = 1;
    endedInsideComment_ = false;

    while (true) {
        skipTrivia();
        if (atEnd()) break;
        scanToken();
    }

    // Place the end-of-input token right after the last real token so errors
    // like "expected '}'" point somewhere useful.
    Token eof;
    eof.type = TokenType::Eof;
    eof.line = line_;
    eof.column = column_;
    for (auto it = tokens_.rbegin(); it != tokens_.rend(); ++it) {
        if (it->type != TokenType::Newline) {
            eof.line = it->line;
            eof.column = it->column + static_cast<int>(utf8Length(it->lexeme));
            break;
        }
    }
    tokens_.push_back(eof);
    return std::move(tokens_);
}

char Lexer::peek(size_t ahead) const {
    size_t index = pos_ + ahead;
    return index < source_.size() ? source_[index] : '\0';
}

bool Lexer::atEnd() const { return pos_ >= source_.size(); }

char Lexer::advance() {
    char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
        ++column_;  // count characters, not UTF-8 continuation bytes
    }
    return c;
}

bool Lexer::match(char expected) {
    if (atEnd() || source_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::error(int line, int column, std::string message) {
    errors_.push_back({line, column, std::move(message)});
}

void Lexer::emitNewline() {
    if (!brackets_.empty() && brackets_.back() != '{') return;  // inside ( ) or [ ]
    if (tokens_.empty() || tokens_.back().type == TokenType::Newline) return;
    addAt(TokenType::Newline, line_, column_, "\n");
}

void Lexer::closeBracket() {
    if (!brackets_.empty()) brackets_.pop_back();
}

void Lexer::skipTrivia() {
    while (!atEnd()) {
        char c = peek();
        if (c == '\n') {
            emitNewline();
            advance();
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (!atEnd() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            blockComment();
        } else {
            return;
        }
    }
}

void Lexer::blockComment() {
    int startLine = line_;
    int startColumn = column_;
    advance();  // '/'
    advance();  // '*'
    while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            return;
        }
        if (peek() == '\n') emitNewline();
        advance();
    }
    endedInsideComment_ = true;
    error(startLine, startColumn, "unterminated block comment (missing '*/')");
}

void Lexer::add(TokenType type) { addAt(type, startLine_, startColumn_, source_.substr(start_, pos_ - start_)); }

void Lexer::addAt(TokenType type, int line, int column, std::string lexeme) {
    Token token;
    token.type = type;
    token.lexeme = std::move(lexeme);
    token.line = line;
    token.column = column;
    tokens_.push_back(std::move(token));
}

void Lexer::scanToken() {
    start_ = pos_;
    startLine_ = line_;
    startColumn_ = column_;

    char c = advance();
    if (c == 'f' && peek() == '"') {
        fString();
        return;
    }
    if (isIdentifierStart(c)) {
        identifier();
        return;
    }
    if (isDigit(c)) {
        number(c);
        return;
    }

    switch (c) {
        case '(': brackets_.push_back('('); add(TokenType::LeftParen); return;
        case ')': closeBracket(); add(TokenType::RightParen); return;
        case '[': brackets_.push_back('['); add(TokenType::LeftBracket); return;
        case ']': closeBracket(); add(TokenType::RightBracket); return;
        case '{': brackets_.push_back('{'); add(TokenType::LeftBrace); return;
        case '}': closeBracket(); add(TokenType::RightBrace); return;
        case ',': add(TokenType::Comma); return;
        case '.': add(TokenType::Dot); return;
        case ';': add(TokenType::Semicolon); return;
        case '?': add(TokenType::Question); return;
        case '~': add(TokenType::Tilde); return;
        case ':': add(match('=') ? TokenType::Declare : TokenType::Colon); return;
        case '+':
            if (match('+')) {
                add(TokenType::PlusPlus);
            } else {
                add(match('=') ? TokenType::PlusEqual : TokenType::Plus);
            }
            return;
        case '-':
            if (match('>')) {
                add(TokenType::Arrow);
            } else if (match('-')) {
                add(TokenType::MinusMinus);
            } else {
                add(match('=') ? TokenType::MinusEqual : TokenType::Minus);
            }
            return;
        case '*':
            if (match('*')) {
                add(TokenType::StarStar);
            } else {
                add(match('=') ? TokenType::StarEqual : TokenType::Star);
            }
            return;
        case '/': add(match('=') ? TokenType::SlashEqual : TokenType::Slash); return;
        case '%': add(match('=') ? TokenType::PercentEqual : TokenType::Percent); return;
        case '^': add(match('=') ? TokenType::CaretEqual : TokenType::Caret); return;
        case '!': add(match('=') ? TokenType::NotEqual : TokenType::Not); return;
        case '=':
            if (match('>')) {
                add(TokenType::FatArrow);
            } else {
                add(match('=') ? TokenType::EqualEqual : TokenType::Equal);
            }
            return;
        case '<':
            if (match('<')) {
                add(match('=') ? TokenType::ShiftLeftEqual : TokenType::ShiftLeft);
            } else {
                add(match('=') ? TokenType::LessEqual : TokenType::Less);
            }
            return;
        case '>':
            if (match('>')) {
                add(match('=') ? TokenType::ShiftRightEqual : TokenType::ShiftRight);
            } else {
                add(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
            }
            return;
        case '&':
            if (match('&')) {
                add(TokenType::And);
            } else {
                add(match('=') ? TokenType::AmpersandEqual : TokenType::Ampersand);
            }
            return;
        case '|':
            if (match('|')) {
                add(TokenType::Or);
            } else {
                add(match('=') ? TokenType::PipeEqual : TokenType::Pipe);
            }
            return;
        case '"':
            string(c);
            return;
        case '\'':
            character();
            return;
        default:
            break;
    }

    std::string shown;
    unsigned char byte = static_cast<unsigned char>(c);
    if (byte >= 0x80) {
        // Consume the rest of the UTF-8 sequence so one character gives one error.
        while (!atEnd() && (static_cast<unsigned char>(peek()) & 0xC0) == 0x80) advance();
        shown = source_.substr(start_, pos_ - start_);
    } else if (std::isprint(byte)) {
        shown = std::string(1, c);
    } else {
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "\\x%02x", byte);
        shown = buffer;
    }
    error(startLine_, startColumn_, "unexpected character '" + shown + "'");
}

void Lexer::identifier() {
    while (isIdentifierChar(peek())) advance();
    auto it = keywordTable().find(source_.substr(start_, pos_ - start_));
    add(it == keywordTable().end() ? TokenType::Identifier : it->second);
}

// Numbers: 42, 1_000, 3.14, 1e9, 0xFF, 0b1010, 0o17, with an optional type
// suffix: 200u8, 7i64, 1.5f32.
void Lexer::number(char first) {
    int base = 10;
    if (first == '0' && (peek() == 'x' || peek() == 'X' || peek() == 'b' || peek() == 'B' || peek() == 'o' ||
                         peek() == 'O')) {
        char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(advance())));
        base = prefix == 'x' ? 16 : (prefix == 'b' ? 2 : 8);
    }

    std::string digits;
    if (base == 10) digits += first;
    bool isFloat = false;
    auto readDigits = [&](int digitBase) {
        while (true) {
            char c = peek();
            if (c == '_' && digitValue(peek(1)) < digitBase) {
                advance();
                continue;
            }
            if (digitValue(c) >= digitBase) return;
            // In decimal numbers, 'e' is an exponent and other letters are suffixes.
            if (digitBase == 10 && !isDigit(c)) return;
            digits += advance();
        }
    };
    readDigits(base);

    if (base == 10) {
        if (peek() == '.' && isDigit(peek(1))) {
            isFloat = true;
            digits += advance();
            readDigits(10);
        }
        if ((peek() == 'e' || peek() == 'E') &&
            (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
            isFloat = true;
            digits += advance();
            if (peek() == '+' || peek() == '-') digits += advance();
            readDigits(10);
        }
    } else if (digits.empty() || isDigit(peek())) {
        while (isIdentifierChar(peek())) advance();
        error(startLine_, startColumn_, "invalid number '" + source_.substr(start_, pos_ - start_) + "'");
        return;
    }

    // Optional type suffix, possibly after an underscore: 255u8 or 255_u8.
    NumKind kind = isFloat ? NumKind::Float : NumKind::Int;
    bool hasSuffix = false;
    if (peek() == '_' && isIdentifierStart(peek(1))) advance();
    if (isIdentifierStart(peek())) {
        size_t suffixStart = pos_;
        while (isIdentifierChar(peek())) advance();
        std::string suffix = source_.substr(suffixStart, pos_ - suffixStart);
        TypeSpec spec;
        bool validSuffix = lookupType(suffix, spec) && spec.kind == TypeKind::Number && suffix != "int" &&
                           suffix != "float" && suffix != "byte";
        if (!validSuffix) {
            error(startLine_, startColumn_, "invalid number '" + source_.substr(start_, pos_ - start_) + "'");
            return;
        }
        if (isFloat && isIntKind(spec.num)) {
            error(startLine_, startColumn_,
                  "'" + suffix + "' is an integer type, but " + source_.substr(start_, suffixStart - start_) +
                      " has a fraction or exponent");
            return;
        }
        if (base != 10 && isFloatKind(spec.num)) {
            error(startLine_, startColumn_, "hex, binary and octal numbers can't have a float suffix");
            return;
        }
        kind = spec.num;
        hasSuffix = true;
        if (isFloatKind(kind)) isFloat = true;
    }

    if (isFloat) {
        add(TokenType::Float);
        tokens_.back().floatValue = std::strtod(digits.c_str(), nullptr);
        tokens_.back().numKind = kind;
        tokens_.back().hasSuffix = hasSuffix;
        return;
    }

    uint64_t value = 0;
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    for (char digit : digits) {
        uint64_t d = static_cast<uint64_t>(digitValue(digit));
        if (value > (max - d) / static_cast<uint64_t>(base)) {
            error(startLine_, startColumn_,
                  "number " + source_.substr(start_, pos_ - start_) + " is too large (the maximum is " +
                      std::to_string(max) + ")");
            return;
        }
        value = value * static_cast<uint64_t>(base) + d;
    }
    add(TokenType::Int);
    tokens_.back().intValue = value;
    tokens_.back().numKind = kind;
    tokens_.back().hasSuffix = hasSuffix;
}

// Reads one escape sequence after a backslash.
bool Lexer::escape(char32_t& out) {
    int escapeLine = line_;
    int escapeColumn = column_ - 1;
    if (atEnd() || peek() == '\n') return false;  // reported as unterminated by the caller
    char c = advance();
    switch (c) {
        case 'n': out = '\n'; return true;
        case 't': out = '\t'; return true;
        case 'r': out = '\r'; return true;
        case '0': out = '\0'; return true;
        case '\\': out = '\\'; return true;
        case '"': out = '"'; return true;
        case '\'': out = '\''; return true;
        case '{': out = '{'; return true;
        case '}': out = '}'; return true;
        case 'x': {
            int high = digitValue(peek());
            int low = high < 16 ? digitValue(peek(1)) : 99;
            if (high >= 16 || low >= 16) {
                error(escapeLine, escapeColumn, "invalid escape '\\x' (expected two hex digits, like \\x41)");
                out = kReplacementChar;
                return true;
            }
            advance();
            advance();
            out = static_cast<char32_t>(high * 16 + low);
            return true;
        }
        case 'u': {
            if (!match('{')) {
                error(escapeLine, escapeColumn, "invalid escape '\\u' (write it like \\u{1F600})");
                out = kReplacementChar;
                return true;
            }
            uint32_t cp = 0;
            int count = 0;
            while (digitValue(peek()) < 16 && count < 7) {
                cp = cp * 16 + static_cast<uint32_t>(digitValue(advance()));
                ++count;
            }
            if (count == 0 || !match('}') || !isValidCodePoint(cp)) {
                error(escapeLine, escapeColumn, "invalid unicode escape (write it like \\u{1F600})");
                out = kReplacementChar;
                return true;
            }
            out = cp;
            return true;
        }
        default:
            error(escapeLine, escapeColumn, std::string("unknown escape sequence '\\") + c + "'");
            out = static_cast<unsigned char>(c);
            return true;
    }
}

// Reads one (possibly multi-byte) character from the source.
char32_t Lexer::sourceChar() {
    size_t before = pos_;
    char32_t cp = decodeUtf8(source_, before);
    while (pos_ < before) advance();
    return cp;
}

void Lexer::string(char quote) {
    std::string value;
    while (true) {
        if (atEnd() || peek() == '\n') {
            error(startLine_, startColumn_, std::string("unterminated string (missing closing ") + quote + ")");
            return;
        }
        char c = peek();
        if (c == quote) {
            advance();
            break;
        }
        if (c == '\\') {
            advance();
            char32_t escaped;
            if (escape(escaped)) appendUtf8(value, escaped);
            continue;
        }
        value += advance();
    }
    add(TokenType::String);
    tokens_.back().text = std::move(value);
}

// 'a', '\n', '\u{1F600}': exactly one character.
void Lexer::character() {
    char32_t value = 0;
    if (atEnd() || peek() == '\n' || peek() == '\'') {
        error(startLine_, startColumn_,
              peek() == '\'' ? "empty char literal ''" : "unterminated char literal (missing closing ')");
        if (peek() == '\'') advance();
        return;
    }
    if (peek() == '\\') {
        advance();
        if (!escape(value)) {
            error(startLine_, startColumn_, "unterminated char literal (missing closing ')");
            return;
        }
    } else {
        value = sourceChar();
    }
    if (!match('\'')) {
        // Probably a string written with single quotes; skip to the closing quote.
        while (!atEnd() && peek() != '\n' && peek() != '\'') {
            if (peek() == '\\') advance();
            if (!atEnd() && peek() != '\n') advance();
        }
        bool closed = match('\'');
        error(startLine_, startColumn_,
              closed ? "a char literal holds exactly one character; use double quotes for strings"
                     : "unterminated char literal (missing closing ')");
        return;
    }
    add(TokenType::Char);
    tokens_.back().charValue = value;
}

// f"Hello, {name}! You are {age:>3} years old."
void Lexer::fString() {
    const int fLine = startLine_;
    const int fColumn = startColumn_;
    advance();  // opening quote
    addAt(TokenType::FStringStart, fLine, fColumn, "f\"");

    std::string literal;
    int literalLine = line_;
    int literalColumn = column_;
    auto flushLiteral = [&] {
        if (!literal.empty()) {
            addAt(TokenType::String, literalLine, literalColumn, literal);
            tokens_.back().text = literal;
            literal.clear();
        }
        literalLine = line_;
        literalColumn = column_;
    };

    while (true) {
        if (atEnd() || peek() == '\n') {
            error(fLine, fColumn, "unterminated f-string (missing closing \")");
            return;
        }
        char c = peek();
        if (c == '"') {
            flushLiteral();
            addAt(TokenType::FStringEnd, line_, column_, "\"");
            advance();
            return;
        }
        if (c == '\\') {
            advance();
            char32_t escaped;
            if (escape(escaped)) appendUtf8(literal, escaped);
            continue;
        }
        if (c == '}') {
            advance();
            if (match('}')) {
                literal += '}';
            } else {
                error(line_, column_ - 1, "single '}' in f-string (write '}}' for a literal brace)");
            }
            continue;
        }
        if (c != '{') {
            literal += advance();
            continue;
        }
        if (peek(1) == '{') {
            advance();
            advance();
            literal += '{';
            continue;
        }

        // An interpolation: {expression} or {expression:spec}
        flushLiteral();
        addAt(TokenType::InterpStart, line_, column_, "{");
        advance();
        const size_t depth = brackets_.size();
        bool sawExpression = false;
        while (true) {
            while (peek() == ' ' || peek() == '\t') advance();
            if (atEnd() || peek() == '\n') {
                error(fLine, fColumn, "unterminated f-string (missing closing \")");
                return;
            }
            if (brackets_.size() == depth && (peek() == '}' || (peek() == ':' && peek(1) != '='))) break;
            scanToken();
            sawExpression = true;
        }
        if (!sawExpression) error(line_, column_, "empty {} in f-string (write {{ for a literal brace)");
        if (peek() == ':') {
            int specLine = line_;
            int specColumn = column_;
            advance();
            std::string spec;
            while (!atEnd() && peek() != '}' && peek() != '\n' && peek() != '"') spec += advance();
            addAt(TokenType::FormatSpec, specLine, specColumn, ":" + spec);
            tokens_.back().text = spec;
            if (peek() != '}') {
                error(fLine, fColumn, "unterminated {...} in f-string (missing '}')");
                return;
            }
        }
        addAt(TokenType::InterpEnd, line_, column_, "}");
        advance();
        literalLine = line_;
        literalColumn = column_;
    }
}

}  // namespace ascend
