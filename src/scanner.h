#ifndef NYX_SCANNER_H
#define NYX_SCANNER_H

typedef enum {
    // Single-character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_SEMICOLON, TOKEN_COLON,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,

    // One or two character tokens
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_AMPERSAND_AMPERSAND,
    TOKEN_PIPE_PIPE,
    TOKEN_ARROW,        // ->
    TOKEN_FAT_ARROW,    // =>
    TOKEN_DOT_DOT,      // ..
    TOKEN_QUESTION,     // ?
    TOKEN_DOT_DOT_DOT, // ...
    TOKEN_QUESTION_DOT, // ?.
    TOKEN_PLUS_EQUAL,   // +=
    TOKEN_MINUS_EQUAL,  // -=
    TOKEN_STAR_EQUAL,   // *=
    TOKEN_SLASH_EQUAL,  // /=
    TOKEN_PERCENT_EQUAL,// %=
    TOKEN_PLUS_PLUS,    // ++
    TOKEN_MINUS_MINUS,  // --
    TOKEN_PIPE,         // | (single pipe, for lambda params)

    // Literals
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_INTERPOLATION,
    TOKEN_INTEGER, TOKEN_FLOAT_LIT,

    // Keywords
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,         // keyword logical ops
    TOKEN_IS,                               // keyword equality
    TOKEN_IN,                               // membership

    TOKEN_IF, TOKEN_ELSE,
    TOKEN_WHILE, TOKEN_FOR, TOKEN_LOOP,
    TOKEN_FN, TOKEN_RETURN,
    TOKEN_LET, TOKEN_VAR, TOKEN_CONST,
    TOKEN_CLASS, TOKEN_SELF, TOKEN_SUPER, TOKEN_INIT,
    TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL,
    TOKEN_PRINT,
    TOKEN_MATCH,
    TOKEN_YIELD, TOKEN_RESUME,
    TOKEN_IMPORT, TOKEN_MODULE, TOKEN_FROM,
    TOKEN_ENUM,
    TOKEN_INSTANCEOF,
    TOKEN_BREAK, TOKEN_CONTINUE,

    TOKEN_ERROR,
    TOKEN_EOF,
} NyxTokenType;

typedef struct {
    NyxTokenType type;
    const char* start;
    int length;
    int line;
} NyxToken;

typedef struct {
    const char* start;
    const char* current;
    int line;
} NyxScanner;

void nyx_scanner_init(NyxScanner* scanner, const char* source);
NyxToken nyx_scanner_scan_token(NyxScanner* scanner);

#endif
