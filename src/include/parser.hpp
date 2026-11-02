#ifndef ASCEND_PARSER_HPP
#define ASCEND_PARSER_HPP

#include "ast.hpp"
#include "error.hpp"
#include "lexer.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace ascend {

// Recursive descent parser that turns tokens into an AST.
//
// It recovers from errors at statement boundaries, so several syntax errors
// can be reported at once.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    std::unique_ptr<Program> parseProgram();

    const std::vector<Diagnostic>& errors() const { return errors_; }

    // True if the first error happened because the input ended early (an
    // unclosed brace, a trailing operator, ...). The REPL then asks for more input.
    bool incomplete() const { return incomplete_; }

private:
    struct ParseError {};
    class NestingGuard;

    // Statements
    StmtPtr statement();
    StmtPtr simpleStatement();
    StmtPtr ifStatement(const Token& keyword);
    StmtPtr whileStatement(const Token& keyword);
    StmtPtr forStatement(const Token& keyword);
    StmtPtr returnStatement(const Token& keyword);
    StmtPtr tryStatement(const Token& keyword);
    StmtPtr throwStatement(const Token& keyword);
    StmtPtr functionDeclaration(const Token& keyword);
    StmtPtr typedDeclaration();
    StmtPtr typedFunctionDeclaration();
    StmtPtr constDeclaration(const Token& keyword);
    StmtPtr matchStatement(const Token& keyword);
    std::unique_ptr<FunctionExpr> functionRest(const Token& keyword, const std::string& name,
                                               const TypeSpec* returnType = nullptr);
    TypeSpec parseType(bool allowVoid);
    size_t typeLength(size_t offset) const;
    static std::string unknownTypeMessage(const std::string& name);
    Block block(const std::string& what);
    static void noteStatement(Block& block, const Stmt& stmt);
    void parseStatementInto(Block& block);
    ExprPtr condition(const std::string& keyword);
    void expectStatementEnd();
    void synchronize();

    // Expressions, from lowest to highest precedence
    ExprPtr expression();
    ExprPtr ternary();
    ExprPtr logicOr();
    ExprPtr logicAnd();
    ExprPtr logicNot();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr bitOr();
    ExprPtr bitXor();
    ExprPtr bitAnd();
    ExprPtr shift();
    ExprPtr additive();
    ExprPtr multiplicative();
    ExprPtr unary();
    ExprPtr power();
    ExprPtr postfix();
    ExprPtr primary();
    ExprPtr binary(ExprPtr (Parser::*operand)(), std::initializer_list<TokenType> operators);
    ExprPtr finishCall(ExprPtr callee);
    ExprPtr finishIndex(ExprPtr object, const Token& bracket);
    ExprPtr listLiteral(const Token& bracket);
    ExprPtr mapLiteral(const Token& brace);
    ExprPtr fString(const Token& start);
    ExprPtr numberLiteral(const Token& token, bool negate);

    // Token helpers
    const Token& peek(size_t ahead = 0) const;
    const Token& peekPastNewlines() const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool matchAny(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& expected);
    void skipNewlines();
    void skipSeparators();
    bool isAtEnd() const;
    [[noreturn]] void error(const Token& at, const std::string& message);

    std::vector<Token> tokens_;
    size_t current_ = 0;
    std::vector<Diagnostic> errors_;
    bool incomplete_ = false;
    int depth_ = 0;
};

}  // namespace ascend

#endif  // ASCEND_PARSER_HPP
