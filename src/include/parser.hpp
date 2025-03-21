#ifndef PARSER_HPP
#define PARSER_HPP

#include "lexer.hpp"
#include "symbol_table.hpp"
#include <iostream>

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
            // dont exit, just continue with the next token
            consume();
            return;
        }
        advance();
    }

    inline void parseAssignment() {
        std::string identifier = lexer.getIdentifier();
        consume();
        
        if (currentToken == Token::ASSIGN) {
            consume();
            int value = parseExpression();
            
            // store the value in the symbol table
            symbolTable.addSymbol(identifier, value);
        } else {
            std::cerr << "Syntax error: expected ':=' after identifier at line " << lexer.getLineNumber() << std::endl;
            exit(1);
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
                    exit(1);
                }
                result /= right;
            } else if (op == Token::MODULO) {
                if (right == 0) {
                    std::cerr << "Error: Modulo by zero at line " << lexer.getLineNumber() << std::endl;
                    exit(1);
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
                exit(1);
            }
            
            result = symbolTable.getValue(varName);
        } else if (currentToken == Token::LPAREN) {
            consume();  // eat '('
            result = parseExpression();
            
            if (currentToken != Token::RPAREN) {
                std::cerr << "Error: Expected ')' at line " << lexer.getLineNumber() << std::endl;
                exit(1);
            }
            
            consume();  // eat ')'
        } else {
            std::cerr << "Error: Unexpected token in expression at line " << lexer.getLineNumber() << std::endl;
            exit(1);
        }
        
        return result;
    }

    inline void parsePrint() {
        // eat print
        match(Token::PRINT);
        
        if (currentToken != Token::LPAREN) {
            std::cerr << "Error: Expected '(' after 'print' at line " << lexer.getLineNumber() << std::endl;
            exit(1);
        }
        consume(); // consume '('
        
        int value = 0;
        if (currentToken == Token::IDENTIFIER) {
            std::string varName = lexer.getIdentifier();
            consume();
            
            if (!symbolTable.hasSymbol(varName)) {
                std::cerr << "Error: Undefined variable '" << varName << "' at line " << lexer.getLineNumber() << std::endl;
                exit(1);
            }
            
            value = symbolTable.getValue(varName);
        } else {
            value = parseExpression();
        }
        
        if (currentToken != Token::RPAREN) {
            std::cerr << "Error: Expected ')' after expression in print() at line " << lexer.getLineNumber() << std::endl;
            exit(1);
        }
        consume(); // eat ')'
        
        std::cout << value << std::endl;
    }

    inline void parseLoop() {
        match(Token::FOR);
        
        if (currentToken != Token::IDENTIFIER) {
            std::cerr << "Error: Expected identifier after 'for' at line " << lexer.getLineNumber() << std::endl;
            exit(1);
        }
        
        std::string varName = lexer.getIdentifier();
        consume();
        
        if (currentToken != Token::IN) {
            std::cerr << "Error: Expected 'in' after loop variable at line " << lexer.getLineNumber() << std::endl;
            exit(1);
        }
        
        consume(); // eat 'in' token
        
        // range(5)
        if (currentToken == Token::RANGE) {
            parseRangeLoop(varName);
        }
    }

    inline void parseRangeLoop(const std::string& varName) {
        match(Token::RANGE);
        match(Token::LPAREN);

        int start = 0;
        int end;

        // parse first number
        end = parseExpression();
        
        // check if there is a comma for the two parameter format
        if (currentToken == Token::COMMA) {
            consume(); // eat the comma
            start = end;
            end = parseExpression(); // parse the end value
        }
        
        match(Token::RPAREN);
        match(Token::LBRACE);

        // store loop body start position
        size_t loopStart = lexer.getCurrentPosition();
        int braceLevel = 1;

        // skip to end of loop body first
        while (braceLevel > 0 && currentToken != Token::END) {
            if (currentToken == Token::LBRACE) braceLevel++;
            else if (currentToken == Token::RBRACE) braceLevel--;
            consume();
        }

        // store loop body end position
        size_t loopEnd = lexer.getCurrentPosition() - 1;

        for (int i = start; i < end; i++) {
            symbolTable.addSymbol(varName, i);
            
            // reset to start of loop body
            lexer.setPosition(loopStart);
            currentToken = lexer.getNextToken();
            
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
                    default:
                        consume();
                        break;
                }
            }
        }

        // pos lexer after the loop
        lexer.setPosition(loopEnd);
        match(Token::RBRACE);
    }
};

#endif // PARSER_HPP
