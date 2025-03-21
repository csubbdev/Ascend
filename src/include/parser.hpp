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
        while (currentToken != Token::END_OF_FILE) {
            if (currentToken == Token::PRINT) {
                parsePrint();
            } else if (currentToken == Token::IDENTIFIER) {
                parseAssignment();
            } else {
                std::cerr << "Syntax error: unexpected token!" << std::endl;
                exit(1);
            }
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

    inline void match(Token token) {
        if (currentToken == token) {
            advance();
        } else {
            std::cerr << "Syntax error: expected " << tokenToString(token) << " but got " << tokenToString(currentToken) << std::endl;
            exit(1);
        }
    }

    inline void parseAssignment() {
        std::string identifier = lexer.getIdentifier();
        consume();
        
        if (currentToken == Token::EQUALS) {
            consume();
            // TODO: ADD LOGIC 
            if (currentToken == Token::NUMBER) {
                int value = lexer.getNumber();
                symbolTable.addSymbol(identifier, value);
                consume();
            } else {
                std::cerr << "Syntax error: expected a number after '='" << std::endl;
                exit(1);
            }
        } else {
            std::cerr << "Syntax error: expected '=' after identifier" << std::endl;
            exit(1);
        }
    }

    inline void parsePrint() {
        match(Token::PRINT);
        if (!checkToken(Token::LPAREN)) {
            std::cerr << "Error: Expected '(' after 'print'" << std::endl;
            exit(1);
        }

        match(Token::LPAREN);

        if (!checkToken(Token::IDENTIFIER)) {
            std::cerr << "Error: Expected identifier after '('" << std::endl;
            exit(1);
        }

        std::string varName = lexer.getIdentifier();
        advance();

        if (!checkToken(Token::RPAREN)) {
            std::cerr << "Error: Expected ')' after identifier" << std::endl;
            exit(1);
        }

        match(Token::RPAREN);
        if (!symbolTable.hasSymbol(varName)) {
            std::cerr << "Error: Undefined variable '" << varName << "'" << std::endl;
            exit(1);
        }
        std::cout << "Value: " << symbolTable.getValue(varName) << std::endl;
    }
};

#endif // PARSER_HPP
