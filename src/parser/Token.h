#ifndef TOKEN_H
#define TOKEN_H

#include <string_view>
#include <string>

namespace iris::parser {
    /**
     * @brief Types of tokens for faster parsing.
     */
    enum class TokenKind {
        // Keywords
        IMPORT, FUN, VAR, VAL, IF, ELSE, WHILE, FOR, REPEAT, RETURN,
        CLASS, ABSTRACT, INTERFACE, ENUM, TRY, CATCH, THROW, SWITCH, CASE, DEFAULT,
        TRUE_VAL, FALSE_VAL, NULL_VAL, WAIT, NATIVE, FROM, AS,

        // Literals
        IDENTIFIER, NUMBER, STRING,

        // Delimiters & Operators
        LBRACE, RBRACE, LPAREN, RPAREN, LBRACKET, RBRACKET,
        COMMA, DOT, COLON, SEMICOLON,
        PLUS, MINUS, STAR, SLASH, PERCENT, EQUAL,
        EQ_EQ, NOT_EQ, LT, GT, LE, GE,
        AND, OR, NOT,
        BIT_AND, BIT_OR, BIT_XOR, SHL, SHR,

        UNKNOWN, EOF_TOKEN
    };

    /**
     * @brief Represents a single token in the source code with its location.
     */
    struct Token {
        std::string_view value;
        TokenKind type;
        int line;
        int column;
        std::string file;

        Token(std::string_view v, TokenKind t, int l, int c, std::string f)
            : value(v), type(t), line(l), column(c), file(std::move(f)) {}
    };
}

#endif //TOKEN_H
