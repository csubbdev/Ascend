#include "lexer.hpp"
#include "test_framework.hpp"

#include <string>
#include <vector>

using ascend::Lexer;
using ascend::Token;
using ascend::TokenType;

namespace {

std::vector<TokenType> types(const std::string& source) {
    Lexer lexer(source);
    std::vector<TokenType> result;
    for (const Token& token : lexer.tokenize()) result.push_back(token.type);
    return result;
}

std::vector<Token> tokens(const std::string& source) {
    Lexer lexer(source);
    return lexer.tokenize();
}

std::string firstError(const std::string& source) {
    Lexer lexer(source);
    lexer.tokenize();
    return lexer.errors().empty() ? "" : lexer.errors()[0].message;
}

}  // namespace

TEST(lexer_declaration) {
    std::vector<TokenType> expected = {TokenType::Identifier, TokenType::Declare, TokenType::Int, TokenType::Plus,
                                       TokenType::Int, TokenType::Eof};
    CHECK(types("x := 10 + 5") == expected);
}

TEST(lexer_two_character_operators) {
    std::vector<TokenType> expected = {
        TokenType::EqualEqual, TokenType::NotEqual,  TokenType::LessEqual,  TokenType::GreaterEqual,
        TokenType::PlusEqual,  TokenType::MinusEqual, TokenType::StarEqual, TokenType::SlashEqual,
        TokenType::PercentEqual, TokenType::StarStar, TokenType::And,       TokenType::Or,
        TokenType::Eof};
    CHECK(types("== != <= >= += -= *= /= %= ** && ||") == expected);
}

TEST(lexer_not_equal_is_one_token) {
    // The old lexer split `!=` into `!` and `=`.
    std::vector<TokenType> expected = {TokenType::Identifier, TokenType::NotEqual, TokenType::Int, TokenType::Eof};
    CHECK(types("x != 3") == expected);
}

TEST(lexer_keywords_and_word_operators) {
    std::vector<TokenType> expected = {TokenType::If,  TokenType::Not, TokenType::Identifier, TokenType::And,
                                       TokenType::True, TokenType::Or, TokenType::Null,       TokenType::Eof};
    CHECK(types("if not ready and true or null") == expected);
}

TEST(lexer_numbers) {
    auto result = tokens("42 3.25 1e3 2.5e-2 1_000_000");
    CHECK(result[0].type == TokenType::Int);
    CHECK_EQ(result[0].intValue, 42u);
    CHECK(result[1].type == TokenType::Float);
    CHECK_EQ(result[1].floatValue, 3.25);
    CHECK(result[2].type == TokenType::Float);
    CHECK_EQ(result[2].floatValue, 1000.0);
    CHECK_EQ(result[3].floatValue, 0.025);
    CHECK_EQ(result[4].intValue, 1000000u);
}

TEST(lexer_number_followed_by_method_call) {
    std::vector<TokenType> expected = {TokenType::Int, TokenType::Dot, TokenType::Identifier, TokenType::Eof};
    CHECK(types("5.double") == expected);
}

TEST(lexer_strings_and_escapes) {
    auto result = tokens(R"("a\tb\n" "it's \"quoted\"" "\x41\\" "\u{e9}")");
    CHECK(result[0].type == TokenType::String);
    CHECK_EQ(result[0].text, std::string("a\tb\n"));
    CHECK_EQ(result[1].text, std::string("it's \"quoted\""));
    CHECK_EQ(result[2].text, std::string("A\\"));
    CHECK_EQ(result[3].text, std::string("\xC3\xA9"));  // U+00E9 in UTF-8
}

TEST(lexer_char_literals) {
    auto result = tokens(R"('a' '\n' '\'' 'é' '\u{1F600}')");
    CHECK(result[0].type == TokenType::Char);
    CHECK(result[0].charValue == U'a');
    CHECK(result[1].charValue == U'\n');
    CHECK(result[2].charValue == U'\'');
    CHECK(result[3].charValue == U'\u00e9');
    CHECK(result[4].charValue == U'\U0001F600');
}

TEST(lexer_comments_are_skipped) {
    std::vector<TokenType> expected = {TokenType::Identifier, TokenType::Newline, TokenType::Identifier, TokenType::Eof};
    CHECK(types("a // line comment\n/* block\ncomment */ b") == expected);
}

TEST(lexer_newlines_end_statements_but_not_inside_brackets) {
    std::vector<TokenType> expected = {
        TokenType::Identifier, TokenType::LeftParen, TokenType::Int, TokenType::Comma, TokenType::Int,
        TokenType::RightParen, TokenType::Newline,   TokenType::Identifier, TokenType::Eof};
    CHECK(types("f(1,\n  2)\ng") == expected);
}

TEST(lexer_newlines_kept_inside_braces) {
    std::vector<TokenType> expected = {TokenType::LeftBrace, TokenType::Newline, TokenType::Identifier,
                                       TokenType::Newline, TokenType::RightBrace, TokenType::Eof};
    CHECK(types("{\n  a\n}") == expected);
}

TEST(lexer_positions) {
    auto result = tokens("x := 1\n  y := 2");
    CHECK_EQ(result[0].line, 1);
    CHECK_EQ(result[0].column, 1);
    CHECK_EQ(result[4].lexeme, std::string("y"));
    CHECK_EQ(result[4].line, 2);
    CHECK_EQ(result[4].column, 3);
}

TEST(lexer_errors) {
    CHECK_EQ(firstError("\"abc"), std::string("unterminated string (missing closing \")"));
    CHECK_EQ(firstError("/* never closed"), std::string("unterminated block comment (missing '*/')"));
    CHECK_EQ(firstError("'ab'"), std::string("a char literal holds exactly one character; use double quotes for strings"));
    CHECK_EQ(firstError("''"), std::string("empty char literal ''"));
    CHECK_EQ(firstError("x := 3$"), std::string("unexpected character '$'"));
    CHECK_EQ(firstError("x := @"), std::string("unexpected character '@'"));
    CHECK_EQ(firstError("\"\\q\""), std::string("unknown escape sequence '\\q'"));
    CHECK_EQ(firstError("12abc"), std::string("invalid number '12abc'"));
    CHECK(firstError("99999999999999999999").find("too large") != std::string::npos);
}

TEST(lexer_reports_unterminated_comment_for_repl) {
    Lexer lexer("x := 1 /* still typing");
    lexer.tokenize();
    CHECK(lexer.endedInsideComment());
}

TEST(lexer_number_bases_and_suffixes) {
    auto result = tokens("0xFF 0b1010 0o17 1_000u16 255u8 7i64 2.5f32 3f64 0xFFu8 0x1f32");
    CHECK_EQ(result[0].intValue, 255u);
    CHECK_EQ(result[1].intValue, 10u);
    CHECK_EQ(result[2].intValue, 15u);
    CHECK_EQ(result[3].intValue, 1000u);
    CHECK(result[3].numKind == ascend::NumKind::U16);
    CHECK(result[4].numKind == ascend::NumKind::U8);
    CHECK(result[5].numKind == ascend::NumKind::I64);
    CHECK(result[6].type == TokenType::Float);
    CHECK(result[6].numKind == ascend::NumKind::F32);
    CHECK(result[7].type == TokenType::Float);  // a float suffix makes an integer literal a float
    CHECK(result[8].numKind == ascend::NumKind::U8);
    CHECK_EQ(result[9].intValue, 0x1f32u);  // 'f' is a hex digit, not a suffix
    CHECK_EQ(firstError("10u9"), std::string("invalid number '10u9'"));
    CHECK_EQ(firstError("0b102"), std::string("invalid number '0b102'"));
    CHECK_EQ(firstError("1.5i32"), std::string("'i32' is an integer type, but 1.5 has a fraction or exponent"));
}

TEST(lexer_bitwise_and_arrow_operators) {
    std::vector<TokenType> expected = {
        TokenType::Ampersand,     TokenType::Pipe,      TokenType::Caret,          TokenType::Tilde,
        TokenType::ShiftLeft,     TokenType::ShiftRight, TokenType::AmpersandEqual, TokenType::PipeEqual,
        TokenType::CaretEqual,    TokenType::ShiftLeftEqual, TokenType::ShiftRightEqual, TokenType::Arrow,
        TokenType::FatArrow,      TokenType::Const,     TokenType::Match,          TokenType::Eof};
    CHECK(types("& | ^ ~ << >> &= |= ^= <<= >>= -> => const match") == expected);
}

TEST(lexer_f_strings) {
    std::vector<TokenType> expected = {
        TokenType::FStringStart, TokenType::String,     TokenType::InterpStart, TokenType::Identifier,
        TokenType::InterpEnd,    TokenType::String,     TokenType::InterpStart, TokenType::Identifier,
        TokenType::LeftBracket,  TokenType::String,     TokenType::RightBracket, TokenType::FormatSpec,
        TokenType::InterpEnd,    TokenType::FStringEnd, TokenType::Eof};
    CHECK(types(R"(f"Hi {name}, {{x}} {m["k"]:>5}")") == expected);
    auto result = tokens(R"(f"a{{b}}c {x:>5}")");
    CHECK_EQ(result[1].text, std::string("a{b}c "));
    CHECK_EQ(result[4].text, std::string(">5"));
    CHECK_EQ(firstError("f\"{x\""), std::string("unterminated string (missing closing \")"));
    CHECK_EQ(firstError("f\"oops }\""), std::string("single '}' in f-string (write '}}' for a literal brace)"));
}

TEST(lexer_raw_and_long_strings) {
    std::vector<Token> raw = Lexer("r\"C:\\new\\d+\"").tokenize();
    CHECK(raw[0].type == TokenType::String);
    CHECK_EQ(raw[0].text, "C:\\new\\d+");

    std::vector<Token> longString = Lexer("x := \"\"\"\nfirst \"quoted\"\n  second\\t\"\"\"\ny").tokenize();
    CHECK(longString[2].type == TokenType::String);
    CHECK_EQ(longString[2].text, "first \"quoted\"\n  second\t");
    CHECK_EQ(longString[4].lexeme, "y");
    CHECK_EQ(longString[4].line, 4);

    std::vector<Token> rawLong = Lexer("r\"\"\"a\\nb\"\"\"").tokenize();
    CHECK_EQ(rawLong[0].text, "a\\nb");

    Lexer unterminated("\"\"\"never closed");
    unterminated.tokenize();
    CHECK_EQ(unterminated.errors().size(), size_t(1));
}

TEST(lexer_new_operators) {
    std::vector<Token> tokens = Lexer("a ?? b?.c 0..5 1..=2 ...xs x ? .5 : y").tokenize();
    std::vector<TokenType> expected = {TokenType::Identifier, TokenType::QuestionQuestion, TokenType::Identifier,
                                       TokenType::QuestionDot, TokenType::Identifier, TokenType::Int,
                                       TokenType::DotDot, TokenType::Int, TokenType::Int, TokenType::DotDotEqual,
                                       TokenType::Int, TokenType::Ellipsis, TokenType::Identifier,
                                       TokenType::Identifier, TokenType::Question, TokenType::Dot};
    for (size_t i = 0; i < expected.size(); ++i) CHECK(tokens[i].type == expected[i]);
    CHECK(Lexer("1.5").tokenize()[0].type == TokenType::Float);
    CHECK(Lexer("x++ --y").tokenize()[1].type == TokenType::PlusPlus);
}
