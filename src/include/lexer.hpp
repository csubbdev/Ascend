#ifndef LEXER_HPP
#define LEXER_HPP

#include <string>

// token types
enum class Token {
    PRINT,
    IF,
    ELSE,
    WHILE, 
    BREAK,
    CONTINUE,
    TUPLE,
    THROW,
    TRY,
    CATCH,
    FINALLY,
    PRIVATE,
    PUBLIC,
    PROTECTED,
    UNDERSCORE,
    HAS,
    HASH,
    ARROW,
    TERNARY,
    UNION,
    SWITCH,
    DEFAULT,
    GOTO,
    IMPORT,
    DO, 
    FOREACH, 
    FOR, 
    CASE, 
    RETURN, 
    FUNC, 
    IDENTIFIER, 
    ASSIGN,
    EQUALS,
    NOT_EQUALS, 
    LESSER_THAN, 
    GREATER_THAN, 
    LESSER_THAN_OR_EQUAL, 
    GREATER_THAN_OR_EQUAL, 
    AND,
    OR,
    NOT,
    COMMA,
    SEMICOLON,
    COLON,
    NUMBER,
    INT,
    FLOAT,
    DOUBLE,
    STATIC,
    EXTENDS,
    ABSTRACT,
    LONG,
    ULONG,
    SHORT,
    USHORT,
    VOID,
    BOOL,
    ARRAY,
    MAP,
    ENUM,
    NULLL,
    CHAR,
    STRING,
    BYTES,
    ADDITION,
    ADDITION_EQUAL,
    INCREMENT,
    DECREMENT,
    QUESTION_MARK,
    ASTERISK,
    SUBTRACTION, 
    SUBTRACTION_EQUAL,
    MULTIPLICATION,
    MULTIPLICATION_EQUAL,
    DIVISION,
    DIVISION_EQUAL,
    MODULUS,
    MODULUS_EQUAL,
    DOUBLE_QUOTES,
    SINGLE_QUOTES,
    LINE_COMMENT,
    BLOCK_COMMENT,
    LPAREN, 
    RPAREN, 
    LEFT_SQUARE_BRACKET,
    RIGHT_SQUARE_BRACKET,
    LEFT_CURLY_BRACKET,
    RIGHT_CURLY_BRACKET,
    CONST,
    STRUCT,
    CLASS,
    TYPEDEF,
    ERROR,
    INVALID,
    ESCAPE_SEQUENCE,
    FALSE,
    TRUE,
    SYNCHRONIZED,
    NEW,
    VOLATILE,
    CONDITIONAL_OPERATOR,
    TIME,
    DATE,
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    BITWISE_LEFT_SHIFT,
    BITWISE_RIGHT_SHIFT,
    EOF_TOKEN,
    END_OF_FILE,
    END
};

// convert token to its string representation
inline std::string tokenToString(Token t) {
    switch (t) {
        case Token::PRINT: return "PRINT";
        case Token::IF: return "IF";
        case Token::IDENTIFIER: return "IDENTIFIER";
        case Token::ASSIGN: return "ASSIGN";
        case Token::NUMBER: return "NUMBER";
        case Token::ADDITION: return "ADDITION";
        case Token::SUBTRACTION: return "SUBTRACTION";
        case Token::LPAREN: return "LPAREN";
        case Token::RPAREN: return "RPAREN";
        case Token::END: return "END";
        default: return "UNKNOWN";
    }
}

class Lexer {
public:
    Lexer(const std::string &src);
    Token getNextToken();
    char currentChar();
    void advance();
    std::string getIdentifier();
    int getNumber();
private:
    std::string source;
    size_t index;
};

#endif // LEXER_HPP