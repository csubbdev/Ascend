#include "include/lexer.hpp"
#include <cctype>
#include <iostream>

// constructor: initialize lexer with the source code
Lexer::Lexer(const std::string &src) : source(src), index(0), lineNumber(1), lastNumber(0) {}

char Lexer::currentChar() {
    return index < source.length() ? source[index] : '\0';
}

char Lexer::peekChar() {
    return index + 1 < source.length() ? source[index + 1] : '\0';
}

void Lexer::advance() {
    if (currentChar() == '\n') {
        lineNumber++;
    }
    index++;
}

Token Lexer::getNextToken() {
    // skip whitespace
    while (isspace(currentChar())) {
        advance();
    }

    // End of file
    if (currentChar() == '\0') {
        return Token::END;
    }

    // line comments
    if (currentChar() == '/' && peekChar() == '/') {
        // skip until end of line
        while (currentChar() != '\n' && currentChar() != '\0') {
            advance();
        }
        // get next token after comment recursively
        return getNextToken();
    }

    // multi-line comments
    if (currentChar() == '/' && peekChar() == '*') {
        advance(); // skip '/'
        advance(); // skip '*'
        
        while (true) {
            if (currentChar() == '\0') {
                std::cerr << "Error: Unterminated multi-line comment at line " << lineNumber << std::endl;
                exit(1);
            }
            if (currentChar() == '*' && peekChar() == '/') {
                advance(); // skip '*'
                advance(); // skip '/'
                break;
            }
            advance();
        }
        return getNextToken(); // recursively get next token after comment
    }

    // := operator (assignment)
    if (currentChar() == ':' && peekChar() == '=') {
        advance(); // skip ':'
        advance(); // skip '='
        return Token::ASSIGN;
    }

    // .. operator (range)
    if (currentChar() == '.' && peekChar() == '.') {
        advance(); // skip first '.'
        advance(); // skip second '.'
        return Token::DOTDOT;
    }

    // arithmetic operators
    if (currentChar() == '+') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::PLUS_ASSIGN;
        }
        return Token::PLUS;
    }
    
    if (currentChar() == '-') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::MINUS_ASSIGN;
        }
        return Token::MINUS;
    }
    
    if (currentChar() == '*') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::MULTIPLY_ASSIGN;
        }
        return Token::MULTIPLY;
    }
    
    if (currentChar() == '/') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::DIVIDE_ASSIGN;
        }
        return Token::DIVIDE;
    }
    
    if (currentChar() == '%') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::MODULO_ASSIGN;
        }
        return Token::MODULO;
    }

    if (currentChar() == '>') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::GREATER_THAN_OR_EQUAL;
        }
        return Token::GREATER_THAN;
    }

    if (currentChar() == '<') {
        advance();
        if (currentChar() == '=') {
            advance();
            return Token::MULTIPLY_ASSIGN;
        }
        return Token::MULTIPLY;
    }

    if (currentChar() == '&' && peekChar() == '&') {
        advance();
        advance();
        return Token::AND;
    }

    if (currentChar() == '|' && peekChar() == '|') {
        advance();
        advance();
        return Token::OR;
    }

    if (currentChar() == '!') {
        advance();
        return Token::NOT;
    }

    // parentheses and braces
    if (currentChar() == '(') {
        advance();
        return Token::LPAREN;
    }
    if (currentChar() == ')') {
        advance();
        return Token::RPAREN;
    }
    if (currentChar() == '{') {
        advance();
        return Token::LBRACE;
    }
    if (currentChar() == '}') {
        advance();
        return Token::RBRACE;
    }
    if (currentChar() == '[') {
        advance();
        return Token::LSQBRACE;
    }
    if (currentChar() == ']') {
        advance();
        return Token::RSQBRACE;
    }

    // misc
    if (currentChar() == ',') {
        advance();
        return Token::COMMA;
    }

    // numbers (digits)
    if (isdigit(currentChar())) {
        std::string number;
        while (isdigit(currentChar())) {
            number += currentChar();
            advance();
        }
        // store number for later retrieval
        lastNumber = std::stoi(number);
        return Token::NUMBER;
    }

    // identifiers and keywords
    if (isalpha(currentChar()) || currentChar() == '_') {
        std::string identifier;
        while (isalnum(currentChar()) || currentChar() == '_') {
            identifier += currentChar();
            advance();
        }
        
        // keywords
        if (identifier == "print") return Token::PRINT;
        if (identifier == "if") return Token::IF;
        if (identifier == "else") return Token::ELSE;
        if (identifier == "while") return Token::WHILE;
        if (identifier == "for") return Token::FOR;
        if (identifier == "in") return Token::IN;
        if (identifier == "range") return Token::RANGE;
        if (identifier == "and") return Token::AND;
        if (identifier == "or") return Token::OR;
        if (identifier == "not") return Token::NOT;
        if (identifier == "break") return Token::BREAK;
        if (identifier == "continue") return Token::CONTINUE;
        if (identifier == "try") return Token::TRY;
        if (identifier == "catch") return Token::CATCH;
        if (identifier == "finally") return Token::FINALLY;
        if (identifier == "null") return Token::NULLL;
        if (identifier == "char") return Token::CHAR;
        if (identifier == "string") return Token::STRING;
        if (identifier == "float") return Token::FLOAT;
        if (identifier == "double") return Token::DOUBLE;
        if (identifier == "int") return Token::INT;
        if (identifier == "null") return Token::NULLL;
        if (identifier == "char") return Token::CHAR;
        if (identifier == "switch") return Token::SWITCH;
        if (identifier == "default") return Token::DEFAULT;
        if (identifier == "goto") return Token::GOTO;
        if (identifier == "import") return Token::IMPORT;
        if (identifier == "do") return Token::DO;
        if (identifier == "foreach") return Token::FOREACH;
        if (identifier == "case") return Token::CASE;
        if (identifier == "return") return Token::RETURN;
        
        return Token::IDENTIFIER;
    }

    // unknown token
    std::cerr << "Syntax error: unexpected character '" << currentChar() << "' at line " << lineNumber << std::endl;
    advance(); // skip unrecognized character
    return getNextToken(); // try to get the next valid token
}

// get the identifier string (collects alphanumeric)
std::string Lexer::getIdentifier() {
    std::string identifier;
    while (isalnum(currentChar()) || currentChar() == '_') {
        identifier += currentChar();
        advance();
    }
    return identifier;
}

// get the number value (collects digits to form a number)
int Lexer::getNumber() {
    return lastNumber;
}
