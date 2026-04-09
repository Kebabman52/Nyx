#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "repl.h"
#include "scanner.h"
#include "vm.h"

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <wchar.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

// Configuration (tweak these if you're feeling brave)

#define REPL_LINE_MAX   4096
#define REPL_BUFFER_MAX 65536
#define HISTORY_MAX     256
#define TAB_WIDTH       4

// ANSI Color Codes (terminal eye candy)

#define COL_RESET    "\033[0m"
#define COL_KEYWORD  "\033[36m"    // cyan
#define COL_STRING   "\033[32m"    // green
#define COL_NUMBER   "\033[33m"    // yellow
#define COL_COMMENT  "\033[90m"    // gray
#define COL_TYPE     "\033[35m"    // magenta
#define COL_BOOL     "\033[33m"    // yellow (like numbers)
#define COL_ERROR    "\033[31m"    // red
#define COL_PROMPT   "\033[1;34m"  // bold blue
#define COL_CONT     "\033[90m"    // gray for ...
#define COL_IDENT    "\033[37m"    // white (brighter than default)
#define COL_FUNC     "\033[1;33m"  // bold yellow for function names
#define COL_PUNCT    "\033[90m"    // gray for brackets/punctuation
#define COL_OP       "\033[91m"    // bright red for operators

// Platform: Raw Terminal Mode (taming the terminal)

#ifdef _WIN32

static HANDLE hStdOut;
static DWORD origOutMode;
static bool rawModeActive = false;

static void disableRawMode(void) {
    if (rawModeActive) {
        SetConsoleMode(hStdOut, origOutMode);
        rawModeActive = false;
    }
}

static void enableRawMode(void) {
    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(hStdOut, &origOutMode);
    SetConsoleMode(hStdOut, origOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    rawModeActive = true;
    // restore terminal on exit, even if we crash
    atexit(disableRawMode);
}

// key codes >255 to avoid ASCII collisions
enum {
    KEY_ENTER = 13,
    KEY_TAB = 9,
    KEY_BACKSPACE = 8,
    KEY_ESCAPE = 27,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_UP = 1000,
    KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_DELETE,
};

static int readKey(void) {
    // _getwch() handles keyboard layout madness (dead keys, AltGr, etc.)
    // returns the actual unicode codepoint, not raw scan codes. thank god
    wint_t ch = _getwch();

    // extended keys: 0 or 0xE0 prefix, then the actual code
    if (ch == 0 || ch == 0xE0) {
        wint_t ext = _getwch();
        switch (ext) {
            case 72:  return KEY_UP;
            case 80:  return KEY_DOWN;
            case 75:  return KEY_LEFT;
            case 77:  return KEY_RIGHT;
            case 71:  return KEY_HOME;
            case 79:  return KEY_END;
            case 83:  return KEY_DELETE;
            default:  return -1; // ignore unknown extended key
        }
    }

    if (ch == 3)    return KEY_CTRL_C;
    if (ch == 4)    return KEY_CTRL_D;
    if (ch == '\b') return KEY_BACKSPACE;
    if (ch == '\r') return KEY_ENTER;
    if (ch == '\t') return KEY_TAB;
    if (ch == 27)   return KEY_ESCAPE;
    if (ch >= 32)   return (int)ch;

    return -1; // ignore other control chars
}

#else // POSIX

static struct termios origTermios;

static void disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
}

static void enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &origTermios);
    struct termios raw = origTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

enum {
    KEY_ENTER = '\r',
    KEY_TAB = '\t',
    KEY_BACKSPACE = 127,
    KEY_ESCAPE = 27,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_UP = 1000,
    KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_HOME, KEY_END, KEY_DELETE,
};

static int readKey(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return KEY_CTRL_D;

    if (c == KEY_ESCAPE) {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return KEY_ESCAPE;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return KEY_ESCAPE;
        if (seq[0] == '[') {
            if (seq[1] == 'A') return KEY_UP;
            if (seq[1] == 'B') return KEY_DOWN;
            if (seq[1] == 'C') return KEY_RIGHT;
            if (seq[1] == 'D') return KEY_LEFT;
            if (seq[1] == 'H') return KEY_HOME;
            if (seq[1] == 'F') return KEY_END;
            if (seq[1] == '3') {
                char trail;
                read(STDIN_FILENO, &trail, 1);
                if (trail == '~') return KEY_DELETE;
            }
        }
        return KEY_ESCAPE;
    }

    if (c == KEY_CTRL_C) return KEY_CTRL_C;
    if (c == KEY_CTRL_D) return KEY_CTRL_D;
    if (c == KEY_BACKSPACE || c == 8) return KEY_BACKSPACE;
    if (c == KEY_ENTER || c == '\n') return KEY_ENTER;
    if (c == KEY_TAB) return KEY_TAB;
    return (unsigned char)c;
}

#endif // platform

// History (up arrow is civilization)

static char* history[HISTORY_MAX];
static int historyCount = 0;

static void historyAdd(const char* line) {
    if (line[0] == '\0') return;
    // skip dupes of the last entry — nobody wants that
    if (historyCount > 0 && strcmp(history[historyCount - 1], line) == 0) return;

    if (historyCount >= HISTORY_MAX) {
        free(history[0]);
        memmove(history, history + 1, sizeof(char*) * (HISTORY_MAX - 1));
        historyCount--;
    }
    history[historyCount++] = strdup(line);
}

static void historyFree(void) {
    for (int i = 0; i < historyCount; i++) free(history[i]);
    historyCount = 0;
}

// Multi-line Detection (are you done typing yet?)

static bool isComplete(const char* input) {
    if (input[0] == '\0') return true;

    int braces = 0, parens = 0, brackets = 0;
    bool inString = false;
    bool inTripleString = false;
    bool inLineComment = false;

    for (const char* p = input; *p; p++) {
        if (inLineComment) {
            if (*p == '\n') inLineComment = false;
            continue;
        }

        if (inTripleString) {
            if (*p == '"' && p[1] == '"' && p[2] == '"') {
                inTripleString = false;
                p += 2;
            }
            continue;
        }

        if (inString) {
            if (*p == '\\' && p[1]) { p++; continue; }
            if (*p == '"') inString = false;
            continue;
        }

        // Check for comments
        if (*p == '/' && p[1] == '/') { inLineComment = true; p++; continue; }

        // Check for triple-quote string
        if (*p == '"' && p[1] == '"' && p[2] == '"') {
            inTripleString = true;
            p += 2;
            continue;
        }

        // Check for string
        if (*p == '"') { inString = true; continue; }

        if (*p == '{') braces++;
        else if (*p == '}') braces--;
        else if (*p == '(') parens++;
        else if (*p == ')') parens--;
        else if (*p == '[') brackets++;
        else if (*p == ']') brackets--;
    }

    return braces <= 0 && parens <= 0 && brackets <= 0
           && !inString && !inTripleString;
}

static int braceDepth(const char* input) {
    int depth = 0;
    bool inString = false;
    for (const char* p = input; *p; p++) {
        if (inString) {
            if (*p == '\\' && p[1]) { p++; continue; }
            if (*p == '"') inString = false;
            continue;
        }
        if (*p == '"') { inString = true; continue; }
        if (*p == '{' || *p == '(' || *p == '[') depth++;
        else if (*p == '}' || *p == ')' || *p == ']') depth--;
    }
    return depth > 0 ? depth : 0;
}

// Syntax Coloring (making the REPL pretty)

static bool isTypeName(const char* start, int len) {
    // built-in types + anything that starts with uppercase
    if (len == 3 && memcmp(start, "int", 3) == 0) return true;
    if (len == 5 && memcmp(start, "float", 5) == 0) return true;
    if (len == 4 && memcmp(start, "bool", 4) == 0) return true;
    if (len == 6 && memcmp(start, "string", 6) == 0) return true;
    if (len == 4 && memcmp(start, "list", 4) == 0) return true;
    if (len == 3 && memcmp(start, "map", 3) == 0) return true;
    if (len == 6 && memcmp(start, "Result", 6) == 0) return true;
    if (len == 2 && memcmp(start, "fn", 2) == 0) return true;
    // capitalized = probably a class/type
    if (len > 0 && start[0] >= 'A' && start[0] <= 'Z') return true;
    return false;
}

static const char* tokenColor(NyxTokenType type, const char* start, int len,
                                NyxTokenType prevType) {
    switch (type) {
        // Keywords
        case TOKEN_AND: case TOKEN_OR: case TOKEN_NOT:
        case TOKEN_IS: case TOKEN_IN:
        case TOKEN_IF: case TOKEN_ELSE:
        case TOKEN_WHILE: case TOKEN_FOR: case TOKEN_LOOP:
        case TOKEN_FN: case TOKEN_RETURN:
        case TOKEN_LET: case TOKEN_VAR: case TOKEN_CONST:
        case TOKEN_CLASS: case TOKEN_SELF: case TOKEN_SUPER: case TOKEN_INIT:
        case TOKEN_PRINT:
        case TOKEN_MATCH:
        case TOKEN_YIELD: case TOKEN_RESUME:
        case TOKEN_IMPORT: case TOKEN_MODULE: case TOKEN_FROM:
        case TOKEN_ENUM:
        case TOKEN_INSTANCEOF:
        case TOKEN_BREAK: case TOKEN_CONTINUE:
            return COL_KEYWORD;

        // Booleans and nil
        case TOKEN_TRUE: case TOKEN_FALSE: case TOKEN_NIL:
            return COL_BOOL;

        // Strings
        case TOKEN_STRING: case TOKEN_INTERPOLATION:
            return COL_STRING;

        // Numbers
        case TOKEN_INTEGER: case TOKEN_FLOAT_LIT:
            return COL_NUMBER;

        // identifiers — context-dependent coloring
        case TOKEN_IDENTIFIER:
            // after colon = type annotation
            if (prevType == TOKEN_COLON) return COL_TYPE;
            // After -> = return type
            if (prevType == TOKEN_ARROW) return COL_TYPE;
            // Known type names
            if (isTypeName(start, len)) return COL_TYPE;
            // Regular identifier
            return COL_IDENT;

        // Punctuation and operators
        case TOKEN_LEFT_PAREN: case TOKEN_RIGHT_PAREN:
        case TOKEN_LEFT_BRACE: case TOKEN_RIGHT_BRACE:
        case TOKEN_LEFT_BRACKET: case TOKEN_RIGHT_BRACKET:
        case TOKEN_SEMICOLON: case TOKEN_COMMA:
            return COL_PUNCT;

        case TOKEN_EQUAL: case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG: case TOKEN_BANG_EQUAL:
        case TOKEN_GREATER: case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS: case TOKEN_LESS_EQUAL:
        case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_STAR:
        case TOKEN_SLASH: case TOKEN_PERCENT:
        case TOKEN_ARROW: case TOKEN_FAT_ARROW:
        case TOKEN_AMPERSAND_AMPERSAND: case TOKEN_PIPE_PIPE:
        case TOKEN_PLUS_EQUAL: case TOKEN_MINUS_EQUAL:
        case TOKEN_STAR_EQUAL: case TOKEN_SLASH_EQUAL:
        case TOKEN_PLUS_PLUS: case TOKEN_MINUS_MINUS:
        case TOKEN_DOT_DOT: case TOKEN_QUESTION:
            return COL_OP;

        // Errors
        case TOKEN_ERROR:
            return COL_ERROR;

        default:
            return COL_RESET;
    }
}

static void printColored(const char* line) {
    NyxScanner scanner;
    nyx_scanner_init(&scanner, line);

    const char* lineEnd = line + strlen(line);
    const char* prev = line;
    NyxTokenType prevType = TOKEN_EOF;

    for (;;) {
        NyxToken token = nyx_scanner_scan_token(&scanner);

        // error tokens point to static strings, not source — bail if out of range
        if (token.type == TOKEN_ERROR || token.start < line || token.start > lineEnd) {
            // Print whatever remains from prev to end of line
            if (prev < lineEnd) printf("%s", prev);
            break;
        }

        if (token.type == TOKEN_EOF) {
            // Print trailing content
            if (prev < lineEnd) printf("%s", prev);
            break;
        }

        // Print any gap between previous token and this one (whitespace, etc)
        if (token.start > prev) {
            // Check if the gap contains a comment
            bool isComment = false;
            for (const char* p = prev; p < token.start - 1; p++) {
                if (p[0] == '/' && p[1] == '/') { isComment = true; break; }
            }
            if (isComment) {
                printf("%s%.*s%s", COL_COMMENT, (int)(token.start - prev), prev, COL_RESET);
            } else {
                printf("%.*s", (int)(token.start - prev), prev);
            }
        }

        const char* col = tokenColor(token.type, token.start, token.length, prevType);
        if (col != COL_RESET) {
            printf("%s%.*s%s", col, token.length, token.start, COL_RESET);
        } else {
            printf("%.*s", token.length, token.start);
        }

        prev = token.start + token.length;
        prevType = token.type;
    }
}

// Line Editor (our janky but functional readline)

static void clearLine(const char* prompt) {
    printf("\r\033[2K%s", prompt);
}

static void refreshLine(const char* prompt, const char* buf, int len, int pos) {
    // clear line, redraw with syntax coloring, put cursor back
    printf("\r\033[2K%s", prompt);
    printColored(buf);

    // cursor back to where it should be
    int backtrack = len - pos;
    if (backtrack > 0) {
        printf("\033[%dD", backtrack);
    }
    fflush(stdout);
}

static int editLine(const char* prompt, char* buf, int bufMax) {
    int len = 0;
    int pos = 0; // cursor position within buf
    int histIdx = historyCount; // past the end = current input
    char savedLine[REPL_LINE_MAX] = "";

    buf[0] = '\0';
    printf("%s", prompt);
    fflush(stdout);

    for (;;) {
        int key = readKey();

        switch (key) {
            case KEY_ENTER:
                printf("\r\n");
                fflush(stdout);
                buf[len] = '\0';
                return len;

            case KEY_CTRL_C:
                buf[0] = '\0';
                printf("\r\n");
                fflush(stdout);
                return -1; // signal: clear

            case KEY_CTRL_D:
                if (len == 0) return -2; // signal: exit
                break; // ignore if buffer has content

            case KEY_BACKSPACE:
                if (pos > 0) {
                    memmove(buf + pos - 1, buf + pos, len - pos);
                    pos--;
                    len--;
                    buf[len] = '\0';
                    refreshLine(prompt, buf, len, pos);
                }
                break;

            case KEY_DELETE:
                if (pos < len) {
                    memmove(buf + pos, buf + pos + 1, len - pos - 1);
                    len--;
                    buf[len] = '\0';
                    refreshLine(prompt, buf, len, pos);
                }
                break;

            case KEY_LEFT:
                if (pos > 0) { pos--; printf("\033[D"); fflush(stdout); }
                break;

            case KEY_RIGHT:
                if (pos < len) { pos++; printf("\033[C"); fflush(stdout); }
                break;

            case KEY_HOME:
                if (pos > 0) { printf("\033[%dD", pos); pos = 0; fflush(stdout); }
                break;

            case KEY_END:
                if (pos < len) { printf("\033[%dC", len - pos); pos = len; fflush(stdout); }
                break;

            case KEY_UP:
                if (histIdx > 0) {
                    if (histIdx == historyCount) {
                        memcpy(savedLine, buf, len + 1);
                    }
                    histIdx--;
                    len = (int)strlen(history[histIdx]);
                    if (len >= bufMax) len = bufMax - 1;
                    memcpy(buf, history[histIdx], len);
                    buf[len] = '\0';
                    pos = len;
                    refreshLine(prompt, buf, len, pos);
                }
                break;

            case KEY_DOWN:
                if (histIdx < historyCount) {
                    histIdx++;
                    if (histIdx == historyCount) {
                        len = (int)strlen(savedLine);
                        memcpy(buf, savedLine, len + 1);
                    } else {
                        len = (int)strlen(history[histIdx]);
                        if (len >= bufMax) len = bufMax - 1;
                        memcpy(buf, history[histIdx], len);
                        buf[len] = '\0';
                    }
                    pos = len;
                    refreshLine(prompt, buf, len, pos);
                }
                break;

            case KEY_TAB:
                // Insert spaces
                if (len + TAB_WIDTH < bufMax) {
                    memmove(buf + pos + TAB_WIDTH, buf + pos, len - pos);
                    for (int i = 0; i < TAB_WIDTH; i++) buf[pos + i] = ' ';
                    len += TAB_WIDTH;
                    pos += TAB_WIDTH;
                    buf[len] = '\0';
                    refreshLine(prompt, buf, len, pos);
                }
                break;

            case -1:
                break; // ignore unknown keys

            default:
                // Printable character
                if (key >= 32 && key < KEY_UP && len + 1 < bufMax) {
                    memmove(buf + pos + 1, buf + pos, len - pos);
                    buf[pos] = (char)key;
                    len++;
                    pos++;
                    buf[len] = '\0';
                    refreshLine(prompt, buf, len, pos);
                }
                break;
        }
    }
}

// REPL Main Loop (read, eval, print, loop, repeat until death)

void nyx_repl_run(void) {
    printf("Nyx %d.%d.%d - Nemesis Technologies\n",
           NYX_VERSION_MAJOR, NYX_VERSION_MINOR, NYX_VERSION_PATCH);
    printf("Type 'exit' to quit.\n\n");

    enableRawMode();

    char buffer[REPL_BUFFER_MAX];
    char line[REPL_LINE_MAX];
    buffer[0] = '\0';
    int bufLen = 0;
    bool multiLine = false;

    for (;;) {
        // Build prompt
        char prompt[64];
        if (!multiLine) {
            snprintf(prompt, sizeof(prompt), "%snyx>%s ", COL_PROMPT, COL_RESET);
        } else {
            int depth = braceDepth(buffer);
            char indent[64] = "";
            int indentLen = 0;
            for (int i = 0; i < depth && indentLen + TAB_WIDTH < (int)sizeof(indent) - 1; i++) {
                for (int j = 0; j < TAB_WIDTH; j++) indent[indentLen++] = ' ';
            }
            indent[indentLen] = '\0';
            snprintf(prompt, sizeof(prompt), "%s ...%s %s", COL_CONT, COL_RESET, indent);
        }

        int result = editLine(prompt, line, sizeof(line));

        if (result == -2) {
            // Ctrl+D — exit
            if (bufLen > 0) {
                // If mid-input, just cancel
                buffer[0] = '\0';
                bufLen = 0;
                multiLine = false;
                printf("\n");
                continue;
            }
            printf("\n");
            break;
        }

        if (result == -1) {
            // Ctrl+C — clear current input
            buffer[0] = '\0';
            bufLen = 0;
            multiLine = false;
            continue;
        }

        // Check for exit command
        if (!multiLine && strcmp(line, "exit") == 0) break;

        // Append line to buffer
        if (bufLen > 0 && bufLen + 1 < REPL_BUFFER_MAX) {
            buffer[bufLen++] = '\n';
        }
        int lineLen = (int)strlen(line);
        if (bufLen + lineLen < REPL_BUFFER_MAX) {
            memcpy(buffer + bufLen, line, lineLen);
            bufLen += lineLen;
        }
        buffer[bufLen] = '\0';

        // Check if input is complete
        if (!isComplete(buffer)) {
            multiLine = true;
            continue;
        }

        // Skip empty input
        if (bufLen == 0) continue;

        // Add to history
        historyAdd(buffer);

        // Execute
        disableRawMode();
        nyx_vm_interpret(buffer);
        fflush(stdout);
        fflush(stderr);
        enableRawMode();
#ifdef _WIN32
        // flush stale keypresses that piled up while code was running
        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#endif

        // Reset buffer
        buffer[0] = '\0';
        bufLen = 0;
        multiLine = false;
    }

    disableRawMode();
    historyFree();
}
