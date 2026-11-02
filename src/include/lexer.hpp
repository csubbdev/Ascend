#ifndef ASCEND_LEXER_HPP
#define ASCEND_LEXER_HPP

#include "error.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ascend {

enum class TokenType {
    // Punctuation
    LeftParen, RightParen, LeftBracket, RightBracket, LeftBrace, RightBrace,
    Comma, Dot, Colon, Semicolon, Question, Arrow /* -> */, FatArrow /* => */,
    QuestionQuestion /* ?? */, QuestionDot /* ?. */, DotDot /* .. */, DotDotEqual /* ..= */, Ellipsis /* ... */,

    // Operators
    Plus, Minus, Star, StarStar, Slash, Percent, PlusPlus, MinusMinus,
    Ampersand, Pipe, Caret, Tilde, ShiftLeft, ShiftRight,
    Equal, EqualEqual, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Declare,  // :=
    PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
    AmpersandEqual, PipeEqual, CaretEqual, ShiftLeftEqual, ShiftRightEqual,

    // Literals
    Identifier, Int, Float, String, Char,

    // f"..." strings are lexed as FStringStart, then String tokens for the
    // literal text and InterpStart <expression tokens> [FormatSpec] InterpEnd
    // for each {...}, then FStringEnd.
    FStringStart, FStringEnd, InterpStart, InterpEnd, FormatSpec,

    // Keywords. `&&`, `||` and `!` are lexed as And, Or and Not.
    And, Or, Not,
    If, Else, While, For, In, Break, Continue, Return, Func,
    True, False, Null, Try, Catch, Throw, Const, Match,

    // Structure
    Newline, Eof
};

struct Token {
    TokenType type = TokenType::Eof;
    std::string lexeme;           // the exact source text of the token
    std::string text;             // decoded String text, or a FormatSpec
    uint64_t intValue = 0;        // magnitude of an Int literal (range-checked by the parser)
    double floatValue = 0;        // value of a Float literal
    char32_t charValue = 0;       // value of a Char literal
    NumKind numKind = NumKind::Int;  // type of a number literal (from a suffix like 10u8)
    bool hasSuffix = false;
    int line = 1;
    int column = 1;
};

// Human readable description of a token for error messages, e.g. "'}'" or "end of line".
std::string describeToken(const Token& token);

// Turns source code into tokens.
//
// Newlines are significant (they end statements) except inside (...) and [...],
// so long calls and lists can be split across lines. Inside {...} newlines are
// kept, because braces delimit blocks of statements.
class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> tokenize();

    const std::vector<Diagnostic>& errors() const { return errors_; }

    // True if the source ended inside a /* block comment */. The REPL uses this
    // to keep reading input instead of reporting an error.
    bool endedInsideComment() const { return endedInsideComment_; }

private:
    char peek(size_t ahead = 0) const;
    bool atEnd() const;
    char advance();
    bool match(char expected);

    void skipTrivia();
    void blockComment();
    void scanToken();
    void identifier();
    void number(char first);
    void string(char quote);
    void longString(bool raw);
    void rawString();
    void character();
    void fString();
    bool escape(char32_t& out);
    char32_t sourceChar();

    void add(TokenType type);
    void addAt(TokenType type, int line, int column, std::string lexeme);
    void emitNewline();
    void closeBracket();
    void error(int line, int column, std::string message);

    std::string source_;
    std::vector<Token> tokens_;
    std::vector<Diagnostic> errors_;
    std::vector<char> brackets_;  // currently open (, [ and {
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    size_t start_ = 0;
    int startLine_ = 1;
    int startColumn_ = 1;
    bool endedInsideComment_ = false;
};

}  // namespace ascend

#endif  // ASCEND_LEXER_HPP
