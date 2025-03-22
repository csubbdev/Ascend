#ifndef PARSER_HPP
#define PARSER_HPP

#include "lexer.hpp"
#include "symbol_table.hpp"
#include <iostream>

struct FunctionInfo {
    std::vector<std::string> parameters;
    size_t bodyStartPos;
    int bodyStartLine;
};

class Parser {
public:
    inline Parser(Lexer &lexer, SymbolTable &symbolTable)
        : lexer(lexer), symbolTable(symbolTable) {
        consume();
    }

    inline void parse() {
        try {
            while (currentToken != Token::END) {
                switch (currentToken) {
                    case Token::PRINT:
                        parsePrint();
                        break;
                    case Token::IDENTIFIER:
                        parseAssignment();
                        break;
                    case Token::FOR:
                        parseLoop();
                        break;
                    case Token::IF:
                        parseIf();
                        break;
                    case Token::WHILE:
                        parseWhile();
                        break;
                    case Token::RETURN:
                        parseReturn();
                        break;
                    case Token::FUNC:
                        parseFunction();
                        break;
                    default:
                        // skip comments and whitespace
                        consume();
                        break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during parsing: " << e.what() << std::endl;
        }
    }

private:
    Lexer &lexer;
    SymbolTable &symbolTable;
    Token currentToken;
    std::unordered_map<std::string, FunctionInfo> functions;
    
    inline void consume() {
        currentToken = lexer.getNextToken();
    }

    inline void advance() {
        currentToken = lexer.getNextToken();
    }

    inline bool checkToken(Token token) {
        return currentToken == token;
    }

    inline void match(Token expected) {
        if (currentToken != expected) {
            std::cerr << "Syntax error at line " << lexer.getLineNumber() 
                      << ": Expected " << tokenToString(expected)
                      << ", got " << tokenToString(currentToken) << std::endl;
            // skip current token and continue parsing
            consume();
            return;
        }
        advance();
    }

    inline void parseFunction() {
        match(Token::FUNC);
        std::string funcName = lexer.getIdentifier();
        consume();
        match(Token::LPAREN);

        std::vector<std::string> parameters;
        while (currentToken == Token::IDENTIFIER) {
            parameters.push_back(lexer.getIdentifier());
            consume();
            if (currentToken == Token::COMMA) consume();
        }
        match(Token::RPAREN);
        match(Token::LBRACE);

        size_t bodyStartPos = lexer.getCurrentPosition();
        int bodyStartLine = lexer.getLineNumber();
        int braceLevel = 1;
        while (braceLevel > 0 && currentToken != Token::END) {
            if (currentToken == Token::LBRACE) braceLevel++;
            if (currentToken == Token::RBRACE) braceLevel--;
            consume();
        }
        functions[funcName] = {parameters, bodyStartPos, bodyStartLine};
    }

    inline void parseFunctionCall(const std::string &funcName) {
        match(Token::LPAREN);
        std::vector<int> args;
        while (currentToken != Token::RPAREN && currentToken != Token::END) {
            args.push_back(parseExpression());
            if (currentToken == Token::COMMA) consume();
        }
        match(Token::RPAREN);

        auto it = functions.find(funcName);
        if (it == functions.end()) {
            std::cerr << "Undefined function " << funcName << std::endl;
            return;
        }
        FunctionInfo &func = it->second;
        if (args.size() != func.parameters.size()) {
            std::cerr << "Incorrect arguments for " << funcName << std::endl;
            return;
        }

        symbolTable.enterScope();
        for (size_t i = 0; i < args.size(); ++i)
            symbolTable.addSymbol(func.parameters[i], args[i]);

        size_t oldPos = lexer.getCurrentPosition();
        int oldLine = lexer.getLineNumber();
        Token oldToken = currentToken;

        lexer.setPosition(func.bodyStartPos);
        lexer.lineNumber = func.bodyStartLine;
        consume();
        parseBlock();

        lexer.setPosition(oldPos);
        lexer.lineNumber = oldLine;
        currentToken = oldToken;
        symbolTable.exitScope();
    }

    inline void parseReturn() {
        consume(); // eat 'return'
        int value = 0;
        if (currentToken != Token::SEMICOLON)
            value = parseExpression();
        std::cout << "Return: " << value << std::endl;
        // TODO: propagate the value back
        match(Token::SEMICOLON);
    }

    inline void parseAssignment() {
        std::string identifier = lexer.getIdentifier();
        int line = lexer.getLineNumber();
        consume(); // go past the identifier
        
        if (currentToken == Token::ASSIGN || 
            currentToken == Token::ASSIGN_SIMPLE ||
            currentToken == Token::PLUS_ASSIGN ||
            currentToken == Token::MINUS_ASSIGN ||
            currentToken == Token::MULTIPLY_ASSIGN ||
            currentToken == Token::DIVIDE_ASSIGN ||
            currentToken == Token::MODULO_ASSIGN) {
            Token assignmentToken = currentToken;
            consume(); // go past the assignment operator
            int value = parseExpression();
        
            // handle compound assignments
            if (assignmentToken!= Token::ASSIGN && assignmentToken!= Token::ASSIGN_SIMPLE) {
                if (!symbolTable.hasSymbol(identifier)) {
                    symbolTable.addSymbol(identifier, value);
                } else {
                    int currentValue = symbolTable.getValue(identifier);
                    switch (assignmentToken) {
                        case Token::PLUS_ASSIGN: value = currentValue + value; break;
                        case Token::MINUS_ASSIGN: value = currentValue - value; break;
                        case Token::MULTIPLY_ASSIGN: value = currentValue * value; break;
                        case Token::DIVIDE_ASSIGN: 
                            if (value == 0) {
                                std::cerr << "Error at line " << line << ": Division by zero" << std::endl;
                                return;
                            }
                            value = currentValue / value; 
                            break;
                        case Token::MODULO_ASSIGN: 
                            if (value == 0) {
                                std::cerr << "Error at line " << line << ": Modulo by zero" << std::endl;
                                return;
                            }
                            value = currentValue % value; 
                            break;
                        default: break;
                    }
                    symbolTable.addSymbol(identifier, value);
                }
            } else {
                // check if variable is declared
                if (assignmentToken == Token::ASSIGN_SIMPLE &&!symbolTable.hasSymbol(identifier)) {
                    std::cerr << "Error: Variable '" << identifier << "' must be declared with ':=' at line " << line << std::endl;
                    return;
                }
                symbolTable.addSymbol(identifier, value);
            }
        } else {
            std::cerr << "Syntax error: expected assignment operator at line " << line << std::endl;
            // skip to the next statement to recover
            while (currentToken!= Token::SEMICOLON && currentToken!= Token::END) {
                consume();
            }
            consume(); // eat the semicolon or END
        }
    }

    inline int parseExpression() {
        int result = parseTerm();
        
        while (currentToken == Token::PLUS || currentToken == Token::MINUS) {
            Token op = currentToken;
            consume();
            int right = parseTerm();
            
            if (op == Token::PLUS) {
                result += right;
            } else {
                result -= right;
            }
        }
        
        return result;
    }

    inline int parseTerm() {
        int result = parseFactor();
        
        while (currentToken == Token::MULTIPLY || currentToken == Token::DIVIDE || currentToken == Token::MODULO) {
            Token op = currentToken;
            consume();
            int right = parseFactor();
            
            if (op == Token::MULTIPLY) {
                result *= right;
            } else if (op == Token::DIVIDE) {
                if (right == 0) {
                    std::cerr << "Error: Division by zero at line " << lexer.getLineNumber() << std::endl;
                    return 0;
                }
                result /= right;
            } else if (op == Token::MODULO) {
                if (right == 0) {
                    std::cerr << "Error: Modulo by zero at line " << lexer.getLineNumber() << std::endl;
                    return 0;
                }
                result %= right;
            }
        }
        
        return result;
    }

    
    
    inline int parseFactor() {
        int result = 0;
        
        if (currentToken == Token::NUMBER) {
            result = lexer.getNumber();
            consume();
        } else if (currentToken == Token::IDENTIFIER) {
            std::string varName = lexer.getIdentifier();
            consume();
            
            if (!symbolTable.hasSymbol(varName)) {
                std::cerr << "Error: Undefined variable '" << varName << "' at line " << lexer.getLineNumber() << std::endl;
                return 0;
            }
            
            result = symbolTable.getValue(varName);
        } else if (currentToken == Token::LPAREN) {
            consume();  // eat '('
            result = parseExpression();
            
            if (currentToken!= Token::RPAREN) {
                std::cerr << "Error: Expected ')' at line " << lexer.getLineNumber() << std::endl;
                return 0;
            }
            
            consume();  // eat ')'
        }
        
        return result;
    }

    inline bool parseComparison() {
        bool left = parseLogicalAND();
        
        while (currentToken == Token::OR) {
            consume();
            bool right = parseLogicalAND();
            left = left || right;
        }
        
        return left;
    }

    inline bool parseLogicalAND() {
        bool left = parseLogicalNOT();
        
        while (currentToken == Token::AND) {
            consume();
            bool right = parseLogicalNOT();
            left = left && right;
        }
        
        return left;
    }

    inline bool parseLogicalNOT() {
        if (currentToken == Token::NOT) {
            consume();
            return !parseComparison();
        }
        return parseEquality();
    }

    inline bool parseEquality() {
        int left = parseExpression();
        Token op = currentToken;
        
        if (isComparisonOperator(op)) {
            consume();
            int right = parseExpression();
            return evaluateComparison(left, op, right);
        }
        
        // handle standalone values (e.g., "if (flag)")
        return static_cast<bool>(left);
    }

    inline bool isComparisonOperator(Token t) {
        return t == Token::GREATER_THAN || t == Token::LESSER_THAN ||
               t == Token::GREATER_THAN_OR_EQUAL || t == Token::LESSER_THAN_OR_EQUAL ||
               t == Token::EQUALS || t == Token::NOT_EQUALS;
    }

    inline bool evaluateComparison(int left, Token op, int right) {
        switch (op) {
            case Token::GREATER_THAN: return left > right;
            case Token::LESSER_THAN: return left < right;
            case Token::GREATER_THAN_OR_EQUAL: return left >= right;
            case Token::LESSER_THAN_OR_EQUAL: return left <= right;
            case Token::EQUALS: return left == right;
            case Token::NOT_EQUALS: return left != right;
            default: return false;
        }
    }

    inline void parsePrint() {
        // eat print
        match(Token::PRINT);
        
        if (currentToken!= Token::LPAREN) {
            std::cerr << "Error: Expected '(' after 'print' at line " << lexer.getLineNumber() << std::endl;
            // skip the current token and continue parsing
            consume();
            return;
        }
        consume(); // consume '('
        
        if (currentToken == Token::RPAREN) {  // empty print()
            std::cout << std::endl;
        } else if (currentToken == Token::IDENTIFIER) {
            std::string varName = lexer.getIdentifier();
            consume();
            
            if (!symbolTable.hasSymbol(varName)) {
                std::cerr << "Error: Undefined variable '" << varName << "' at line " << lexer.getLineNumber() << std::endl;
                consume();
                return;
            }
            
            int value = symbolTable.getValue(varName);
            
            if (currentToken!= Token::RPAREN) {
                std::cerr << "Error: Expected ')' after variable in print() at line " << lexer.getLineNumber() << std::endl;
                consume();
                return;
            }
            consume(); // eat ')'
            
            std::cout << value << std::endl;
        } else {
            int value = parseExpression();
            
            if (currentToken!= Token::RPAREN) {
                std::cerr << "Error: Expected ')' after expression in print() at line " << lexer.getLineNumber() << std::endl;
                consume();
                return;
            }
            consume(); // eat ')'
            
            std::cout << value << std::endl;
        }
    }
    
    inline void parseBlock() {
        match(Token::LBRACE);
        while (currentToken != Token::RBRACE && currentToken != Token::END) {
            switch (currentToken) {
                case Token::PRINT:
                    parsePrint();   
                    break;
                case Token::IDENTIFIER:
                    parseAssignment();
                    break;
                case Token::FOR:
                    parseLoop();
                    break;
                case Token::IF:
                    parseIf();
                    break;
                case Token::WHILE:
                    parseWhile();
                    break;
            default:
                consume();
                break;
            }
        }
        match(Token::RBRACE);
    }

    inline void parseIf() {
        match(Token::IF);
        match(Token::LPAREN);
        bool condition = parseComparison();
        match(Token::RPAREN);
        
        // check if the condition is true
        if (condition) {
            // 'if' parse
            if (currentToken!= Token::LBRACE) {
                std::cerr << "Error: Expected '{' after if condition at line " << lexer.getLineNumber() << std::endl;
                // recovery: skip to '{' if possible
                while (currentToken!= Token::LBRACE && currentToken!= Token::END) {
                    consume();
                }
            }
    
            if (currentToken == Token::LBRACE) {
                parseBlock();
            }
        } else {
            // skip the 'if' block if the condition is false
            if (currentToken == Token::LBRACE) {
                consume();
                int braceLevel = 1;
                while (braceLevel > 0 && currentToken!= Token::END) {
                    if (currentToken == Token::LBRACE) {
                        braceLevel++;
                    } else if (currentToken == Token::RBRACE) {
                        braceLevel--;
                    }
                    consume();
                }
                if (currentToken == Token::ELSE) {
                    consume(); // eat 'else'
    
                    if (currentToken == Token::IF) {
                        // 'else if'
                        parseIf(); // parse the 'else if' as another if statement recursively
                    } else {
                        // 'else'
                        if (currentToken!= Token::LBRACE) {
                            std::cerr << "Error: Expected '{' after else at line " << lexer.getLineNumber() << std::endl;
                            while (currentToken!= Token::LBRACE && currentToken!= Token::END) {
                                consume();
                            }
                        }
                        if (currentToken == Token::LBRACE) {
                            parseBlock();
                        }
                    }
                }
            } else {
                // 'else' without an 'if'
                if (currentToken == Token::ELSE) {
                    consume(); // eat 'else'
    
                    if (currentToken == Token::IF) {
                        // 'else if'
                        parseIf(); // parse the 'else if' as another if statement recursively
                    } else {
                        // 'else'
                        if (currentToken!= Token::LBRACE) {
                            std::cerr << "Error: Expected '{' after else at line " << lexer.getLineNumber() << std::endl;
                            while (currentToken!= Token::LBRACE && currentToken!= Token::END) {
                                consume();
                            }
                        }
                        if (currentToken == Token::LBRACE) {
                            parseBlock();
                        }
                    }
                }
            }
        }
    }

    inline void parseWhile() {
        match(Token::WHILE);
        match(Token::LPAREN); // Require '(' after while
        
        // Store positions for re-parsing
        size_t condStart = lexer.getCurrentPosition();
        int condLine = lexer.getLineNumber();
        
        // Initial condition check
        bool condition = parseComparison();
        match(Token::RPAREN); // Require ')'
        
        // Store loop body start
        size_t bodyStart = lexer.getCurrentPosition();
        int bodyLine = lexer.getLineNumber();
        
        while (condition) {
            // Parse loop body
            if (currentToken == Token::LBRACE) {
                // Store block start
                size_t blockStart = lexer.getCurrentPosition();
                int blockLine = lexer.getLineNumber();
                
                parseBlock();
                
                // Reset to block start for next iteration
                lexer.setPosition(blockStart);
                lexer.lineNumber = blockLine;
                currentToken = lexer.getNextToken();
            }
            
            // Re-check condition
            lexer.setPosition(condStart);
            lexer.lineNumber = condLine;
            currentToken = lexer.getNextToken();
            condition = parseComparison();
            match(Token::RPAREN);
        }
        
        // Skip past loop structure
        while (currentToken != Token::RBRACE && currentToken != Token::END) {
            consume();
        }
        if (currentToken == Token::RBRACE) consume();
    }

    inline void parseLoop() {
        match(Token::FOR);
        
        if (currentToken != Token::IDENTIFIER) {
            std::cerr << "Error: Expected identifier after 'for' at line " << lexer.getLineNumber() << std::endl;
            consume();
            return;
        }
        
        std::string varName = lexer.getIdentifier();
        consume();
        
        if (currentToken != Token::IN) {
            std::cerr << "Error: Expected 'in' after loop variable at line " << lexer.getLineNumber() << std::endl;
            consume();
            return;
        }
        
        consume(); // eat 'in' token
        
        if (currentToken == Token::RANGE) {
            parseRangeLoop(varName);
        }
    }

    inline void parseRangeLoop(const std::string& varName) {
        match(Token::RANGE);
        match(Token::LPAREN);
    
        int start = 0;
        int end = parseExpression();
        if (currentToken == Token::COMMA) {
            consume();
            start = end;
            end = parseExpression();
        }
        match(Token::RPAREN);
        match(Token::LBRACE);
    
        // store initial parser state
        size_t loopStart = lexer.getCurrentPosition();
        int initialLine = lexer.getLineNumber();
        Token initialToken = currentToken;
    
        for (int i = start; i < end; i++) {
            // reset state for each iteration
            lexer.setPosition(loopStart);
            lexer.lineNumber = initialLine;
            currentToken = initialToken;
            symbolTable.addSymbol(varName, i);
    
            // reparse the block
            int braceLevel = 1;
            while (braceLevel > 0 && currentToken != Token::END) {
                switch (currentToken) {
                    case Token::LBRACE: braceLevel++; break;
                    case Token::RBRACE: braceLevel--; break;
                    case Token::PRINT: parsePrint(); break;
                    case Token::IDENTIFIER: parseAssignment(); break;
                    case Token::FOR: parseLoop(); break;
                    case Token::IF: parseIf(); break;
                    case Token::WHILE: parseWhile(); break;
                    default: consume();
                }
                if (currentToken != Token::RBRACE) consume();
            }
        }
    
        // skip past the closing brace
        consume();
    }
};

#endif // PARSER_HPP
