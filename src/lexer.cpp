#include "include/lexer.hpp"
#include <cctype>
#include <iostream>

// Constructor: Initializes the lexer with the source code
Lexer::Lexer(const std::string &src) : source(src), index(0) {}

// Get the current character in the source string
char Lexer::currentChar() {
    return index < source.length() ? source[index] : '\0';
}

// Move to the next character in the source string
void Lexer::advance() {
    index++;
}

// Get the next token from the source string
Token Lexer::getNextToken() {
    // Skip any whitespace characters
    while (isspace(currentChar())) {
        advance();
    }

    // End of input
    if (currentChar() == '\0') return Token::END;

    char current = currentChar();

    // Handle the := operator (assignment)
    if (current == ':' && source[index + 1] == '=') {
        advance(); // Skip ':'
        advance(); // Skip '='
        return Token::ASSIGN;
    }

    // Handle parentheses
    if (current == '(') {
        advance();
        return Token::LPAREN;
    }
    if (current == ')') {
        advance();
        return Token::RPAREN;
    }

    // Handle numbers (digits)
    if (isdigit(current)) {
        return Token::NUMBER; // Numbers are handled here
    }

    // Handle operators (plus and minus)
    if (current == '+') {
        advance();
        return Token::ADDITION;
    }
    if (current == '-') {
        advance();
        return Token::SUBTRACTION;
    }

    // Handle identifiers (alphanumeric keywords or variable names)
    if (isalpha(current)) {
        std::string identifier;
        while (isalnum(currentChar())) { // Collect alphanumeric characters for identifiers
            identifier += currentChar();
            advance();
        }

        // Return token for known keywords
        if (identifier == "print") return Token::PRINT;
        if (identifier == "if") return Token::IF;

        // Return as a variable or function identifier
        return Token::IDENTIFIER; 
    }

    // Handle type declarations (e.g., int, float, etc.)
    if (current == 'i' && source.substr(index, 3) == "int") {
        advance(); advance(); advance(); // Skip "int"
        return Token::TYPEDEF;
    }
    if (current == 'f' && source.substr(index, 4) == "float") {
        advance(); advance(); advance(); advance(); // Skip "float"
        return Token::TYPEDEF;
    }

    // unknown token, return an error
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
