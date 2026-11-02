#include "lexer.hpp"

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
    };
    return table;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentifierStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
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
            eof.column = it->column + static_cast<int>(it->lexeme.size());
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
    Token token;
    token.type = TokenType::Newline;
    token.lexeme = "\n";
    token.line = line_;
    token.column = column_;
    tokens_.push_back(token);
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

void Lexer::add(TokenType type) {
    Token token;
    token.type = type;
    token.lexeme = source_.substr(start_, pos_ - start_);
    token.line = startLine_;
    token.column = startColumn_;
    tokens_.push_back(std::move(token));
}

void Lexer::scanToken() {
    start_ = pos_;
    startLine_ = line_;
    startColumn_ = column_;

    char c = advance();
    if (isIdentifierStart(c)) {
        identifier();
        return;
    }
    if (isDigit(c)) {
        number();
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
        case ':': add(match('=') ? TokenType::Declare : TokenType::Colon); return;
        case '+': add(match('=') ? TokenType::PlusEqual : TokenType::Plus); return;
        case '-': add(match('=') ? TokenType::MinusEqual : TokenType::Minus); return;
        case '*':
            if (match('*')) {
                add(TokenType::StarStar);
            } else {
                add(match('=') ? TokenType::StarEqual : TokenType::Star);
            }
            return;
        case '/': add(match('=') ? TokenType::SlashEqual : TokenType::Slash); return;
        case '%': add(match('=') ? TokenType::PercentEqual : TokenType::Percent); return;
        case '!': add(match('=') ? TokenType::NotEqual : TokenType::Not); return;
        case '=': add(match('=') ? TokenType::EqualEqual : TokenType::Equal); return;
        case '<': add(match('=') ? TokenType::LessEqual : TokenType::Less); return;
        case '>': add(match('=') ? TokenType::GreaterEqual : TokenType::Greater); return;
        case '&':
            if (match('&')) {
                add(TokenType::And);
            } else {
                error(startLine_, startColumn_, "unexpected character '&' (use '&&' or 'and' for logical AND)");
            }
            return;
        case '|':
            if (match('|')) {
                add(TokenType::Or);
            } else {
                error(startLine_, startColumn_, "unexpected character '|' (use '||' or 'or' for logical OR)");
            }
            return;
        case '"':
        case '\'':
            string(c);
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

void Lexer::number() {
    auto digits = [this] {
        while (isDigit(peek()) || (peek() == '_' && isDigit(peek(1)))) advance();
    };

    digits();
    bool isFloat = false;
    if (peek() == '.' && isDigit(peek(1))) {
        isFloat = true;
        advance();
        digits();
    }
    if ((peek() == 'e' || peek() == 'E') &&
        (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        digits();
    }
    if (isIdentifierChar(peek())) {
        while (isIdentifierChar(peek())) advance();
        error(startLine_, startColumn_,
              "invalid number '" + source_.substr(start_, pos_ - start_) + "'");
        return;
    }

    std::string text;
    for (size_t i = start_; i < pos_; ++i) {
        if (source_[i] != '_') text += source_[i];
    }

    if (isFloat) {
        add(TokenType::Float);
        tokens_.back().floatValue = std::strtod(text.c_str(), nullptr);
        return;
    }

    const int64_t max = std::numeric_limits<int64_t>::max();
    int64_t value = 0;
    for (char digit : text) {
        int d = digit - '0';
        if (value > (max - d) / 10) {
            error(startLine_, startColumn_,
                  "integer " + text + " is too large (the maximum is " + std::to_string(max) + ")");
            return;
        }
        value = value * 10 + d;
    }
    add(TokenType::Int);
    tokens_.back().intValue = value;
}

void Lexer::string(char quote) {
    std::string value;
    while (true) {
        if (atEnd() || peek() == '\n') {
            error(startLine_, startColumn_,
                  std::string("unterminated string (missing closing ") + quote + ")");
            return;
        }
        char c = advance();
        if (c == quote) break;
        if (c != '\\') {
            value += c;
            continue;
        }

        int escapeLine = line_;
        int escapeColumn = column_ - 1;
        if (atEnd() || peek() == '\n') continue;  // reported as unterminated above
        char escaped = advance();
        switch (escaped) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '0': value += '\0'; break;
            case '\\': value += '\\'; break;
            case '"': value += '"'; break;
            case '\'': value += '\''; break;
            case 'x': {
                int high = hexValue(peek());
                int low = high < 0 ? -1 : hexValue(peek(1));
                if (high < 0 || low < 0) {
                    error(escapeLine, escapeColumn, "invalid escape '\\x' (expected two hex digits, like \\x41)");
                    break;
                }
                advance();
                advance();
                value += static_cast<char>(high * 16 + low);
                break;
            }
            default:
                error(escapeLine, escapeColumn, std::string("unknown escape sequence '\\") + escaped + "'");
                break;
        }
    }
    add(TokenType::String);
    tokens_.back().text = std::move(value);
}

}  // namespace ascend
