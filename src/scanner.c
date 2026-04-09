#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

void nyx_scanner_init(NyxScanner* scanner, const char* source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAtEnd(NyxScanner* scanner) {
    return *scanner->current == '\0';
}

static char advance(NyxScanner* scanner) {
    scanner->current++;
    return scanner->current[-1];
}

static char peek(NyxScanner* scanner) {
    return *scanner->current;
}

static char peekNext(NyxScanner* scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}

static bool match(NyxScanner* scanner, char expected) {
    if (isAtEnd(scanner)) return false;
    if (*scanner->current != expected) return false;
    scanner->current++;
    return true;
}

static NyxToken makeToken(NyxScanner* scanner, NyxTokenType type) {
    NyxToken token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int)(scanner->current - scanner->start);
    token.line = scanner->line;
    return token;
}

static NyxToken errorToken(NyxScanner* scanner, const char* message) {
    NyxToken token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner->line;
    return token;
}

static void skipWhitespace(NyxScanner* scanner) {
    for (;;) {
        char c = peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(scanner);
                break;
            case '\n':
                scanner->line++;
                advance(scanner);
                break;
            case '/':
                if (peekNext(scanner) == '/') {
                    // line comment — skip this rant
                    while (peek(scanner) != '\n' && !isAtEnd(scanner)) {
                        advance(scanner);
                    }
                } else if (peekNext(scanner) == '*') {
                    // block comment — supports nesting because we're not savages
                    advance(scanner);
                    advance(scanner);
                    int depth = 1;
                    while (depth > 0 && !isAtEnd(scanner)) {
                        if (peek(scanner) == '/' && peekNext(scanner) == '*') {
                            advance(scanner);
                            advance(scanner);
                            depth++;
                        } else if (peek(scanner) == '*' && peekNext(scanner) == '/') {
                            advance(scanner);
                            advance(scanner);
                            depth--;
                        } else {
                            if (peek(scanner) == '\n') scanner->line++;
                            advance(scanner);
                        }
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static NyxTokenType checkKeyword(NyxScanner* scanner, int start, int length,
                                  const char* rest, NyxTokenType type) {
    if (scanner->current - scanner->start == start + length &&
        memcmp(scanner->start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static NyxTokenType identifierType(NyxScanner* scanner) {
    switch (scanner->start[0]) {
        case 'a': return checkKeyword(scanner, 1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(scanner, 1, 4, "reak", TOKEN_BREAK);
        case 'c':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l': return checkKeyword(scanner, 2, 3, "ass", TOKEN_CLASS);
                    case 'o':
                        if (scanner->current - scanner->start > 3) {
                            switch (scanner->start[3]) {
                                case 's': return checkKeyword(scanner, 2, 3, "nst", TOKEN_CONST);
                                case 't': return checkKeyword(scanner, 2, 6, "ntinue", TOKEN_CONTINUE);
                            }
                        }
                        break;
                }
            }
            break;
        case 'e':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l': return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
                    case 'n': return checkKeyword(scanner, 2, 2, "um", TOKEN_ENUM);
                }
            }
            break;
        case 'f':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
                    case 'n':
                        if (scanner->current - scanner->start == 2) return TOKEN_FN;
                        break;
                    case 'o': return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
                    case 'r': return checkKeyword(scanner, 2, 2, "om", TOKEN_FROM);
                }
            }
            break;
        case 'i':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'f':
                        if (scanner->current - scanner->start == 2) return TOKEN_IF;
                        break;
                    case 'm': return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
                    case 'n':
                        if (scanner->current - scanner->start == 2) return TOKEN_IN;
                        return checkKeyword(scanner, 2, 2, "it", TOKEN_INIT);
                    case 's':
                        if (scanner->current - scanner->start == 2) return TOKEN_IS;
                        break;
                }
            }
            break;
        case 'l':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e': return checkKeyword(scanner, 2, 1, "t", TOKEN_LET);
                    case 'o': return checkKeyword(scanner, 2, 2, "op", TOKEN_LOOP);
                }
            }
            break;
        case 'm':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a': return checkKeyword(scanner, 2, 3, "tch", TOKEN_MATCH);
                    case 'o': return checkKeyword(scanner, 2, 4, "dule", TOKEN_MODULE);
                }
            }
            break;
        case 'n':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'i': return checkKeyword(scanner, 2, 1, "l", TOKEN_NIL);
                    case 'o': return checkKeyword(scanner, 2, 1, "t", TOKEN_NOT);
                }
            }
            break;
        case 'o': return checkKeyword(scanner, 1, 1, "r", TOKEN_OR);
        case 'p': return checkKeyword(scanner, 1, 4, "rint", TOKEN_PRINT);
        case 'r':
            if (scanner->current - scanner->start > 2) {
                switch (scanner->start[2]) {
                    case 't': return checkKeyword(scanner, 1, 5, "eturn", TOKEN_RETURN);
                    case 's': return checkKeyword(scanner, 1, 5, "esume", TOKEN_RESUME);
                }
            }
            break;
        case 's':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e': return checkKeyword(scanner, 2, 2, "lf", TOKEN_SELF);
                    case 'u': return checkKeyword(scanner, 2, 3, "per", TOKEN_SUPER);
                }
            }
            break;
        case 't': return checkKeyword(scanner, 1, 3, "rue", TOKEN_TRUE);
        case 'v': return checkKeyword(scanner, 1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(scanner, 1, 4, "hile", TOKEN_WHILE);
        case 'y': return checkKeyword(scanner, 1, 4, "ield", TOKEN_YIELD);
    }
    return TOKEN_IDENTIFIER;
}

static NyxToken identifier(NyxScanner* scanner) {
    while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) {
        advance(scanner);
    }
    return makeToken(scanner, identifierType(scanner));
}

static NyxToken number(NyxScanner* scanner) {
    bool isFloat = false;

    // hex literal — for people who think in base 16 like absolute psychos
    if (scanner->current[-1] == '0' && (peek(scanner) == 'x' || peek(scanner) == 'X')) {
        advance(scanner);
        while (isxdigit((unsigned char)peek(scanner))) advance(scanner);
        return makeToken(scanner, TOKEN_INTEGER);
    }

    while (isDigit(peek(scanner))) advance(scanner);

    // decimal point? you fancy
    if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
        isFloat = true;
        advance(scanner);
        while (isDigit(peek(scanner))) advance(scanner);
    }

    // scientific notation for the nerds
    if (peek(scanner) == 'e' || peek(scanner) == 'E') {
        isFloat = true;
        advance(scanner);
        if (peek(scanner) == '+' || peek(scanner) == '-') advance(scanner);
        while (isDigit(peek(scanner))) advance(scanner);
    }

    return makeToken(scanner, isFloat ? TOKEN_FLOAT_LIT : TOKEN_INTEGER);
}

static NyxToken string(NyxScanner* scanner) {
    int interpDepth = 0; // track ${} nesting depth so nested quotes don't break everything

    while (!isAtEnd(scanner)) {
        char c = peek(scanner);

        if (interpDepth > 0) {
            // inside ${...} — brace counting hell
            if (c == '{') interpDepth++;
            else if (c == '}') { interpDepth--; }
            else if (c == '\n') scanner->line++;
            advance(scanner);
            continue;
        }

        if (c == '"') break;
        if (c == '\n') scanner->line++;
        if (c == '\\') { advance(scanner); advance(scanner); continue; }
        if (c == '$' && peekNext(scanner) == '{') {
            advance(scanner); // skip $
            advance(scanner); // skip {
            interpDepth = 1;
            continue;
        }
        advance(scanner);
    }

    // you forgot to close your string. we're not your IDE
    if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

    advance(scanner);
    return makeToken(scanner, TOKEN_STRING);
}

NyxToken nyx_scanner_scan_token(NyxScanner* scanner) {
    skipWhitespace(scanner);
    scanner->start = scanner->current;

    if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

    char c = advance(scanner);

    if (isAlpha(c)) return identifier(scanner);
    if (isDigit(c)) return number(scanner);

    switch (c) {
        case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
        case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
        case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
        case '}': return makeToken(scanner, TOKEN_RIGHT_BRACE);
        case '[': return makeToken(scanner, TOKEN_LEFT_BRACKET);
        case ']': return makeToken(scanner, TOKEN_RIGHT_BRACKET);
        case ',': return makeToken(scanner, TOKEN_COMMA);
        case '.':
            if (match(scanner, '.')) {
                return match(scanner, '.') ? makeToken(scanner, TOKEN_DOT_DOT_DOT)
                                           : makeToken(scanner, TOKEN_DOT_DOT);
            }
            return makeToken(scanner, TOKEN_DOT);
        case ';': return makeToken(scanner, TOKEN_SEMICOLON);
        case ':': return makeToken(scanner, TOKEN_COLON);
        case '+':
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_PLUS_EQUAL);
            if (match(scanner, '+')) return makeToken(scanner, TOKEN_PLUS_PLUS);
            return makeToken(scanner, TOKEN_PLUS);
        case '-':
            if (match(scanner, '>')) return makeToken(scanner, TOKEN_ARROW);
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_MINUS_EQUAL);
            if (match(scanner, '-')) return makeToken(scanner, TOKEN_MINUS_MINUS);
            return makeToken(scanner, TOKEN_MINUS);
        case '*':
            return match(scanner, '=') ? makeToken(scanner, TOKEN_STAR_EQUAL)
                                       : makeToken(scanner, TOKEN_STAR);
        case '/':
            return match(scanner, '=') ? makeToken(scanner, TOKEN_SLASH_EQUAL)
                                       : makeToken(scanner, TOKEN_SLASH);
        case '%':
            return match(scanner, '=') ? makeToken(scanner, TOKEN_PERCENT_EQUAL)
                                       : makeToken(scanner, TOKEN_PERCENT);
        case '?':
            return match(scanner, '.') ? makeToken(scanner, TOKEN_QUESTION_DOT)
                                       : makeToken(scanner, TOKEN_QUESTION);

        case '!':
            return match(scanner, '=') ? makeToken(scanner, TOKEN_BANG_EQUAL)
                                       : makeToken(scanner, TOKEN_BANG);
        case '=':
            if (match(scanner, '=')) return makeToken(scanner, TOKEN_EQUAL_EQUAL);
            if (match(scanner, '>')) return makeToken(scanner, TOKEN_FAT_ARROW);
            return makeToken(scanner, TOKEN_EQUAL);
        case '<':
            return match(scanner, '=') ? makeToken(scanner, TOKEN_LESS_EQUAL)
                                       : makeToken(scanner, TOKEN_LESS);
        case '>':
            return match(scanner, '=') ? makeToken(scanner, TOKEN_GREATER_EQUAL)
                                       : makeToken(scanner, TOKEN_GREATER);

        case '&':
            if (match(scanner, '&')) return makeToken(scanner, TOKEN_AMPERSAND_AMPERSAND);
            return errorToken(scanner, "Unexpected character '&'. Did you mean '&&'?");
        case '|':
            if (match(scanner, '|')) return makeToken(scanner, TOKEN_PIPE_PIPE);
            return makeToken(scanner, TOKEN_PIPE);

        case '"':
            // triple-quote """multi-line""" — because sometimes strings have feelings too
            if (peek(scanner) == '"' && peekNext(scanner) == '"') {
                advance(scanner); advance(scanner);
                while (!isAtEnd(scanner)) {
                    if (peek(scanner) == '\n') scanner->line++;
                    if (peek(scanner) == '"') {
                        if (scanner->current[1] == '"' && scanner->current[2] == '"') {
                            advance(scanner); advance(scanner); advance(scanner);
                            return makeToken(scanner, TOKEN_STRING);
                        }
                    }
                    advance(scanner);
                }
                return errorToken(scanner, "Unterminated multi-line string.");
            }
            return string(scanner);
    }

    return errorToken(scanner, "Unexpected character.");
}
