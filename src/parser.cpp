#include "include/lexer.hpp"
#include "include/symbol_table.hpp"
#include "include/parser.hpp"

//analyzing the syntax of the tokens
class Parser {
private:
    Lexer &lexer;
    Token currentToken;
    SymbolTable &symbolTable;
 
    // Move to the next token
    void advance() {
        currentToken = lexer.getNextToken();
    }
 
    // Ensure the current token matches the expected token
    void match(Token token) {
        if (currentToken == token) {
            advance();
        } else {
            std::cerr << "Syntax error: expected " << tokenToString(token) << " but got " << tokenToString(currentToken) << std::endl;
            exit(1);
        }
    }
 
    // Check if the current token matches the expected token
    bool checkToken(Token token) {
        return currentToken == token;
    }
 
public:
    Parser(Lexer &lex, SymbolTable &symbolTbl) : lexer(lex), symbolTable(symbolTbl) {
        advance(); // Initialize with the first token
    }
 
    // Start parsing the code
    void parse() {
        if (currentToken == Token::END) {
            std::cerr << "Error: Empty input!" << std::endl;
            exit(1);
        }
 
        // Parse the code based on different token types
        while (currentToken != Token::END) {
            if (currentToken == Token::PRINT) {
                parsePrint();
            } else if (currentToken == Token::IDENTIFIER) {
                std::cout << "Identifier found: " << lexer.getIdentifier() << std::endl;
                advance();
            } else if (currentToken == Token::NUMBER) {
                std::cout << "Number found: " << lexer.getNumber() << std::endl;
                advance();
            } else {
                std::cerr << "Syntax error: unexpected token!" << std::endl;
                exit(1);
            }
        }
    }

    // Parse 'print' statements and handle variable printing
    void parsePrint() {
        match(Token::PRINT);
        if (!checkToken(Token::LPAREN)) {
            std::cerr << "Error: Expected '(' after 'print'!" << std::endl;
            exit(1);
        }

        match(Token::LPAREN);

        if (!checkToken(Token::IDENTIFIER)) {
            std::cerr << "Error: Expected identifier after '('!" << std::endl;
            exit(1);
        }

        std::string varName = lexer.getIdentifier();
        advance();

        if (!checkToken(Token::RPAREN)) {
            std::cerr << "Error: Expected ')' after identifier!" << std::endl;
            exit(1);
        }

        match(Token::RPAREN);
        std::cout << "Print statement parsed! Printing: " << varName << std::endl;
        std::cout << "Value: " << symbolTable.getVariable(varName) << std::endl; // Print variable value
    }
};