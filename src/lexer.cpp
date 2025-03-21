#include "include/lexer.hpp"
#include <cctype>
#include <iostream>

// constructor: initialize lexer with the source code
Lexer::Lexer(const std::string &src) : source(src), index(0) {}

char Lexer::currentChar() {
    return index < source.length() ? source[index] : '\0';
}

void Lexer::advance() {
    index++;
}

Token Lexer::getNextToken() {
    // skip whitespace
    while (isspace(currentChar())) {
        advance();
    }

    if (currentChar() == '\0') return Token::END;

    char current = currentChar();

    // := operator (assignment)
    if (current == ':' && source[index + 1] == '=') {
        advance(); // skip ':'
        advance(); // skip '='
        return Token::ASSIGN;
    }

    if (current == '(') {
        advance();
        return Token::LPAREN;
    }
    if (current == ')') {
        advance();
        return Token::RPAREN;
    }

    // numbers (digits)
    if (isdigit(current)) {
        return Token::NUMBER;
    }

    // operators (plus and minus)
    if (current == '+') {
        advance();
        return Token::ADDITION;
    }
    if (current == '-') {
        advance();
        return Token::SUBTRACTION;
    }

    // identifiers (alphanumeric keywords or variable names)
    if (isalpha(current)) {
        std::string identifier;
        while (isalnum(currentChar())) { // get alphanumeric chars for identifiers
            identifier += currentChar();
            advance();
        }

        // keywords
        if (identifier == "print") return Token::PRINT;
        if (identifier == "if") return Token::IF;

        return Token::IDENTIFIER; 
    }

    // type declarations (int, float, etc.)
    if (current == 'i' && source.substr(index, 3) == "int") {
        advance(); advance(); advance(); // skip "int"
        return Token::TYPEDEF;
    }
    if (current == 'f' && source.substr(index, 4) == "float") {
        advance(); advance(); advance(); advance(); // skip "float"
        return Token::TYPEDEF;
    }

    // unknown token
    std::cerr << "Syntax error: unexpected token!" << std::endl;
    return Token::ERROR;
}

// get the identifier string (collects alphanumeric)
std::string Lexer::getIdentifier() {
    std::string identifier;
    while (isalnum(currentChar())) {
        identifier += currentChar();
        advance();
    }
    return identifier;
}

// get the number value (collects digits to form a number)
int Lexer::getNumber() {
    int number = 0;
    while (isdigit(currentChar())) {
        number = number * 10 + (currentChar() - '0');
        advance();
    }
    return number;
}
