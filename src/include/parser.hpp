#ifndef PARSER_HPP
#define PARSER_HPP

#include "include/lexer.hpp"
#include "include/symbol_table.hpp"

class Parser {
public:
    Parser(Lexer &lexer, SymbolTable &symbolTable);
    void parse();

private:
    Lexer &lexer;
    SymbolTable &symbolTable;
    Token currentToken;
    
    void consume();
    void parseAssignment();
};

Parser::Parser(Lexer &lexer, SymbolTable &symbolTable)
    : lexer(lexer), symbolTable(symbolTable), currentToken(lexer.getNextToken()) {}

void Parser::consume() {
    currentToken = lexer.getNextToken();
}

void Parser::parseAssignment() {
    if (currentToken == Token::IDENTIFIER) {
        std::string variableName = "var";  // Just a placeholder for the variable name

        consume();  // Move past the identifier
        
        if (currentToken == Token::ASSIGN) {
            consume();  // Move past the '='
            
            if (currentToken == Token::NUMBER) {
                std::cout << "Assigning value to " << variableName << std::endl;
                consume();  // Move past the number
            }
        }
    }
}

void Parser::parse() {
    while (currentToken != Token::EOF_TOKEN) {
        if (currentToken == Token::IDENTIFIER) {
            parseAssignment();
        } else {
            std::cerr << "Syntax error: unexpected token!" << std::endl;
            consume();  // Move past the error
        }
    }
}

#endif // PARSER_HPP
