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
    RANGE,
    DOTDOT,
    IN,
    FUNC, 
    IDENTIFIER,
    NUMBER,
    ASSIGN,
    ASSIGN_SIMPLE,
    EQUALS,
    NOT_EQUALS,
    LESSER_THAN, 
    GREATER_THAN, 
    LESSER_THAN_OR_EQUAL, 
    GREATER_THAN_OR_EQUAL,
    AND,
    OR,
    NOT,
    PLUS,
    PLUS_ASSIGN,
    MINUS,
    MINUS_ASSIGN,
    MULTIPLY,
    MULTIPLY_ASSIGN,
    DIVIDE,
    DIVIDE_ASSIGN,
    MODULO,
    MODULO_ASSIGN,
    LPAREN,
    RPAREN,
    COMMA,
    COLON,
    END,
    ERROR,
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
    ADDITION_EQUAL,
    INCREMENT,
    DECREMENT,
    QUESTION_MARK,
    ASTERISK,
    SUBTRACTION_EQUAL,
    MULTIPLICATION_EQUAL,
    DIVISION_EQUAL,
    MODULUS_EQUAL,
    DOUBLE_QUOTES,
    SINGLE_QUOTES,
    LINE_COMMENT,
    BLOCK_COMMENT,
    LSQBRACE,
    RSQBRACE,
    LBRACE,
    RBRACE,
    CONST,
    STRUCT,
    CLASS,
    TYPEDEF,
    SEMICOLON,
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
    EOF_TOKEN
};

// convert token to string representation
inline std::string tokenToString(Token t) {
    switch (t) {
        case Token::PRINT: return "PRINT";
        case Token::SEMICOLON: return "SEMICOLON";
        case Token::IF: return "IF";
        case Token::IDENTIFIER: return "IDENTIFIER";
        case Token::ASSIGN_SIMPLE: return "ASSIGN_SIMPLE";
        case Token::ASSIGN: return "ASSIGN";
        case Token::NUMBER: return "NUMBER";
        case Token::PLUS: return "PLUS";
        case Token::MINUS: return "MINUS";
        case Token::MULTIPLY: return "MULTIPLY";
        case Token::DIVIDE: return "DIVIDE";
        case Token::MODULO: return "MODULO";
        case Token::LPAREN: return "LPAREN";
        case Token::RPAREN: return "RPAREN";
        case Token::LSQBRACE: return "LSQBRACE";
        case Token::RSQBRACE: return "RSQBRACE";
        case Token::LBRACE: return "LBRACE";
        case Token::RBRACE: return "RBRACE";
        case Token::GREATER_THAN_OR_EQUAL: return "GREATER_THAN_OR_EQUAL";
        case Token::LESSER_THAN_OR_EQUAL: return "LESSER_THAN_OR_EQUAL";
        case Token::GREATER_THAN: return "GREATER_THAN";
        case Token::LESSER_THAN: return "LESSER_THAN";
        case Token::AND: return "AND";
        case Token::OR: return "OR";
        case Token::NOT: return "NOT";
        case Token::RETURN: return "RETURN";
        case Token::FUNC: return "FUNC";
        case Token::END: return "END";
        default: return "UNKNOWN";
    }
}

class Lexer {
public:
    Lexer(const std::string &src);
    
    Token getNextToken();
    char currentChar(); 
    char peekChar();
    void advance();
    std::string getIdentifier();
    int getNumber();
    std::string getString();
    int lineNumber;
    int getLineNumber() { return lineNumber; }
    
    // add pos tracking methods
    size_t getCurrentPosition() const { return index; }
    void setPosition(size_t pos) { 
        if (pos <= source.length()) {
            index = pos;
        }
    }

private:
    std::string source;
    size_t index;
    int lastNumber;
    std::string lastString;
    std::string lastIdentifier;
};

#endif // LEXER_HPP