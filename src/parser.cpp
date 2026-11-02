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
        case TokenType::AmpersandEqual:
        case TokenType::PipeEqual:
        case TokenType::CaretEqual:
        case TokenType::ShiftLeftEqual:
        case TokenType::ShiftRightEqual:
            return true;
        default:
            return false;
    }
}

bool isAssignable(const Expr& expr) {
    return expr.kind == ExprKind::Variable || expr.kind == ExprKind::Index || expr.kind == ExprKind::Member;
}

// Tokens that continue an expression after a closing parenthesis. Anything
// else after `if (...)` starts the body: `if (x > 0) print(x)`.
bool continuesExpression(TokenType type) {
    switch (type) {
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Star:
        case TokenType::StarStar:
        case TokenType::Slash:
        case TokenType::Percent:
        case TokenType::Ampersand:
        case TokenType::Pipe:
        case TokenType::Caret:
        case TokenType::ShiftLeft:
        case TokenType::ShiftRight:
        case TokenType::EqualEqual:
        case TokenType::NotEqual:
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual:
        case TokenType::In:
        case TokenType::And:
        case TokenType::Or:
        case TokenType::Question:
        case TokenType::Dot:
        case TokenType::LeftBracket:
        case TokenType::LeftParen:
            return true;
        default:
            return false;
    }
}

// Tokens that can start an operand, so `(i32) x` is a cast.
bool startsOperand(TokenType type) {
    switch (type) {
        case TokenType::Identifier:
        case TokenType::Int:
        case TokenType::Float:
        case TokenType::String:
        case TokenType::Char:
        case TokenType::FStringStart:
        case TokenType::LeftParen:
        case TokenType::LeftBracket:
        case TokenType::True:
        case TokenType::False:
        case TokenType::Null:
        case TokenType::Minus:
        case TokenType::Not:
        case TokenType::Tilde:
        case TokenType::PlusPlus:
        case TokenType::MinusMinus:
            return true;
        default:
            return false;
    }
}

// A name that can be used as a variable: "geometry", not "my-lib" or "if".
bool isIdentifierText(const std::string& text) {
    Lexer lexer(text);
    std::vector<Token> tokens = lexer.tokenize();
    return lexer.errors().empty() && !tokens.empty() && tokens[0].type == TokenType::Identifier &&
           tokens[0].lexeme == text;
}

template <typename T>
std::unique_ptr<T> nodeAt(const Token& token) {
    return std::make_unique<T>(token.line, token.column);
}

}  // namespace

// Limits how deeply blocks and expressions may nest, so pathological input
// produces an error instead of overflowing the stack. Both the nesting depth
// and the real stack use are checked, since stack frames differ in size
// between compilers.
class Parser::NestingGuard {
public:
    NestingGuard(Parser& parser, const Token& at) : parser_(parser) {
        if (parser_.depth_ >= kMaxNesting || parser_.stack_.exceeded()) parser_.error(at, "code is nested too deeply");
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

// Records facts about a block's statements that the interpreter relies on.
void Parser::noteStatement(Block& block, const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::Declare:
        case StmtKind::Import:
        case StmtKind::Enum:
            block.declaresNames = true;
            break;
        case StmtKind::Function:
        case StmtKind::Struct:
            block.declaresNames = true;
            block.hasFunctions = true;
            break;
        case StmtKind::Group:
            for (const auto& inner : stmt.as<GroupStmt>().statements) noteStatement(block, *inner);
            break;
        default:
            break;
    }
}

// Parses one statement into `block`, recovering from syntax errors.
void Parser::parseStatementInto(Block& block) {
    size_t start = current_;
    try {
        StmtPtr stmt = statement();
        noteStatement(block, *stmt);
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
        case TokenType::Const: advance(); return constDeclaration(token);
        case TokenType::Match: advance(); return matchStatement(token);
        case TokenType::Identifier: {
            // Words that only have a special meaning at the start of a statement,
            // so they can still be used as names elsewhere.
            TokenType next = peek(1).type;
            if (isWord(0, "import") && (next == TokenType::Identifier || next == TokenType::String)) {
                advance();
                return importStatement(token);
            }
            if (isWord(0, "from") && looksLikeFromImport()) {
                advance();
                return fromImportStatement(token);
            }
            if ((isWord(0, "struct") || isWord(0, "enum")) && next == TokenType::Identifier &&
                (peek(2).type == TokenType::LeftBrace || peek(2).type == TokenType::Newline)) {
                advance();
                return token.lexeme == "struct" ? structDeclaration(token) : enumDeclaration(token);
            }
            if (isWord(0, "switch") && looksLikeSwitch()) {
                advance();
                return switchStatement(token);
            }
            size_t afterDo = current_ + 1;
            while (afterDo + 1 < tokens_.size() && tokens_[afterDo].type == TokenType::Newline) ++afterDo;
            if (isWord(0, "do") && (tokens_[afterDo].type == TokenType::LeftBrace || next == TokenType::Identifier)) {
                advance();
                return doStatement(token);
            }
            if (isWord(0, "auto") && next == TokenType::Identifier) return typedDeclaration();
            if ((isWord(0, "case") && next != TokenType::Declare && !isAssignmentOperator(next)) ||
                (isWord(0, "default") && next == TokenType::Colon)) {
                error(token, "'" + token.lexeme + "' can only be used inside a switch");
            }

            // C-style declarations: `u8 age = 30`, `Point p = Point(1, 2)` and
            // functions `i32 add(i32 a, i32 b) { }`.
            size_t length = typeLength(0, true);
            if (length > 0 && peek(length).type == TokenType::Identifier && isDeclaration(length)) {
                if (peek(length + 1).type == TokenType::LeftParen) return typedFunctionDeclaration();
                return typedDeclaration();
            }
            if (next == TokenType::Colon) {
                // The other common style, `age: u8 = 30`: suggest the C-style form.
                std::string type = typeLength(2) > 0 ? peek(2).lexeme : "i32";
                error(token, "write the type before the name, like '" + type + " " + token.lexeme + " = ...'");
            }
            break;
        }
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
        case TokenType::Func: {
            // `func name(...)` declares a function; `func callback = ...` and
            // `func? callback` declare a variable that holds one.
            TokenType afterName = peek(2).type;
            bool variable = peek(1).type == TokenType::Question ||
                            (peek(1).type == TokenType::Identifier &&
                             (afterName == TokenType::Equal || afterName == TokenType::Newline ||
                              afterName == TokenType::Semicolon || afterName == TokenType::Eof ||
                              afterName == TokenType::RightBrace));
            if (variable) return typedDeclaration();
            if (peek(1).type == TokenType::Identifier) {
                advance();
                return functionDeclaration(token);
            }
            break;  // an anonymous function used as an expression
        }
        case TokenType::LeftBrace: {
            auto stmt = nodeAt<BlockStmt>(token);
            stmt->body = block("block");
            return stmt;
        }
        case TokenType::PlusPlus:
        case TokenType::MinusMinus:
            break;  // ++x as a statement
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
StmtPtr Parser::simpleStatement(bool requireEnd) {
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
        if (requireEnd) expectStatementEnd();
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
        if (requireEnd) expectStatementEnd();
        return stmt;
    }

    auto stmt = std::make_unique<ExpressionStmt>(expr->line, expr->column);
    stmt->expr = std::move(expr);
    if (requireEnd) expectStatementEnd();
    return stmt;
}

StmtPtr Parser::ifStatement(const Token& keyword) {
    auto stmt = nodeAt<IfStmt>(keyword);
    stmt->condition = condition("if");
    stmt->thenBody = body("'if' body");
    if (peekPastNewlines().type == TokenType::Else) {
        skipNewlines();
        const Token& elseToken = advance();
        if (check(TokenType::If)) {
            const Token& ifToken = advance();
            stmt->elseBranch = ifStatement(ifToken);
        } else {
            auto elseBlock = nodeAt<BlockStmt>(elseToken);
            elseBlock->body = body("'else' body");
            stmt->elseBranch = std::move(elseBlock);
        }
    }
    return stmt;
}

StmtPtr Parser::whileStatement(const Token& keyword) {
    auto stmt = nodeAt<WhileStmt>(keyword);
    stmt->condition = condition("while");
    stmt->body = body("'while' body");
    return stmt;
}

// do { ... } while (condition)
StmtPtr Parser::doStatement(const Token& keyword) {
    auto stmt = nodeAt<WhileStmt>(keyword);
    stmt->doWhile = true;
    stmt->body = body("'do' body");
    skipNewlines();
    consume(TokenType::While, "'while' and a condition after the 'do' body");
    stmt->condition = condition("while");
    expectStatementEnd();
    return stmt;
}

// for x in items { }         for (x in items) { }       for (string s : names) { }
// for (i32 i = 0; i < n; i++) { }                       for i := 0; i < n; i++ { }
// for { }  (loops until `break`)
StmtPtr Parser::forStatement(const Token& keyword) {
    if (check(TokenType::LeftBrace)) {
        auto stmt = nodeAt<CForStmt>(keyword);
        stmt->body = block("'for' body");
        return stmt;
    }
    bool parenthesized = false;
    if (check(TokenType::LeftParen)) {
        size_t close = matchingParen(current_);
        if (close != std::string::npos) {
            if (hasTopLevelSemicolon(current_ + 1, close)) return cForStatement(keyword, true);
            parenthesized = true;
        }
    } else {
        // Without parentheses the header ends at the body's '{' or the end of the line.
        size_t end = current_;
        int depth = 0;
        for (; end < tokens_.size(); ++end) {
            TokenType type = tokens_[end].type;
            if (type == TokenType::Eof || (depth == 0 && (type == TokenType::LeftBrace || type == TokenType::Newline))) {
                break;
            }
            if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace) ++depth;
            if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace) --depth;
        }
        if (hasTopLevelSemicolon(current_, end)) return cForStatement(keyword, false);
    }

    auto stmt = nodeAt<ForStmt>(keyword);
    if (parenthesized) advance();
    loopVariable(stmt->first, stmt->firstType);
    if (match(TokenType::Comma)) loopVariable(stmt->second, stmt->secondType);
    if (!match(TokenType::In) && !match(TokenType::Colon)) {
        error(peek(), "expected 'in' after the loop variable, found " + describeToken(peek()));
    }
    if (parenthesized) {
        stmt->iterable = expression();
        consume(TokenType::RightParen, "')' after the 'for' header");
    } else {
        stmt->iterable = condition("in");
    }
    stmt->body = body("'for' body");
    return stmt;
}

// A loop variable, optionally with a type: `item`, `string name`, `auto x`.
void Parser::loopVariable(std::string& name, TypeSpec& type) {
    if (isWord(0, "auto") && peek(1).type == TokenType::Identifier) {
        advance();
    } else {
        size_t length = typeLength(0, true);
        if (length > 0 && peek(length).type == TokenType::Identifier) type = parseType(false);
    }
    name = consume(TokenType::Identifier, "a loop variable name after 'for'").lexeme;
}

StmtPtr Parser::cForStatement(const Token& keyword, bool parenthesized) {
    auto stmt = nodeAt<CForStmt>(keyword);
    if (parenthesized) advance();
    if (!check(TokenType::Semicolon)) stmt->init = forClauses();
    consume(TokenType::Semicolon, "';' after the first part of the 'for' header");
    if (!check(TokenType::Semicolon)) stmt->condition = expression();
    consume(TokenType::Semicolon, "';' after the loop condition");
    TokenType end = parenthesized ? TokenType::RightParen : TokenType::LeftBrace;
    if (!check(end)) stmt->step = forClauses();
    if (parenthesized) consume(TokenType::RightParen, "')' after the 'for' header");
    int functionsBefore = functionCount_;
    stmt->body = body("'for' body");
    stmt->copyPerIteration = functionCount_ != functionsBefore;
    return stmt;
}

// The statements in the first or last part of a C-style for header,
// separated by commas: `i32 i = 0, j = n` or `i++, j--`.
std::vector<StmtPtr> Parser::forClauses() {
    std::vector<StmtPtr> clauses;
    do {
        size_t length = typeLength(0, true);
        bool declaration = (isWord(0, "auto") && peek(1).type == TokenType::Identifier) ||
                           (length > 0 && peek(length).type == TokenType::Identifier);
        clauses.push_back(declaration ? typedDeclaration(false) : simpleStatement(false));
    } while (match(TokenType::Comma));
    return clauses;
}

// switch (value) { case 1: ... break  default: ... }
StmtPtr Parser::switchStatement(const Token& keyword) {
    auto stmt = nodeAt<SwitchStmt>(keyword);
    stmt->subject = condition("switch");
    skipNewlines();
    const Token& open = consume(TokenType::LeftBrace, "'{' to start the switch cases");
    NestingGuard guard(*this, open);
    bool sawDefault = false;
    while (true) {
        skipSeparators();
        if (check(TokenType::RightBrace) || isAtEnd()) break;
        bool isCase = isWord(0, "case") && peek(1).type != TokenType::Declare && !isAssignmentOperator(peek(1).type);
        bool isDefault = isWord(0, "default") && peek(1).type == TokenType::Colon;
        if (!isCase && !isDefault) {
            if (stmt->cases.empty()) {
                errors_.push_back({peek().line, peek().column,
                                   "expected 'case' or 'default' in the switch, found " + describeToken(peek())});
                synchronize();
                continue;
            }
            parseStatementInto(stmt->body);
            continue;
        }
        size_t start = current_;
        try {
            const Token& label = advance();
            SwitchCase entry;
            entry.start = stmt->body.statements.size();
            if (isDefault) {
                if (sawDefault) error(label, "a switch can only have one 'default'");
                sawDefault = true;
            } else {
                do {
                    entry.values.push_back(expression());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::Colon, isDefault ? "':' after 'default'" : "':' after the case value");
            stmt->cases.push_back(std::move(entry));
        } catch (const ParseError&) {
            synchronize();
            if (current_ == start && !isAtEnd()) advance();
        }
    }
    if (!check(TokenType::RightBrace)) {
        error(peek(), "expected '}' to close the switch that starts on line " + std::to_string(open.line) +
                          ", found " + describeToken(peek()));
    }
    advance();
    return stmt;
}

// import geometry      import lib.shapes as shapes      import "../tools/x.asc" as x
StmtPtr Parser::importStatement(const Token& keyword) {
    auto stmt = nodeAt<ImportStmt>(keyword);
    do {
        const Token& start = peek();
        ImportName item;
        std::string lastPart;
        item.name = modulePath(lastPart);
        if (isWord(0, "as")) {
            advance();
            item.binding = consume(TokenType::Identifier, "a name after 'as'").lexeme;
        } else if (isIdentifierText(lastPart)) {
            item.binding = lastPart;
        } else {
            error(start, "can't use \"" + lastPart + "\" as a variable name; give the module a name with 'as', like: "
                         "import " + start.lexeme + " as name");
        }
        stmt->names.push_back(std::move(item));
    } while (match(TokenType::Comma));
    expectStatementEnd();
    return stmt;
}

// from geometry import area, Point as P      from "./helpers" import *
StmtPtr Parser::fromImportStatement(const Token& keyword) {
    auto stmt = nodeAt<ImportStmt>(keyword);
    stmt->from = true;
    std::string lastPart;
    stmt->fromPath = modulePath(lastPart);
    if (!isWord(0, "import")) error(peek(), "expected 'import' after the module name, found " + describeToken(peek()));
    advance();
    if (match(TokenType::Star)) {
        stmt->all = true;
    } else {
        bool parenthesized = match(TokenType::LeftParen);
        do {
            if (parenthesized && check(TokenType::RightParen)) break;  // trailing comma
            ImportName item;
            item.name = consume(TokenType::Identifier, "a name to import").lexeme;
            item.binding = item.name;
            if (isWord(0, "as")) {
                advance();
                item.binding = consume(TokenType::Identifier, "a name after 'as'").lexeme;
            }
            stmt->names.push_back(std::move(item));
        } while (match(TokenType::Comma));
        if (parenthesized) consume(TokenType::RightParen, "')' after the imported names");
    }
    expectStatementEnd();
    return stmt;
}

// A module: a dotted name (lib.shapes means the file lib/shapes.asc, or the
// package lib's file shapes.asc) or a path in a string. `lastPart` is set to
// the name the module is stored under by default.
std::string Parser::modulePath(std::string& lastPart) {
    if (check(TokenType::String)) {
        const Token& token = advance();
        std::string path = token.text;
        if (path.empty()) error(token, "the module path is empty");
        size_t slash = path.find_last_of("/\\");
        lastPart = slash == std::string::npos ? path : path.substr(slash + 1);
        size_t dot = lastPart.rfind('.');
        if (dot != std::string::npos && dot > 0) lastPart = lastPart.substr(0, dot);  // "shapes.asc"
        return path;
    }
    std::string path = consume(TokenType::Identifier, "a module name").lexeme;
    lastPart = path;
    while (check(TokenType::Dot) && peek(1).type == TokenType::Identifier) {
        advance();
        lastPart = advance().lexeme;
        path += "/" + lastPart;
    }
    return path;
}

// struct Point {
//     f64 x
//     f64 y = 0
// }
StmtPtr Parser::structDeclaration(const Token& keyword) {
    auto stmt = nodeAt<StructStmt>(keyword);
    stmt->name = consume(TokenType::Identifier, "a name after 'struct'").lexeme;
    skipNewlines();
    const Token& open = consume(TokenType::LeftBrace, "'{' to start the struct's fields");
    NestingGuard guard(*this, open);
    while (true) {
        skipSeparators();
        if (check(TokenType::RightBrace) || isAtEnd()) break;
        size_t start = current_;
        try {
            TypeSpec type;
            std::string typeText = "func";
            size_t length = typeLength(0, true);
            if (length > 0 && peek(length).type == TokenType::Identifier) {
                typeText = peek().lexeme;
                type = parseType(false);
            }
            do {
                const Token& name = consume(TokenType::Identifier, "a field name");
                if (check(TokenType::LeftParen)) {
                    error(peek(), "a struct can't contain functions; write it after the struct, like '" + typeText + " " +
                                      name.lexeme + "(" + stmt->name + " self) { ... }', and call it as value." +
                                      name.lexeme + "()");
                }
                if (check(TokenType::Colon)) {
                    error(peek(), "write the field's type before its name, like 'f64 " + name.lexeme + "'");
                }
                StructField field;
                field.name = name.lexeme;
                field.type = type;
                field.line = name.line;
                field.column = name.column;
                if (match(TokenType::Equal)) {
                    skipNewlines();
                    field.defaultValue = expression();
                }
                stmt->fields.push_back(std::move(field));
            } while (match(TokenType::Comma));
            expectStatementEnd();
        } catch (const ParseError&) {
            synchronize();
            if (current_ == start && !isAtEnd()) advance();
        }
    }
    if (!check(TokenType::RightBrace)) {
        error(peek(), "expected '}' to close the struct that starts on line " + std::to_string(open.line) +
                          ", found " + describeToken(peek()));
    }
    advance();
    return stmt;
}

// enum Color { Red, Green = 5, Blue }
StmtPtr Parser::enumDeclaration(const Token& keyword) {
    auto stmt = nodeAt<EnumStmt>(keyword);
    stmt->name = consume(TokenType::Identifier, "a name after 'enum'").lexeme;
    skipNewlines();
    const Token& open = consume(TokenType::LeftBrace, "'{' to start the enum's members");
    NestingGuard guard(*this, open);
    while (true) {
        skipSeparators();
        if (check(TokenType::RightBrace) || isAtEnd()) break;
        const Token& name = consume(TokenType::Identifier, "an enum member name");
        EnumMember member;
        member.name = name.lexeme;
        member.line = name.line;
        member.column = name.column;
        if (match(TokenType::Equal)) {
            skipNewlines();
            member.value = expression();
        }
        stmt->members.push_back(std::move(member));
        if (!match(TokenType::Comma) && !check(TokenType::Newline) && !check(TokenType::Semicolon) &&
            !check(TokenType::RightBrace)) {
            error(peek(), "expected ',' or '}' after the enum member, found " + describeToken(peek()));
        }
    }
    if (!check(TokenType::RightBrace)) {
        error(peek(), "expected '}' to close the enum that starts on line " + std::to_string(open.line) +
                          ", found " + describeToken(peek()));
    }
    advance();
    return stmt;
}

StmtPtr Parser::returnStatement(const Token& keyword) {
    auto stmt = nodeAt<ReturnStmt>(keyword);
    if (!check(TokenType::Newline) && !check(TokenType::Semicolon) && !check(TokenType::RightBrace) &&
        !check(TokenType::Else) && !isAtEnd()) {
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

// How many tokens the type name starting `offset` tokens ahead takes up
// (1 for `u8`, 2 for `string?`), or 0 if there is no type name there. With
// `allowStruct`, any other name counts as a (struct) type name too; callers
// check that a variable name follows.
size_t Parser::typeLength(size_t offset, bool allowStruct) const {
    const Token& token = peek(offset);
    TypeSpec type;
    bool isType = token.type == TokenType::Func ||
                  (token.type == TokenType::Identifier && (allowStruct || lookupType(token.lexeme, type)));
    if (!isType) return 0;
    return peek(offset + 1).type == TokenType::Question ? 2 : 1;
}

// After `Name name` or `Name? name` at the start of a statement: is it a
// declaration? `Point p` always is, but `ready ? go() : wait()` isn't.
bool Parser::isDeclaration(size_t typeTokens) const {
    TypeSpec builtin;
    if (typeTokens == 1 || peek().type == TokenType::Func || lookupType(peek().lexeme, builtin)) return true;
    TokenType after = peek(typeTokens + 1).type;
    if (after == TokenType::Equal || after == TokenType::Newline || after == TokenType::Semicolon ||
        after == TokenType::Eof || after == TokenType::RightBrace) {
        return true;
    }
    if (after != TokenType::LeftParen) return false;
    size_t close = matchingParen(current_ + typeTokens + 1);
    if (close == std::string::npos) return false;
    TokenType next = tokens_[close + 1].type;
    return next == TokenType::LeftBrace || next == TokenType::Newline;
}

std::string Parser::unknownTypeMessage(const std::string& name) {
    return "unknown type '" + name +
           "' (the types are: i8 i16 i32 i64 u8 u16 u32 u64 int byte short long f32 f64 float double bool char string "
           "list map func any, and the structs you declare)";
}

// type name = value   or   type name   (which starts at the type's zero value)
// Several at once, C style: i32 a = 1, b = 2.   auto name = value takes the
// value's type.
StmtPtr Parser::typedDeclaration(bool requireEnd) {
    const Token& start = peek();
    bool inferType = isWord(0, "auto");
    TypeSpec type;
    if (inferType) {
        advance();
    } else {
        type = parseType(false);
    }
    std::vector<StmtPtr> declarations;
    do {
        const Token& name = consume(TokenType::Identifier, "a variable name after the type");
        auto stmt = nodeAt<DeclareStmt>(declarations.empty() ? start : name);
        stmt->type = type;
        stmt->hasType = !inferType;
        stmt->inferType = inferType;
        stmt->name = name.lexeme;
        if (check(TokenType::Declare)) {
            error(peek(), "use '=' in a typed declaration, like '" + (inferType ? "auto" : type.name()) + " " +
                              name.lexeme + " = ...'");
        }
        if (match(TokenType::Equal)) {
            skipNewlines();
            stmt->value = expression();
        } else if (inferType) {
            error(peek(), "'auto " + name.lexeme + "' needs a value to take its type from, like 'auto " + name.lexeme +
                              " = 0'");
        }
        declarations.push_back(std::move(stmt));
    } while (match(TokenType::Comma));
    if (requireEnd) expectStatementEnd();
    if (declarations.size() == 1) return std::move(declarations[0]);
    auto group = nodeAt<GroupStmt>(start);
    group->statements = std::move(declarations);
    return group;
}

// A function with its return type in front, C style: `i32 add(i32 a, i32 b) { ... }`
StmtPtr Parser::typedFunctionDeclaration() {
    const Token& start = peek();
    TypeSpec returnType = parseType(true);
    const Token& name = advance();
    auto stmt = nodeAt<FunctionStmt>(start);
    stmt->function = functionRest(name, name.lexeme, &returnType);
    return stmt;
}

// const NAME = value   or   const type NAME = value
StmtPtr Parser::constDeclaration(const Token& keyword) {
    auto stmt = nodeAt<DeclareStmt>(keyword);
    stmt->constant = true;
    size_t length = typeLength(0, true);
    if (isWord(0, "auto") && peek(1).type == TokenType::Identifier) {
        advance();
        stmt->inferType = true;
    } else if (length > 0 && peek(length).type == TokenType::Identifier) {
        stmt->type = parseType(false);
        stmt->hasType = true;
    }
    const Token& name = consume(TokenType::Identifier, "a name after 'const'");
    stmt->name = name.lexeme;
    if (check(TokenType::Colon)) {
        std::string type = typeLength(1) > 0 ? peek(1).lexeme : "i32";
        error(peek(), "write the type before the name, like 'const " + type + " " + name.lexeme + " = ...'");
    }
    if (!match(TokenType::Equal) && !match(TokenType::Declare)) {
        error(peek(), "a constant needs a value: 'const " + name.lexeme + " = ...'");
    }
    skipNewlines();
    stmt->value = expression();
    expectStatementEnd();
    return stmt;
}

// match value {
//     1, 2 => print("small")
//     else => { print("other") }
// }
StmtPtr Parser::matchStatement(const Token& keyword) {
    auto stmt = nodeAt<MatchStmt>(keyword);
    stmt->subject = condition("match");
    skipNewlines();
    const Token& open = consume(TokenType::LeftBrace, "'{' to start the match arms");
    NestingGuard guard(*this, open);
    bool sawDefault = false;
    while (true) {
        skipSeparators();
        if (check(TokenType::RightBrace) || isAtEnd()) break;
        size_t start = current_;
        try {
            MatchArm arm;
            bool isDefault = check(TokenType::Else) ||
                             (check(TokenType::Identifier) && peek().lexeme == "_" && peek(1).type == TokenType::FatArrow);
            if (isDefault) {
                const Token& defaultToken = advance();
                if (sawDefault) error(defaultToken, "a match can only have one default ('else') arm");
                sawDefault = true;
            } else {
                do {
                    arm.patterns.push_back(expression());
                } while (match(TokenType::Comma));
            }
            consume(TokenType::FatArrow, "'=>' after the match pattern");
            skipNewlines();
            if (check(TokenType::LeftBrace)) {
                arm.body = block("match arm");
            } else {
                StmtPtr body = statement();
                noteStatement(arm.body, *body);
                arm.body.statements.push_back(std::move(body));
            }
            stmt->arms.push_back(std::move(arm));
        } catch (const ParseError&) {
            synchronize();
            if (current_ == start && !isAtEnd()) advance();
        }
    }
    if (!check(TokenType::RightBrace)) {
        error(peek(), "expected '}' to close the match that starts on line " + std::to_string(open.line) +
                          ", found " + describeToken(peek()));
    }
    advance();
    return stmt;
}

// A type name like `i32`, `string`, `u8?` (the ? allows null) or the name of
// a struct.
TypeSpec Parser::parseType(bool allowVoid, bool allowStruct) {
    const Token& token = peek();
    std::string name;
    if (token.type == TokenType::Identifier) {
        name = token.lexeme;
    } else if (token.type == TokenType::Func) {
        name = "func";
    } else {
        error(token, "expected a type name (like i32, f64, string or bool), found " + describeToken(token));
    }
    TypeSpec type;
    if (name == "auto") error(token, "'auto' can only be used to declare a variable with a value");
    if (!lookupType(name, type)) {
        if (!allowStruct) error(token, unknownTypeMessage(name));
        type = structType(name);  // checked once the whole program is parsed
    }
    if (type.kind == TypeKind::Void && !allowVoid) error(token, "'void' can only be used as a return type");
    advance();
    if (match(TokenType::Question)) {
        if (type.kind == TypeKind::Any || type.kind == TypeKind::Void) {
            error(previous(), "'" + name + "' can't be made nullable with '?'");
        }
        type.nullable = true;
    }
    return type;
}

StmtPtr Parser::functionDeclaration(const Token& keyword) {
    const Token& name = advance();
    auto stmt = nodeAt<FunctionStmt>(keyword);
    stmt->function = functionRest(name, name.lexeme);
    return stmt;
}

// Parameters, optional `-> type` and the body of a function. A typed
// parameter is written C style: `i32 count`.
std::unique_ptr<FunctionExpr> Parser::functionRest(const Token& keyword, const std::string& name,
                                                   const TypeSpec* returnType) {
    auto function = nodeAt<FunctionExpr>(keyword);
    ++functionCount_;
    function->name = name;
    if (returnType != nullptr) function->returnType = *returnType;
    consume(TokenType::LeftParen, name.empty() ? "'(' after 'func'" : "'(' after the function name");
    bool sawDefault = false;
    while (!check(TokenType::RightParen)) {
        Param parameter;
        size_t length = typeLength(0, true);
        if (length > 0 && peek(length).type == TokenType::Identifier) parameter.type = parseType(false);
        const Token& param = consume(TokenType::Identifier, "a parameter name");
        parameter.name = param.lexeme;
        if (check(TokenType::Colon)) {
            std::string type = typeLength(1) > 0 ? peek(1).lexeme : "i32";
            error(peek(), "write the parameter's type before its name, like '" + type + " " + param.lexeme + "'");
        }
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
    if (check(TokenType::Arrow)) {
        if (returnType != nullptr) error(peek(), "the return type is already written before the function name");
        advance();
        function->returnType = parseType(true);
    }
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

// The body of an if, else, while, for or do: a { block }, or C style a single
// statement (on the same line or the next), or `;` for nothing.
Block Parser::body(const std::string& what) {
    if (match(TokenType::Semicolon)) return Block();  // while (busy());
    skipNewlines();
    if (check(TokenType::LeftBrace)) return block(what);
    if (isAtEnd() || check(TokenType::RightBrace) || check(TokenType::Else)) {
        error(peek(), "expected '{' to start the " + what + ", found " + describeToken(peek()));
    }
    NestingGuard guard(*this, peek());
    Block result;
    StmtPtr stmt = statement();
    noteStatement(result, *stmt);
    result.statements.push_back(std::move(stmt));
    return result;
}

// The condition of an if, while, switch or match. When it's in parentheses,
// the closing ')' ends it, so the body can follow on the same line without
// braces: `if (x > 0) print(x)`.
ExprPtr Parser::condition(const std::string& keyword) {
    if (check(TokenType::LeftBrace) || check(TokenType::Newline) || isAtEnd()) {
        error(peek(), "expected an expression after '" + keyword + "', found " + describeToken(peek()));
    }
    if (check(TokenType::LeftParen)) {
        size_t close = matchingParen(current_);
        if (close != std::string::npos && !continuesExpression(tokens_[close + 1].type)) {
            const Token& open = advance();
            if (check(TokenType::RightParen)) error(peek(), "expected a condition between '(' and ')'");
            ExprPtr inner = expression();
            consume(TokenType::RightParen, "')' to close the '(' on line " + std::to_string(open.line));
            return inner;
        }
    }
    return expression();
}

// The index of the ')' matching the '(' at `open`, or npos.
size_t Parser::matchingParen(size_t open) const {
    int depth = 0;
    for (size_t i = open; i < tokens_.size(); ++i) {
        TokenType type = tokens_[i].type;
        if (type == TokenType::LeftParen) ++depth;
        if (type == TokenType::RightParen && --depth == 0) return i;
        if (type == TokenType::Eof) break;
    }
    return std::string::npos;
}

bool Parser::hasTopLevelSemicolon(size_t from, size_t to) const {
    int depth = 0;
    for (size_t i = from; i < to && i < tokens_.size(); ++i) {
        TokenType type = tokens_[i].type;
        if (type == TokenType::LeftParen || type == TokenType::LeftBracket || type == TokenType::LeftBrace) ++depth;
        if (type == TokenType::RightParen || type == TokenType::RightBracket || type == TokenType::RightBrace) --depth;
        if (type == TokenType::Semicolon && depth == 0) return true;
    }
    return false;
}

bool Parser::isWord(size_t ahead, const char* word) const {
    const Token& token = peek(ahead);
    return token.type == TokenType::Identifier && token.lexeme == word;
}

// from <module> import ...
bool Parser::looksLikeFromImport() const {
    size_t i = 1;
    if (peek(i).type == TokenType::String) {
        ++i;
    } else if (peek(i).type == TokenType::Identifier) {
        ++i;
        while (peek(i).type == TokenType::Dot && peek(i + 1).type == TokenType::Identifier) i += 2;
    } else {
        return false;
    }
    return isWord(i, "import");
}

// switch (x) {   or   switch x {   (and not a call to a function named switch)
bool Parser::looksLikeSwitch() const {
    TokenType next = peek(1).type;
    if (next == TokenType::LeftParen) {
        size_t close = matchingParen(current_ + 1);
        if (close == std::string::npos) return false;
        size_t i = close + 1;
        while (i + 1 < tokens_.size() && tokens_[i].type == TokenType::Newline) ++i;
        return tokens_[i].type == TokenType::LeftBrace;
    }
    return startsOperand(next) && next != TokenType::Minus && next != TokenType::PlusPlus &&
           next != TokenType::MinusMinus && next != TokenType::LeftBracket;
}

void Parser::expectStatementEnd() {
    if (match(TokenType::Newline) || match(TokenType::Semicolon)) return;
    if (check(TokenType::RightBrace) || isAtEnd()) return;
    if (check(TokenType::Else)) return;  // if (x) a() else b()
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
            case TokenType::Const:
            case TokenType::Match:
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
    if (check(TokenType::FormatSpec)) {
        error(peek(), "in an f-string, put a conditional expression in parentheses: {(a ? b : c)}");
    }
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
    return binary(&Parser::bitOr, {TokenType::Less, TokenType::LessEqual, TokenType::Greater,
                                   TokenType::GreaterEqual, TokenType::In});
}

// Bitwise operators bind tighter than comparisons (unlike C), so
// `flags & 1 == 0` means `(flags & 1) == 0`.
ExprPtr Parser::bitOr() { return binary(&Parser::bitXor, {TokenType::Pipe}); }

ExprPtr Parser::bitXor() { return binary(&Parser::bitAnd, {TokenType::Caret}); }

ExprPtr Parser::bitAnd() { return binary(&Parser::shift, {TokenType::Ampersand}); }

ExprPtr Parser::shift() { return binary(&Parser::additive, {TokenType::ShiftLeft, TokenType::ShiftRight}); }

ExprPtr Parser::additive() { return binary(&Parser::multiplicative, {TokenType::Plus, TokenType::Minus}); }

ExprPtr Parser::multiplicative() {
    return binary(&Parser::unary, {TokenType::Star, TokenType::Slash, TokenType::Percent});
}

ExprPtr Parser::unary() {
    // A minus directly before a number literal is part of the literal, so
    // `-128i8` works even though 128 alone doesn't fit in an i8. Not when the
    // number is used further: -2 ** 2 is -(2 ** 2).
    if (check(TokenType::Minus) && (peek(1).type == TokenType::Int || peek(1).type == TokenType::Float)) {
        TokenType after = peek(2).type;
        if (after != TokenType::StarStar && after != TokenType::Dot && after != TokenType::LeftParen &&
            after != TokenType::LeftBracket) {
            const Token& minus = advance();
            const Token& number = advance();
            ExprPtr literal = numberLiteral(number, true);
            auto expr = nodeAt<LiteralExpr>(minus);
            expr->value = literal->as<LiteralExpr>().value;
            return expr;
        }
    }
    if (check(TokenType::Minus) || check(TokenType::Not) || check(TokenType::Tilde)) {
        const Token& op = advance();
        NestingGuard guard(*this, op);
        auto expr = nodeAt<UnaryExpr>(op);
        expr->op = op.type;
        expr->operand = unary();
        return expr;
    }
    if (check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {
        const Token& op = advance();
        NestingGuard guard(*this, op);
        auto expr = nodeAt<IncDecExpr>(op);
        expr->increment = op.type == TokenType::PlusPlus;
        expr->prefix = true;
        expr->target = unary();
        if (!isAssignable(*expr->target)) {
            error(op, "'" + op.lexeme + "' needs a variable, element or field to change");
        }
        return expr;
    }
    // A C-style cast: (i32) x, (f64) total / count. It binds like a unary operator.
    if (check(TokenType::LeftParen) && peek(1).type == TokenType::Identifier && peek(2).type == TokenType::RightParen &&
        startsOperand(peek(3).type)) {
        TypeSpec type;
        bool castable = lookupType(peek(1).lexeme, type) &&
                        (type.kind == TypeKind::Number || type.kind == TypeKind::Bool || type.kind == TypeKind::Char ||
                         type.kind == TypeKind::String);
        if (castable) {
            const Token& open = advance();
            const Token& name = advance();
            advance();
            NestingGuard guard(*this, open);
            auto callee = nodeAt<VariableExpr>(name);
            callee->name = name.lexeme;
            auto call = nodeAt<CallExpr>(open);
            call->callee = std::move(callee);
            call->args.push_back(unary());
            return call;
        }
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
        } else if ((check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) && isAssignable(*expr)) {
            const Token& op = advance();
            auto incdec = std::make_unique<IncDecExpr>(expr->line, expr->column);
            incdec->increment = op.type == TokenType::PlusPlus;
            incdec->target = std::move(expr);
            return incdec;
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
        case TokenType::Int:
        case TokenType::Float:
            advance();
            return numberLiteral(token, false);
        case TokenType::Char: {
            advance();
            auto literal = nodeAt<LiteralExpr>(token);
            literal->value = Value::fromChar(token.charValue);
            return literal;
        }
        case TokenType::FStringStart:
            advance();
            return fString(token);
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

// Builds a number literal, checking that it fits its type.
ExprPtr Parser::numberLiteral(const Token& token, bool negate) {
    auto literal = nodeAt<LiteralExpr>(token);
    std::string text = (negate ? "-" : "") + token.lexeme;
    if (token.type == TokenType::Float) {
        literal->value = Value::fromFloat(negate ? -token.floatValue : token.floatValue, token.numKind);
        return literal;
    }

    const NumKind kind = token.numKind;
    const uint64_t magnitude = token.intValue;
    if (isUnsignedKind(kind)) {
        if (negate && magnitude != 0) error(token, text + " is negative, but " + numKindName(kind) + " is unsigned");
        if (magnitude > unsignedMax(kind)) {
            error(token, text + " doesn't fit in " + numKindName(kind) + " (" + rangeText(kind) + ")");
        }
        literal->value = Value::fromUInt(magnitude, kind);
        return literal;
    }

    const uint64_t limit = negate ? static_cast<uint64_t>(signedMax(kind)) + 1 : static_cast<uint64_t>(signedMax(kind));
    if (magnitude > limit) {
        if (kind == NumKind::Int) {
            error(token, text + " is too large for int (" + rangeText(kind) + ")" +
                             (negate ? "" : "; add a suffix to use another type, like " + token.lexeme + "u64"));
        }
        error(token, text + " doesn't fit in " + numKindName(kind) + " (" + rangeText(kind) + ")");
    }
    int64_t value = negate ? static_cast<int64_t>(0 - magnitude) : static_cast<int64_t>(magnitude);
    literal->value = Value::fromInt(value, kind);
    return literal;
}

// f"Hello, {name}!" was lexed into its literal text and interpolations.
ExprPtr Parser::fString(const Token& start) {
    auto expr = nodeAt<FStringExpr>(start);
    while (!check(TokenType::FStringEnd)) {
        if (check(TokenType::String)) {
            FStringPart part;
            part.text = advance().text;
            expr->parts.push_back(std::move(part));
            continue;
        }
        consume(TokenType::InterpStart, "'{' or the end of the f-string");
        FStringPart part;
        part.expr = expression();
        if (check(TokenType::FormatSpec)) part.spec = advance().text;
        if (!check(TokenType::InterpEnd)) {
            error(peek(), "expected '}' after the expression in the f-string, found " + describeToken(peek()) +
                              " (put a conditional expression in parentheses)");
        }
        advance();
        expr->parts.push_back(std::move(part));
    }
    advance();  // FStringEnd
    return expr;
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
