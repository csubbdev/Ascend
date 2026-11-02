#include "parser.hpp"

#include <algorithm>

namespace ascend {

namespace {

constexpr int kMaxNesting = 200;

bool isAssignmentOperator(TokenType type) {
    switch (type) {
        case TokenType::Equal:
        case TokenType::PlusEqual:
        case TokenType::MinusEqual:
        case TokenType::StarEqual:
        case TokenType::SlashEqual:
        case TokenType::PercentEqual:
            return true;
        default:
            return false;
    }
}

bool isAssignable(const Expr& expr) {
    return expr.kind == ExprKind::Variable || expr.kind == ExprKind::Index || expr.kind == ExprKind::Member;
}

template <typename T>
std::unique_ptr<T> nodeAt(const Token& token) {
    return std::make_unique<T>(token.line, token.column);
}

}  // namespace

// Limits how deeply blocks and expressions may nest, so pathological input
// produces an error instead of overflowing the stack.
class Parser::NestingGuard {
public:
    NestingGuard(Parser& parser, const Token& at) : parser_(parser) {
        if (parser_.depth_ >= kMaxNesting) parser_.error(at, "code is nested too deeply");
        ++parser_.depth_;
    }
    ~NestingGuard() { --parser_.depth_; }
    NestingGuard(const NestingGuard&) = delete;
    NestingGuard& operator=(const NestingGuard&) = delete;

private:
    Parser& parser_;
};

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
    if (tokens_.empty() || tokens_.back().type != TokenType::Eof) {
        Token eof;
        eof.type = TokenType::Eof;
        if (!tokens_.empty()) {
            eof.line = tokens_.back().line;
            eof.column = tokens_.back().column;
        }
        tokens_.push_back(eof);
    }
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>();
    while (true) {
        skipSeparators();
        if (isAtEnd()) break;
        if (check(TokenType::RightBrace)) {
            const Token& brace = advance();
            errors_.push_back({brace.line, brace.column, "unexpected '}' (there is no open block to close)"});
            continue;
        }
        parseStatementInto(program->body);
    }
    return program;
}

// Parses one statement into `block`, recovering from syntax errors.
void Parser::parseStatementInto(Block& block) {
    size_t start = current_;
    try {
        StmtPtr stmt = statement();
        if (stmt->kind == StmtKind::Declare) block.declaresNames = true;
        if (stmt->kind == StmtKind::Function) {
            block.declaresNames = true;
            block.hasFunctions = true;
        }
        block.statements.push_back(std::move(stmt));
    } catch (const ParseError&) {
        synchronize();
        if (current_ == start && !isAtEnd()) advance();  // always make progress
    }
}

// ---------------------------------------------------------------------------
// Statements

StmtPtr Parser::statement() {
    const Token& token = peek();
    switch (token.type) {
        case TokenType::If: advance(); return ifStatement(token);
        case TokenType::While: advance(); return whileStatement(token);
        case TokenType::For: advance(); return forStatement(token);
        case TokenType::Return: advance(); return returnStatement(token);
        case TokenType::Try: advance(); return tryStatement(token);
        case TokenType::Throw: advance(); return throwStatement(token);
        case TokenType::Break: {
            advance();
            auto stmt = nodeAt<BreakStmt>(token);
            expectStatementEnd();
            return stmt;
        }
        case TokenType::Continue: {
            advance();
            auto stmt = nodeAt<ContinueStmt>(token);
            expectStatementEnd();
            return stmt;
        }
        case TokenType::Func:
            if (peek(1).type == TokenType::Identifier) {
                advance();
                return functionDeclaration(token);
            }
            break;  // an anonymous function used as an expression
        case TokenType::LeftBrace: {
            auto stmt = nodeAt<BlockStmt>(token);
            stmt->body = block("block");
            return stmt;
        }
        case TokenType::Else:
            error(token, "'else' without a matching 'if'");
        case TokenType::Catch:
            error(token, "'catch' without a matching 'try'");
        default:
            break;
    }
    return simpleStatement();
}

// Expression statements, declarations (x := 1) and assignments (x = 1, x += 1).
StmtPtr Parser::simpleStatement() {
    ExprPtr expr = expression();

    if (check(TokenType::Declare)) {
        const Token& op = advance();
        if (expr->kind != ExprKind::Variable) {
            error(op, "':=' can only declare a plain variable name (use '=' to change an element or field)");
        }
        auto stmt = std::make_unique<DeclareStmt>(expr->line, expr->column);
        stmt->name = expr->as<VariableExpr>().name;
        skipNewlines();
        stmt->value = expression();
        expectStatementEnd();
        return stmt;
    }

    if (isAssignmentOperator(peek().type)) {
        const Token& op = advance();
        if (!isAssignable(*expr)) error(op, "can't assign to this expression");
        auto stmt = std::make_unique<AssignStmt>(expr->line, expr->column);
        stmt->op = op.type;
        stmt->target = std::move(expr);
        skipNewlines();
        stmt->value = expression();
        expectStatementEnd();
        return stmt;
    }

    auto stmt = std::make_unique<ExpressionStmt>(expr->line, expr->column);
    stmt->expr = std::move(expr);
    expectStatementEnd();
    return stmt;
}

StmtPtr Parser::ifStatement(const Token& keyword) {
    auto stmt = nodeAt<IfStmt>(keyword);
    stmt->condition = condition("if");
    stmt->thenBody = block("'if' body");
    if (peekPastNewlines().type == TokenType::Else) {
        skipNewlines();
        const Token& elseToken = advance();
        if (check(TokenType::If)) {
            const Token& ifToken = advance();
            stmt->elseBranch = ifStatement(ifToken);
        } else {
            auto elseBlock = nodeAt<BlockStmt>(elseToken);
            elseBlock->body = block("'else' body");
            stmt->elseBranch = std::move(elseBlock);
        }
    }
    return stmt;
}

StmtPtr Parser::whileStatement(const Token& keyword) {
    auto stmt = nodeAt<WhileStmt>(keyword);
    stmt->condition = condition("while");
    stmt->body = block("'while' body");
    return stmt;
}

StmtPtr Parser::forStatement(const Token& keyword) {
    auto stmt = nodeAt<ForStmt>(keyword);
    bool parenthesized = match(TokenType::LeftParen);  // allow for (x in xs) { ... }
    stmt->first = consume(TokenType::Identifier, "a loop variable name after 'for'").lexeme;
    if (match(TokenType::Comma)) {
        stmt->second = consume(TokenType::Identifier, "a second loop variable name after ','").lexeme;
    }
    consume(TokenType::In, "'in' after the loop variable");
    stmt->iterable = condition("in");
    if (parenthesized) consume(TokenType::RightParen, "')' after the 'for' header");
    stmt->body = block("'for' body");
    return stmt;
}

StmtPtr Parser::returnStatement(const Token& keyword) {
    auto stmt = nodeAt<ReturnStmt>(keyword);
    if (!check(TokenType::Newline) && !check(TokenType::Semicolon) && !check(TokenType::RightBrace) &&
        !isAtEnd()) {
        stmt->value = expression();
    }
    expectStatementEnd();
    return stmt;
}

StmtPtr Parser::tryStatement(const Token& keyword) {
    auto stmt = nodeAt<TryStmt>(keyword);
    stmt->body = block("'try' body");
    if (peekPastNewlines().type != TokenType::Catch) {
        error(peekPastNewlines(), "expected 'catch' after the 'try' block, found " + describeToken(peekPastNewlines()));
    }
    skipNewlines();
    advance();  // catch
    if (match(TokenType::LeftParen)) {
        stmt->errorName = consume(TokenType::Identifier, "an error variable name").lexeme;
        consume(TokenType::RightParen, "')' after the error variable name");
    } else if (check(TokenType::Identifier)) {
        stmt->errorName = advance().lexeme;
    }
    stmt->handler = block("'catch' body");
    return stmt;
}

StmtPtr Parser::throwStatement(const Token& keyword) {
    auto stmt = nodeAt<ThrowStmt>(keyword);
    if (check(TokenType::Newline) || check(TokenType::Semicolon) || check(TokenType::RightBrace) || isAtEnd()) {
        error(peek(), "expected a value to throw after 'throw'");
    }
    stmt->value = expression();
    expectStatementEnd();
    return stmt;
}

StmtPtr Parser::functionDeclaration(const Token& keyword) {
    const Token& name = advance();
    auto stmt = nodeAt<FunctionStmt>(keyword);
    stmt->function = functionRest(name, name.lexeme);
    return stmt;
}

std::unique_ptr<FunctionExpr> Parser::functionRest(const Token& keyword, const std::string& name) {
    auto function = nodeAt<FunctionExpr>(keyword);
    function->name = name;
    consume(TokenType::LeftParen, name.empty() ? "'(' after 'func'" : "'(' after the function name");
    bool sawDefault = false;
    while (!check(TokenType::RightParen)) {
        const Token& param = consume(TokenType::Identifier, "a parameter name");
        Param parameter;
        parameter.name = param.lexeme;
        if (match(TokenType::Equal)) {
            parameter.defaultValue = expression();
            sawDefault = true;
        } else if (sawDefault) {
            error(param, "parameter '" + param.lexeme + "' needs a default value because an earlier parameter has one");
        } else {
            ++function->requiredParams;
        }
        function->params.push_back(std::move(parameter));
        if (!match(TokenType::Comma)) break;
    }
    consume(TokenType::RightParen, "')' after the parameters");
    function->body = block("function body");
    return function;
}

Block Parser::block(const std::string& what) {
    skipNewlines();  // allow the opening brace on its own line
    const Token& open = consume(TokenType::LeftBrace, "'{' to start the " + what);
    NestingGuard guard(*this, open);
    Block result;
    while (true) {
        skipSeparators();
        if (check(TokenType::RightBrace) || isAtEnd()) break;
        parseStatementInto(result);
    }
    if (!check(TokenType::RightBrace)) {
        error(peek(), "expected '}' to close the " + what + " that starts on line " + std::to_string(open.line) +
                          ", found " + describeToken(peek()));
    }
    advance();
    return result;
}

ExprPtr Parser::condition(const std::string& keyword) {
    if (check(TokenType::LeftBrace) || check(TokenType::Newline) || isAtEnd()) {
        error(peek(), "expected an expression after '" + keyword + "', found " + describeToken(peek()));
    }
    return expression();
}

void Parser::expectStatementEnd() {
    if (match(TokenType::Newline) || match(TokenType::Semicolon)) return;
    if (check(TokenType::RightBrace) || isAtEnd()) return;
    error(peek(), "expected a newline or ';' after the statement, found " + describeToken(peek()));
}

// Skips tokens until a likely statement boundary after a syntax error.
// Braces opened while skipping are skipped up to their matching '}', so one
// mistake doesn't cause a cascade of "unexpected '}'" errors.
void Parser::synchronize() {
    int braces = 0;
    while (!isAtEnd()) {
        TokenType type = peek().type;
        if (type == TokenType::LeftBrace) {
            ++braces;
            advance();
            continue;
        }
        if (braces > 0) {
            if (type == TokenType::RightBrace) --braces;
            advance();
            if (braces == 0 && (check(TokenType::Newline) || check(TokenType::Semicolon))) advance();
            if (braces == 0) return;
            continue;
        }
        switch (type) {
            case TokenType::Newline:
            case TokenType::Semicolon:
                advance();
                return;
            case TokenType::RightBrace:
            case TokenType::If:
            case TokenType::While:
            case TokenType::For:
            case TokenType::Func:
            case TokenType::Return:
            case TokenType::Try:
            case TokenType::Throw:
            case TokenType::Break:
            case TokenType::Continue:
                return;
            default:
                advance();
        }
    }
}

// ---------------------------------------------------------------------------
// Expressions

ExprPtr Parser::expression() {
    NestingGuard guard(*this, peek());
    return ternary();
}

ExprPtr Parser::ternary() {
    ExprPtr condition = logicOr();
    if (!check(TokenType::Question)) return condition;
    const Token& question = advance();
    auto expr = nodeAt<TernaryExpr>(question);
    skipNewlines();
    expr->thenExpr = expression();
    skipNewlines();
    consume(TokenType::Colon, "':' in the conditional expression");
    skipNewlines();
    expr->elseExpr = ternary();
    expr->condition = std::move(condition);
    return expr;
}

ExprPtr Parser::logicOr() {
    ExprPtr left = logicAnd();
    while (check(TokenType::Or)) {
        const Token& op = advance();
        skipNewlines();
        auto expr = nodeAt<LogicalExpr>(op);
        expr->op = TokenType::Or;
        expr->left = std::move(left);
        expr->right = logicAnd();
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::logicAnd() {
    ExprPtr left = logicNot();
    while (check(TokenType::And)) {
        const Token& op = advance();
        skipNewlines();
        auto expr = nodeAt<LogicalExpr>(op);
        expr->op = TokenType::And;
        expr->left = std::move(left);
        expr->right = logicNot();
        left = std::move(expr);
    }
    return left;
}

// `not` / `!` at the start of an operand applies to the whole comparison:
// `not x in items` means `not (x in items)`.
ExprPtr Parser::logicNot() {
    if (!check(TokenType::Not)) return equality();
    const Token& op = advance();
    NestingGuard guard(*this, op);
    auto expr = nodeAt<UnaryExpr>(op);
    expr->op = TokenType::Not;
    expr->operand = logicNot();
    return expr;
}

ExprPtr Parser::binary(ExprPtr (Parser::*operand)(), std::initializer_list<TokenType> operators) {
    ExprPtr left = (this->*operand)();
    while (matchAny(operators)) {
        const Token& op = previous();
        skipNewlines();
        auto expr = nodeAt<BinaryExpr>(op);
        expr->op = op.type;
        expr->left = std::move(left);
        expr->right = (this->*operand)();
        left = std::move(expr);
    }
    return left;
}

ExprPtr Parser::equality() {
    return binary(&Parser::comparison, {TokenType::EqualEqual, TokenType::NotEqual});
}

ExprPtr Parser::comparison() {
    return binary(&Parser::additive, {TokenType::Less, TokenType::LessEqual, TokenType::Greater,
                                      TokenType::GreaterEqual, TokenType::In});
}

ExprPtr Parser::additive() { return binary(&Parser::multiplicative, {TokenType::Plus, TokenType::Minus}); }

ExprPtr Parser::multiplicative() {
    return binary(&Parser::unary, {TokenType::Star, TokenType::Slash, TokenType::Percent});
}

ExprPtr Parser::unary() {
    if (check(TokenType::Minus) || check(TokenType::Not)) {
        const Token& op = advance();
        NestingGuard guard(*this, op);
        auto expr = nodeAt<UnaryExpr>(op);
        expr->op = op.type;
        expr->operand = unary();
        return expr;
    }
    return power();
}

// `**` binds tighter than unary minus on its left and is right associative:
// -2 ** 2 == -4 and 2 ** 3 ** 2 == 2 ** 9.
ExprPtr Parser::power() {
    ExprPtr base = postfix();
    if (!check(TokenType::StarStar)) return base;
    const Token& op = advance();
    skipNewlines();
    auto expr = nodeAt<BinaryExpr>(op);
    expr->op = TokenType::StarStar;
    expr->left = std::move(base);
    expr->right = unary();
    return expr;
}

ExprPtr Parser::postfix() {
    ExprPtr expr = primary();
    while (true) {
        if (check(TokenType::LeftParen)) {
            advance();
            expr = finishCall(std::move(expr));
        } else if (check(TokenType::LeftBracket)) {
            const Token& bracket = advance();
            expr = finishIndex(std::move(expr), bracket);
        } else if (check(TokenType::Dot)) {
            advance();
            const Token& name = consume(TokenType::Identifier, "a field or method name after '.'");
            auto member = nodeAt<MemberExpr>(name);
            member->object = std::move(expr);
            member->name = name.lexeme;
            expr = std::move(member);
        } else {
            return expr;
        }
    }
}

ExprPtr Parser::finishCall(ExprPtr callee) {
    auto call = std::make_unique<CallExpr>(callee->line, callee->column);
    call->callee = std::move(callee);
    while (!check(TokenType::RightParen)) {
        call->args.push_back(expression());
        if (!match(TokenType::Comma)) break;
    }
    consume(TokenType::RightParen, "')' after the arguments");
    return call;
}

ExprPtr Parser::finishIndex(ExprPtr object, const Token& bracket) {
    ExprPtr start;
    if (!check(TokenType::Colon)) {
        if (check(TokenType::RightBracket)) error(peek(), "expected an index between '[' and ']'");
        start = expression();
    }
    if (match(TokenType::Colon)) {
        auto slice = nodeAt<SliceExpr>(bracket);
        slice->object = std::move(object);
        slice->start = std::move(start);
        if (!check(TokenType::RightBracket)) slice->end = expression();
        consume(TokenType::RightBracket, "']' after the slice");
        return slice;
    }
    consume(TokenType::RightBracket, "']' after the index");
    auto index = nodeAt<IndexExpr>(bracket);
    index->object = std::move(object);
    index->index = std::move(start);
    return index;
}

ExprPtr Parser::primary() {
    const Token& token = peek();
    switch (token.type) {
        case TokenType::Int: {
            advance();
            auto literal = nodeAt<LiteralExpr>(token);
            literal->value = Value::fromInt(token.intValue);
            return literal;
        }
        case TokenType::Float: {
            advance();
            auto literal = nodeAt<LiteralExpr>(token);
            literal->value = Value::fromFloat(token.floatValue);
            return literal;
        }
        case TokenType::String: {
            advance();
            auto literal = nodeAt<LiteralExpr>(token);
            literal->value = Value::fromString(token.text);
            return literal;
        }
        case TokenType::True:
        case TokenType::False: {
            advance();
            auto literal = nodeAt<LiteralExpr>(token);
            literal->value = Value::fromBool(token.type == TokenType::True);
            return literal;
        }
        case TokenType::Null: {
            advance();
            return nodeAt<LiteralExpr>(token);
        }
        case TokenType::Identifier: {
            advance();
            auto variable = nodeAt<VariableExpr>(token);
            variable->name = token.lexeme;
            return variable;
        }
        case TokenType::LeftParen: {
            advance();
            ExprPtr inner = expression();
            consume(TokenType::RightParen, "')' to close the '(' on line " + std::to_string(token.line));
            return inner;
        }
        case TokenType::LeftBracket:
            advance();
            return listLiteral(token);
        case TokenType::LeftBrace:
            advance();
            return mapLiteral(token);
        case TokenType::Func:
            advance();
            if (check(TokenType::Identifier)) {
                error(peek(), "a named function can't be used as a value; write 'func " + peek().lexeme +
                                  "(...) { }' on its own line, or 'name := func(...) { }'");
            }
            return functionRest(token, "");
        default:
            error(token, "expected an expression, found " + describeToken(token));
    }
}

ExprPtr Parser::listLiteral(const Token& bracket) {
    auto list = nodeAt<ListExpr>(bracket);
    while (!check(TokenType::RightBracket)) {
        list->elements.push_back(expression());
        if (!match(TokenType::Comma)) break;
    }
    consume(TokenType::RightBracket, "']' to close the list that starts on line " + std::to_string(bracket.line));
    return list;
}

// {key: value, "other key": value, (expression): value}
// A bare identifier before ':' is a string key, like in JavaScript.
ExprPtr Parser::mapLiteral(const Token& brace) {
    auto map = nodeAt<MapExpr>(brace);
    skipNewlines();
    while (!check(TokenType::RightBrace)) {
        ExprPtr key;
        if (check(TokenType::Identifier) && peek(1).type == TokenType::Colon) {
            const Token& name = advance();
            auto literal = nodeAt<LiteralExpr>(name);
            literal->value = Value::fromString(name.lexeme);
            key = std::move(literal);
        } else {
            key = expression();
        }
        skipNewlines();
        consume(TokenType::Colon, "':' after the map key");
        skipNewlines();
        ExprPtr value = expression();
        map->entries.emplace_back(std::move(key), std::move(value));
        skipNewlines();
        if (!match(TokenType::Comma)) break;
        skipNewlines();
    }
    consume(TokenType::RightBrace, "'}' to close the map that starts on line " + std::to_string(brace.line));
    return map;
}

// ---------------------------------------------------------------------------
// Token helpers

const Token& Parser::peek(size_t ahead) const {
    return tokens_[std::min(current_ + ahead, tokens_.size() - 1)];
}

const Token& Parser::peekPastNewlines() const {
    size_t i = current_;
    while (i + 1 < tokens_.size() && tokens_[i].type == TokenType::Newline) ++i;
    return tokens_[i];
}

const Token& Parser::previous() const { return tokens_[current_ > 0 ? current_ - 1 : 0]; }

const Token& Parser::advance() {
    if (!isAtEnd()) ++current_;
    return previous();
}

bool Parser::check(TokenType type) const { return peek().type == type; }

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool Parser::matchAny(std::initializer_list<TokenType> types) {
    for (TokenType type : types) {
        if (match(type)) return true;
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& expected) {
    if (check(type)) return advance();
    error(peek(), "expected " + expected + ", found " + describeToken(peek()));
}

void Parser::skipNewlines() {
    while (check(TokenType::Newline)) advance();
}

void Parser::skipSeparators() {
    while (check(TokenType::Newline) || check(TokenType::Semicolon)) advance();
}

bool Parser::isAtEnd() const { return check(TokenType::Eof); }

void Parser::error(const Token& at, const std::string& message) {
    if (errors_.empty() && at.type == TokenType::Eof) incomplete_ = true;
    errors_.push_back({at.line, at.column, message});
    throw ParseError{};
}

}  // namespace ascend
