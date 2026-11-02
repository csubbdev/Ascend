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
    CHECK_EQ(result[0].intValue, 42);
    CHECK(result[1].type == TokenType::Float);
    CHECK_EQ(result[1].floatValue, 3.25);
    CHECK(result[2].type == TokenType::Float);
    CHECK_EQ(result[2].floatValue, 1000.0);
    CHECK_EQ(result[3].floatValue, 0.025);
    CHECK_EQ(result[4].intValue, 1000000);
}

TEST(lexer_number_followed_by_method_call) {
    std::vector<TokenType> expected = {TokenType::Int, TokenType::Dot, TokenType::Identifier, TokenType::Eof};
    CHECK(types("5.double") == expected);
}

TEST(lexer_strings_and_escapes) {
    auto result = tokens(R"("a\tb\n" 'it\'s' "\x41\\")");
    CHECK(result[0].type == TokenType::String);
    CHECK_EQ(result[0].text, std::string("a\tb\n"));
    CHECK_EQ(result[1].text, std::string("it's"));
    CHECK_EQ(result[2].text, std::string("A\\"));
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
    CHECK_EQ(firstError("a & b"), std::string("unexpected character '&' (use '&&' or 'and' for logical AND)"));
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
