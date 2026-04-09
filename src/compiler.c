#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "types.h"
#include "vm.h"

#ifdef NYX_DEBUG_TRACE
#include "debug.h"
#endif

// Parser State

typedef struct {
    NyxToken current;
    NyxToken previous;
    NyxScanner scanner;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_TERNARY,     // ? :
    PREC_OR,          // or ||
    PREC_AND,         // and &&
    PREC_EQUALITY,    // == != is  is not
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * / %
    PREC_UNARY,       // ! not -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

// Compiler State (one per function, they nest like matryoshka dolls)

typedef struct {
    NyxToken name;
    int depth;
    bool isCaptured;
    NyxType type;           // declared or inferred type
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT,
} FunctionType;

typedef struct ClassCompiler {
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
} ClassCompiler;

typedef struct Compiler {
    struct Compiler* enclosing;
    NyxObjFunction* function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;

    NyxType lastExprType;   // type of the most recently compiled expression
    NyxType returnType;     // declared return type of this function

    // loop bookkeeping for break/continue
    int loopStart;          // bytecode offset of loop condition
    int loopScopeDepth;     // scope depth when the loop began
    int breakJumps[64];     // pending break forward-jumps
    int breakCount;
    int continueJumps[64];  // pending continue forward-jumps (for-loops only)
    int continueCount;
    bool forLoop;           // true if this is a for-loop (continue needs forward jump)
} Compiler;

static Parser parser;
static Compiler* current = NULL;
static ClassCompiler* currentClass = NULL;

static NyxChunk* currentChunk(void) {
    return &current->function->chunk;
}

// Error Reporting (we try to be helpful, but no promises)

static void errorAt(NyxToken* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type != TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

// Token Consumption

static void advance(void) {
    parser.previous = parser.current;

    for (;;) {
        parser.current = nyx_scanner_scan_token(&parser.scanner);
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

static void consume(NyxTokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

static bool check(NyxTokenType type) {
    return parser.current.type == type;
}

static bool match(NyxTokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// Bytecode Emission (turn ideas into instructions)

static void emitByte(uint8_t byte) {
    nyx_chunk_write(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn(void) {
    if (current->type == TYPE_INITIALIZER) {
        emitBytes(OP_GET_LOCAL, 0); // return 'self'
    } else {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static int makeConstant(NyxValue value) {
    // GC protection: park it on the stack while the constant array grows
    nyx_vm_push(value);
    int constant = nyx_chunk_add_constant(currentChunk(), value);
    nyx_vm_pop();
    if (constant > UINT16_MAX) {
        error("Too many constants in one chunk (max 65535).");
        return 0;
    }
    return constant;
}

// emit a constant — uses short form for <256 constants, long form after that
static void emitConstantIndex(int index) {
    if (index <= UINT8_MAX) {
        emitBytes(OP_CONSTANT, (uint8_t)index);
    } else {
        emitByte(OP_CONSTANT_LONG);
        emitByte((uint8_t)((index >> 8) & 0xff));
        emitByte((uint8_t)(index & 0xff));
    }
}

static void emitConstant(NyxValue value) {
    // small ints get a special opcode — one byte instead of a constant pool entry
    if (IS_INT(value)) {
        int64_t iv = AS_INT(value);
        if (iv >= -128 && iv <= 127) {
            emitBytes(OP_LOADI, (uint8_t)(int8_t)iv);
            return;
        }
    }
    emitConstantIndex(makeConstant(value));
}

// like makeConstant but yells if you exceed 256 — some opcodes only have 1 byte to spare
static uint8_t makeConstant8(NyxValue value) {
    int constant = makeConstant(value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk for this opcode (max 256 names).");
        return 0;
    }
    return (uint8_t)constant;
}

// Peephole Optimizer (making the bytecode less embarrassing) 
// constant folding: if both operands are constants, just do the math now

static bool tryFoldBinary(uint8_t op) {
    NyxChunk* chunk = currentChunk();
    int count = chunk->count;

    // need at least 5 bytes of bytecode to fold anything
    if (count < 5) return false;

    // sanity check: is the op actually there?
    if (chunk->code[count - 1] != op) return false;

    // figure out where operand b lives in the bytecode
    int bIdx, bSize;
    if (count >= 3 && chunk->code[count - 3] == OP_CONSTANT) {
        bIdx = chunk->code[count - 2];
        bSize = 2; // opcode + 1 byte index
    } else if (count >= 4 && chunk->code[count - 4] == OP_CONSTANT_LONG) {
        bIdx = (chunk->code[count - 3] << 8) | chunk->code[count - 2];
        bSize = 3; // opcode + 2 byte index
    } else {
        return false;
    }

    // now find operand a (right before b)
    int aStart = count - 1 - bSize;
    int aIdx, aSize;
    if (aStart >= 2 && chunk->code[aStart - 2] == OP_CONSTANT) {
        aIdx = chunk->code[aStart - 1];
        aSize = 2;
    } else if (aStart >= 3 && chunk->code[aStart - 3] == OP_CONSTANT_LONG) {
        aIdx = (chunk->code[aStart - 2] << 8) | chunk->code[aStart - 1];
        aSize = 3;
    } else {
        return false;
    }

    int totalSize = aSize + bSize + 1; // +1 for the binary op itself

    NyxValue a = chunk->constants.values[aIdx];
    NyxValue b = chunk->constants.values[bIdx];

    // only fold numbers — string folding is a different circle of hell
    if (!IS_INT(a) && !IS_FLOAT(a)) return false;
    if (!IS_INT(b) && !IS_FLOAT(b)) return false;

    NyxValue result;
    bool bothInt = IS_INT(a) && IS_INT(b);

    switch (op) {
        case OP_ADD:
            result = bothInt ? INT_VAL(AS_INT(a) + AS_INT(b))
                             : FLOAT_VAL((IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a)) +
                                         (IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b)));
            break;
        case OP_SUBTRACT:
            result = bothInt ? INT_VAL(AS_INT(a) - AS_INT(b))
                             : FLOAT_VAL((IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a)) -
                                         (IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b)));
            break;
        case OP_MULTIPLY:
            result = bothInt ? INT_VAL(AS_INT(a) * AS_INT(b))
                             : FLOAT_VAL((IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a)) *
                                         (IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b)));
            break;
        case OP_DIVIDE: {
            double bv = IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b);
            if (bv == 0) return false; // don't fold division by zero
            if (bothInt && AS_INT(b) != 0 && AS_INT(a) % AS_INT(b) == 0)
                result = INT_VAL(AS_INT(a) / AS_INT(b));
            else
                result = FLOAT_VAL((IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a)) / bv);
            break;
        }
        case OP_MODULO:
            if (bothInt && AS_INT(b) != 0)
                result = INT_VAL(AS_INT(a) % AS_INT(b));
            else return false;
            break;
        default:
            return false;
    }

    // rewind the bytecode and replace with a single constant. chef's kiss
    chunk->count -= totalSize;
    emitConstant(result);
    return true;
}

// try to fuse LOADI + ADD/SUB into a single ADDI/SUBI — fewer instructions, faster loops
static bool tryImmediateOp(uint8_t op) {
    NyxChunk* chunk = currentChunk();
    int count = chunk->count;

    // need at least 3 bytes to work with
    if (count < 3) return false;
    if (chunk->code[count - 1] != op) return false;
    if (chunk->code[count - 3] != OP_LOADI) return false;

    uint8_t imm = chunk->code[count - 2];

    if (op == OP_ADD) {
        chunk->count -= 3;
        emitBytes(OP_ADDI, imm);
        return true;
    }
    if (op == OP_SUBTRACT) {
        chunk->count -= 3;
        emitBytes(OP_SUBI, imm);
        return true;
    }
    return false;
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// Compiler Lifecycle (birth, suffering, death)

static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
    compiler->returnType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
    compiler->loopStart = -1;
    compiler->loopScopeDepth = 0;
    compiler->breakCount = 0;
    compiler->continueCount = 0;
    compiler->forLoop = false;
    compiler->function = nyx_new_function();
    current = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = nyx_copy_string(
            parser.previous.start, parser.previous.length);
    }

    // slot 0 is reserved: 'self' in methods, the closure itself in functions
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION && type != TYPE_SCRIPT) {
        local->name.start = "self";
        local->name.length = 4;
    } else {
        local->name.start = "";
        local->name.length = 0;
    }
}

static NyxObjFunction* endCompiler(void) {
    emitReturn();
    NyxObjFunction* function = current->function;

#ifdef NYX_DEBUG_TRACE
    if (!parser.hadError) {
        nyx_disassemble_chunk(currentChunk(),
            function->name != NULL ? function->name->chars : "<script>");
    }
#endif

    current = current->enclosing;
    return function;
}

// Scoping (who can see what)

static void beginScope(void) {
    current->scopeDepth++;
}

static void endScope(void) {
    current->scopeDepth--;
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

static void addLocal(NyxToken name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
    local->type = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static bool identifiersEqual(NyxToken* a, NyxToken* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, NyxToken* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Cannot read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    // already captured? reuse it
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, NyxToken* name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

// Forward Declarations (C's favorite game)

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(NyxTokenType type);
static void parsePrecedence(Precedence precedence);
static uint8_t argumentList(void);
static void namedVariable(NyxToken name, bool canAssign);
static void matchExpr(bool canAssign);
static uint8_t parseVariable(const char* errorMessage);
static void defineVariable(uint8_t global);
static void declareVariable(void);

// loop context for break/continue — this is more complex than it has any right to be
typedef struct {
    int prevLoopStart;
    int prevLoopScopeDepth;
    int prevBreakJumps[64];
    int prevBreakCount;
    int prevContinueJumps[64];
    int prevContinueCount;
    bool prevForLoop;
} LoopContext;

static void beginLoop(LoopContext* ctx, int loopStart);
static void patchContinueJumps(void);
static void endLoop(LoopContext* ctx);

// Type Annotations (parsed but mostly decorative for now)
static NyxType parseTypeAnnotation(void) {
    advance(); // consume type name token
    NyxType type = nyx_type_from_name(parser.previous.start, parser.previous.length);

    // generics: list<int>, map<string, int>
    if (match(TOKEN_LESS)) {
        int depth = 1;
        while (depth > 0 && !check(TOKEN_EOF)) {
            if (check(TOKEN_LESS)) depth++;
            if (check(TOKEN_GREATER)) depth--;
            if (depth > 0) advance();
        }
        consume(TOKEN_GREATER, "Expected '>' after generic type.");
    }
    // nullable: int?
    if (match(TOKEN_QUESTION)) {
        type.nullable = true;
    }
    return type;
}

// legacy wrapper — keeping the old name around because why not
static void skipTypeAnnotation(void) {
    parseTypeAnnotation();
}

// Expression Parsers (Pratt parsing, the one good idea)

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
}

static void number_int(bool canAssign) {
    (void)canAssign;
    int64_t value = strtoll(parser.previous.start, NULL, 0); // base 0 auto-detects hex (0x), octal (0), decimal
    emitConstant(INT_VAL(value));
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_INT);
}

static void number_float(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    emitConstant(FLOAT_VAL(value));
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_FLOAT);
}

static void unary(bool canAssign) {
    (void)canAssign;
    NyxTokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);

    switch (operatorType) {
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        case TOKEN_BANG:
        case TOKEN_NOT:   emitByte(OP_NOT); break;
        default: return;
    }
}

static void binary(bool canAssign) {
    (void)canAssign;
    NyxTokenType operatorType = parser.previous.type;
    NyxType leftType = current->lastExprType;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_PLUS:
            emitByte(OP_ADD);
            // ADDI only works for numbers — can't use it if string concat might happen
            if (!tryFoldBinary(OP_ADD)) {
                if (leftType.tag == NYX_TYPE_INT || leftType.tag == NYX_TYPE_FLOAT)
                    tryImmediateOp(OP_ADD);
            }
            if (leftType.tag == NYX_TYPE_STRING || current->lastExprType.tag == NYX_TYPE_STRING) {
                current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_STRING);
            } else if (leftType.tag == NYX_TYPE_FLOAT || current->lastExprType.tag == NYX_TYPE_FLOAT) {
                current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_FLOAT);
            } else {
                current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_INT);
            }
            break;
        case TOKEN_MINUS:
            emitByte(OP_SUBTRACT);
            if (!tryFoldBinary(OP_SUBTRACT)) tryImmediateOp(OP_SUBTRACT);
            goto numeric_result;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); tryFoldBinary(OP_MULTIPLY); goto numeric_result;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE);   tryFoldBinary(OP_DIVIDE);   goto numeric_result;
        case TOKEN_PERCENT:       emitByte(OP_MODULO);   tryFoldBinary(OP_MODULO);   goto numeric_result;
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_IS:            emitByte(OP_EQUAL); goto bool_result;
        case TOKEN_BANG_EQUAL:    emitByte(OP_NOT_EQUAL); goto bool_result;
        case TOKEN_GREATER:       emitByte(OP_GREATER); goto bool_result;
        case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); goto bool_result;
        case TOKEN_LESS:          emitByte(OP_LESS); goto bool_result;
        case TOKEN_LESS_EQUAL:    emitByte(OP_LESS_EQUAL); goto bool_result;
        default: return;
    }
    return;

numeric_result:
    if (leftType.tag == NYX_TYPE_FLOAT || current->lastExprType.tag == NYX_TYPE_FLOAT) {
        current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_FLOAT);
    } else {
        current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_INT);
    }
    return;

bool_result:
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_BOOL);
    return;
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE:
            emitByte(OP_FALSE);
            current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_BOOL);
            break;
        case TOKEN_NIL:
            emitByte(OP_NIL);
            current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_NIL);
            break;
        case TOKEN_TRUE:
            emitByte(OP_TRUE);
            current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_BOOL);
            break;
        default: return;
    }
}

static void processStringSegment(const char* src, int len) {
    char* buffer = ALLOCATE(char, len + 1);
    int bufLen = 0;
    for (int i = 0; i < len; i++) {
        if (src[i] == '\\' && i + 1 < len) {
            i++;
            switch (src[i]) {
                case 'n':  buffer[bufLen++] = '\n'; break;
                case 't':  buffer[bufLen++] = '\t'; break;
                case 'r':  buffer[bufLen++] = '\r'; break;
                case '\\': buffer[bufLen++] = '\\'; break;
                case '"':  buffer[bufLen++] = '"';  break;
                case '0':  buffer[bufLen++] = '\0'; break;
                default:
                    buffer[bufLen++] = '\\';
                    buffer[bufLen++] = src[i];
                    break;
            }
        } else {
            buffer[bufLen++] = src[i];
        }
    }
    buffer[bufLen] = '\0';
    emitConstant(OBJ_VAL(nyx_take_string(buffer, bufLen)));
}

static void string(bool canAssign) {
    (void)canAssign;
    const char* start;
    int length;

    // triple-quote strings — because sometimes you need to rant
    if (parser.previous.length >= 6 &&
        parser.previous.start[0] == '"' &&
        parser.previous.start[1] == '"' &&
        parser.previous.start[2] == '"') {
        start = parser.previous.start + 3;
        length = parser.previous.length - 6;
    } else {
        start = parser.previous.start + 1;
        length = parser.previous.length - 2;
    }

    // string interpolation: "hello ${name}"
    bool hasInterp = false;
    for (int i = 0; i < length - 1; i++) {
        if (start[i] == '$' && start[i + 1] == '{') { hasInterp = true; break; }
    }

    if (!hasInterp) {
        processStringSegment(start, length);
        current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_STRING);
        return;
    }

    // compiles "abc ${var} def" into "abc " + str(var) + " def"
    int segments = 0;
    const char* p = start;
    const char* end = start + length;

    while (p < end) {
        // hunt for the next ${
        const char* interp = p;
        while (interp < end - 1 && !(interp[0] == '$' && interp[1] == '{'))
            interp++;

        if (interp >= end - 1) {
            // no more interpolations — just emit the rest
            if (interp <= end && (int)(end - p) > 0) {
                processStringSegment(p, (int)(end - p));
                if (segments > 0) emitByte(OP_ADD);
                segments++;
            }
            break;
        }

        // emit the text chunk before the interpolation
        if (interp > p) {
            processStringSegment(p, (int)(interp - p));
            if (segments > 0) emitByte(OP_ADD);
            segments++;
        }

        // find the matching }
        const char* exprStart = interp + 2;
        const char* exprEnd = exprStart;
        int depth = 1;
        while (exprEnd < end && depth > 0) {
            if (*exprEnd == '{') depth++;
            else if (*exprEnd == '}') depth--;
            if (depth > 0) exprEnd++;
        }

        // compile the expression inside ${} — this is where it gets wild
        int nameLen = (int)(exprEnd - exprStart);
        if (nameLen > 0) {
            if (segments == 0) {
                // first segment? emit "" so the ADD knows to do string concat
                processStringSegment("", 0);
                segments++;
            }

            // save the parser — we're about to do crimes with a sub-scanner
            NyxScanner savedScanner = parser.scanner;
            NyxToken savedCurrent = parser.current;
            NyxToken savedPrevious = parser.previous;
            int savedLine = parser.previous.line;

            // null-terminate the expression chunk
            char* exprBuf = ALLOCATE(char, nameLen + 2);
            memcpy(exprBuf, exprStart, nameLen);
            exprBuf[nameLen] = ';';  // sentinel so parser stops cleanly
            exprBuf[nameLen + 1] = '\0';

            // spin up a sub-scanner for the expression
            nyx_scanner_init(&parser.scanner, exprBuf);
            parser.scanner.line = savedLine;
            advance(); // prime the parser with first token

            // compile it
            expression();

            // restore the parser — crimes complete
            FREE_ARRAY(char, exprBuf, nameLen + 2);
            parser.scanner = savedScanner;
            parser.current = savedCurrent;
            parser.previous = savedPrevious;

            emitByte(OP_ADD); // string + value triggers auto-convert
            segments++;
        }

        p = exprEnd + 1; // skip past }
    }

    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_STRING);
}

static uint8_t identifierConstant(NyxToken* name) {
    return makeConstant8(OBJ_VAL(nyx_copy_string(name->start, name->length)));
}

static void namedVariable(NyxToken name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    NyxType varType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);

    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
        varType = current->locals[arg].type;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        if (varType.tag != NYX_TYPE_UNKNOWN && current->lastExprType.tag != NYX_TYPE_UNKNOWN) {
            if (!nyx_type_compatible(varType, current->lastExprType)) {
                error("Type mismatch: cannot assign '%s' to variable of type '%s'.");
            }
        }
        emitBytes(setOp, (uint8_t)arg);
    } else if (canAssign && (check(TOKEN_PLUS_EQUAL) || check(TOKEN_MINUS_EQUAL) ||
               check(TOKEN_STAR_EQUAL) || check(TOKEN_SLASH_EQUAL) ||
               check(TOKEN_PERCENT_EQUAL))) {
        // compound assignment: x += expr → x = x + expr (sugar)
        NyxTokenType op = parser.current.type;
        advance();
        emitBytes(getOp, (uint8_t)arg); // get current value
        expression();                    // get RHS
        switch (op) {
            case TOKEN_PLUS_EQUAL:    emitByte(OP_ADD); break;
            case TOKEN_MINUS_EQUAL:   emitByte(OP_SUBTRACT); break;
            case TOKEN_STAR_EQUAL:    emitByte(OP_MULTIPLY); break;
            case TOKEN_SLASH_EQUAL:   emitByte(OP_DIVIDE); break;
            case TOKEN_PERCENT_EQUAL: emitByte(OP_MODULO); break;
            default: break;
        }
        emitBytes(setOp, (uint8_t)arg);
    } else if (canAssign && match(TOKEN_PLUS_PLUS)) {
        // x++ — the classics never die
        emitBytes(getOp, (uint8_t)arg);
        emitConstant(INT_VAL(1));
        emitByte(OP_ADD);
        emitBytes(setOp, (uint8_t)arg);
    } else if (canAssign && match(TOKEN_MINUS_MINUS)) {
        // x--
        emitBytes(getOp, (uint8_t)arg);
        emitConstant(INT_VAL(1));
        emitByte(OP_SUBTRACT);
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
        current->lastExprType = varType;
    }
}

static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

// yield — pause the coroutine and hand back a value
static void yield_(bool canAssign) {
    (void)canAssign;
    current->function->isGenerator = true;
    expression();
    emitByte(OP_YIELD);
}

// resume — poke the coroutine and see what it gives us
static void resume_(bool canAssign) {
    (void)canAssign;
    parsePrecedence(PREC_UNARY);
    emitByte(OP_RESUME);
}

// |x, y| => expr — lambdas, because fn is too many characters apparently
static void lambda(bool canAssign) {
    (void)canAssign;

    // spin up an anonymous function
    Compiler compiler;
    initCompiler(&compiler, TYPE_FUNCTION);
    beginScope();

    // params between the pipes
    if (!check(TOKEN_PIPE)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Cannot have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expected parameter name.");
            if (match(TOKEN_COLON)) {
                NyxType paramType = parseTypeAnnotation();
                if (current->localCount > 0)
                    current->locals[current->localCount - 1].type = paramType;
            }
            defineVariable(constant);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_PIPE, "Expected '|' after lambda parameters.");

    // optional return type annotation
    if (match(TOKEN_ARROW)) {
        current->returnType = parseTypeAnnotation();
    }

    consume(TOKEN_FAT_ARROW, "Expected '=>' after lambda parameters.");

    // lambda body: one expression, auto-returned. no braces needed
    expression();
    emitByte(OP_RETURN);

    NyxObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant8(OBJ_VAL(fn)));
    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void self_(bool canAssign) {
    (void)canAssign;
    if (currentClass == NULL) {
        error("Cannot use 'self' outside of a class.");
        return;
    }
    variable(false);
}

static void super_(bool canAssign) {
    (void)canAssign;
    if (currentClass == NULL) {
        error("Cannot use 'super' outside of a class.");
        return;
    } else if (!currentClass->hasSuperclass) {
        error("Cannot use 'super' in a class with no superclass.");
        return;
    }

    consume(TOKEN_DOT, "Expected '.' after 'super'.");
    // accept both identifiers and 'init' — init is technically a keyword but also a method name
    if (match(TOKEN_INIT)) {
        // 'init' gets a pass here
    } else {
        consume(TOKEN_IDENTIFIER, "Expected superclass method name.");
    }
    uint8_t name = identifierConstant(&parser.previous);

    namedVariable((NyxToken){TOKEN_IDENTIFIER, "self", 4, parser.previous.line}, false);

    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable((NyxToken){TOKEN_IDENTIFIER, "super", 5, parser.previous.line}, false);
        emitBytes(OP_SUPER_INVOKE, name);
        emitByte(argCount);
    } else {
        namedVariable((NyxToken){TOKEN_IDENTIFIER, "super", 5, parser.previous.line}, false);
        emitBytes(OP_GET_SUPER, name);
    }
}

// ?. — null-safe access. if it's nil, we just shrug and move on
static void nullSafeDot(bool canAssign) {
    (void)canAssign;
    // if nil, skip the property access entirely
    int skipJump = emitJump(OP_JUMP_IF_NIL);
    // not nil — actually do the thing
    consume(TOKEN_IDENTIFIER, "Expected property name after '?.'.");
    uint8_t name = identifierConstant(&parser.previous);

    if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }

    patchJump(skipJump);
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
}

static void dot(bool canAssign) {
    consume(TOKEN_IDENTIFIER, "Expected property name after '.'.");
    uint8_t name = identifierConstant(&parser.previous);

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitBytes(OP_SET_PROPERTY, name);
    } else if (match(TOKEN_LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(OP_INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes(OP_GET_PROPERTY, name);
    }
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
}

// [1, 2, 3] — list literal
static void listLiteral(bool canAssign) {
    (void)canAssign;
    int itemCount = 0;
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            if (check(TOKEN_RIGHT_BRACKET)) break; // trailing comma
            expression();
            itemCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expected ']' after list items.");
    emitBytes(OP_BUILD_LIST, (uint8_t)itemCount);
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_LIST);
}

// {"a": 1, "b": 2} — map literal
static void mapLiteral(bool canAssign) {
    (void)canAssign;
    int entryCount = 0;
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            if (check(TOKEN_RIGHT_BRACE)) break; // trailing comma
            expression(); // key
            consume(TOKEN_COLON, "Expected ':' after map key.");
            expression(); // value
            entryCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACE, "Expected '}' after map entries.");
    emitBytes(OP_BUILD_MAP, (uint8_t)entryCount);
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_MAP);
}

static void subscript(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expected ']' after index.");

    if (canAssign && match(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_INDEX_SET);
    } else {
        emitByte(OP_INDEX_GET);
    }
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
}

static uint8_t argumentList(void) {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Cannot have more than 255 arguments.");
            }
            argCount++;
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
    return argCount;
}

static void call_(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN); // can't know return type statically
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

// ? — ternary OR try-unwrap. context decides. good luck
static void questionMark(bool canAssign) {
    (void)canAssign;

    // if followed by ; or ) or , — it's postfix try-unwrap (result?)
    if (check(TOKEN_SEMICOLON) || check(TOKEN_RIGHT_PAREN) ||
        check(TOKEN_COMMA) || check(TOKEN_EOF)) {
        if (current->type == TYPE_SCRIPT) {
            error("Cannot use '?' outside of a function.");
            return;
        }
        emitByte(OP_TRY_UNWRAP);
        return;
    }

    // otherwise it's a ternary
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    // 'then' branch
    parsePrecedence(PREC_TERNARY);
    int elseJump = emitJump(OP_JUMP);

    consume(TOKEN_COLON, "Expected ':' in ternary expression.");
    patchJump(thenJump);
    emitByte(OP_POP);

    // 'else' branch
    parsePrecedence(PREC_TERNARY);
    patchJump(elseJump);
}

// 0..10 — range expression
static void rangeLiteral(bool canAssign) {
    (void)canAssign;
    // LHS is already on stack, parse the RHS
    parsePrecedence(PREC_COMPARISON + 1);
    emitByte(OP_BUILD_RANGE);
}

static void in_not_operator(bool canAssign) {
    (void)canAssign;
    consume(TOKEN_IN, "Expected 'in' after 'not' in expression.");
    parsePrecedence(PREC_COMPARISON + 1);
    emitByte(OP_CONTAINS);
    emitByte(OP_NOT);
}

static void in_operator(bool canAssign) {
    (void)canAssign;
    parsePrecedence(PREC_COMPARISON + 1);
    emitByte(OP_CONTAINS);
}

static void is_operator(bool canAssign) {
    (void)canAssign;
    if (match(TOKEN_NOT)) {
        parsePrecedence(PREC_EQUALITY + 1);
        emitByte(OP_NOT_EQUAL);
    } else {
        parsePrecedence(PREC_EQUALITY + 1);
        emitByte(OP_EQUAL);
    }
}

// Parse Rules Table (the Pratt parser's brain)

static ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]          = {grouping,     call_,  PREC_CALL},
    [TOKEN_RIGHT_PAREN]         = {NULL,         NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]          = {mapLiteral,   NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]         = {NULL,         NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACKET]        = {listLiteral,  subscript, PREC_CALL},
    [TOKEN_RIGHT_BRACKET]       = {NULL,         NULL,   PREC_NONE},
    [TOKEN_COMMA]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_DOT]                 = {NULL,         dot,    PREC_CALL},
    [TOKEN_QUESTION_DOT]        = {NULL,         nullSafeDot, PREC_CALL},
    [TOKEN_SEMICOLON]           = {NULL,         NULL,   PREC_NONE},
    [TOKEN_COLON]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_PLUS]                = {NULL,         binary, PREC_TERM},
    [TOKEN_MINUS]               = {unary,        binary, PREC_TERM},
    [TOKEN_STAR]                = {NULL,         binary, PREC_FACTOR},
    [TOKEN_SLASH]               = {NULL,         binary, PREC_FACTOR},
    [TOKEN_PERCENT]             = {NULL,         binary, PREC_FACTOR},
    [TOKEN_BANG]                = {unary,        NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]          = {NULL,         binary, PREC_EQUALITY},
    [TOKEN_EQUAL]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]         = {NULL,         binary, PREC_EQUALITY},
    [TOKEN_GREATER]             = {NULL,         binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]       = {NULL,         binary, PREC_COMPARISON},
    [TOKEN_LESS]                = {NULL,         binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]          = {NULL,         binary, PREC_COMPARISON},
    [TOKEN_AMPERSAND_AMPERSAND] = {NULL,         and_,   PREC_AND},
    [TOKEN_PIPE_PIPE]           = {NULL,         or_,    PREC_OR},
    [TOKEN_PIPE]                = {lambda,       NULL,   PREC_NONE},
    [TOKEN_ARROW]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_FAT_ARROW]           = {NULL,         NULL,   PREC_NONE},
    [TOKEN_DOT_DOT]             = {NULL,         rangeLiteral, PREC_TERM},
    [TOKEN_QUESTION]            = {NULL,         questionMark, PREC_TERNARY},
    [TOKEN_IDENTIFIER]          = {variable,     NULL,   PREC_NONE},
    [TOKEN_STRING]              = {string,       NULL,   PREC_NONE},
    [TOKEN_INTEGER]             = {number_int,   NULL,   PREC_NONE},
    [TOKEN_FLOAT_LIT]           = {number_float, NULL,   PREC_NONE},
    [TOKEN_AND]                 = {NULL,         and_,   PREC_AND},
    [TOKEN_OR]                  = {NULL,         or_,    PREC_OR},
    [TOKEN_NOT]                 = {unary,        in_not_operator, PREC_COMPARISON},
    [TOKEN_IS]                  = {NULL,         is_operator, PREC_EQUALITY},
    [TOKEN_IN]                  = {NULL,         in_operator, PREC_COMPARISON},
    [TOKEN_IF]                  = {NULL,         NULL,   PREC_NONE},
    [TOKEN_ELSE]                = {NULL,         NULL,   PREC_NONE},
    [TOKEN_WHILE]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_FOR]                 = {NULL,         NULL,   PREC_NONE},
    [TOKEN_LOOP]                = {NULL,         NULL,   PREC_NONE},
    [TOKEN_FN]                  = {NULL,         NULL,   PREC_NONE},
    [TOKEN_RETURN]              = {NULL,         NULL,   PREC_NONE},
    [TOKEN_LET]                 = {NULL,         NULL,   PREC_NONE},
    [TOKEN_VAR]                 = {NULL,         NULL,   PREC_NONE},
    [TOKEN_CONST]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_CLASS]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_SELF]                = {self_,        NULL,   PREC_NONE},
    [TOKEN_SUPER]               = {super_,       NULL,   PREC_NONE},
    [TOKEN_INIT]                = {NULL,         NULL,   PREC_NONE},
    [TOKEN_TRUE]                = {literal,      NULL,   PREC_NONE},
    [TOKEN_FALSE]               = {literal,      NULL,   PREC_NONE},
    [TOKEN_NIL]                 = {literal,      NULL,   PREC_NONE},
    [TOKEN_PRINT]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_MATCH]               = {matchExpr,    NULL,   PREC_NONE},
    [TOKEN_YIELD]               = {yield_,       NULL,   PREC_NONE},
    [TOKEN_RESUME]              = {resume_,      NULL,   PREC_NONE},
    [TOKEN_IMPORT]              = {NULL,         NULL,   PREC_NONE},
    [TOKEN_MODULE]              = {NULL,         NULL,   PREC_NONE},
    [TOKEN_FROM]                = {NULL,         NULL,   PREC_NONE},
    [TOKEN_BREAK]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_CONTINUE]            = {NULL,         NULL,   PREC_NONE},
    [TOKEN_ERROR]               = {NULL,         NULL,   PREC_NONE},
    [TOKEN_EOF]                 = {NULL,         NULL,   PREC_NONE},
};

static ParseRule* getRule(NyxTokenType type) {
    return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expected expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static void expression(void) {
    parsePrecedence(PREC_ASSIGNMENT);
}

// Statements (where code actually does things)

static void synchronize(void) {
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FN:
            case TOKEN_VAR:
            case TOKEN_LET:
            case TOKEN_CONST:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
            case TOKEN_MATCH:
                return;
            default:;
        }
        advance();
    }
}

static void printStatement(void) {
    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'print'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after value.");
    consume(TOKEN_SEMICOLON, "Expected ';' after print statement.");
    emitByte(OP_PRINT);
}

static void expressionStatement(void) {
    expression();
    consume(TOKEN_SEMICOLON, "Expected ';' after expression.");
    emitByte(OP_POP);
}

static void block(void) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expected '}' after block.");
}

// parse either { block } or a single statement — we're flexible like that
static void bodyStatement(void) {
    if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        statement();
    }
}

static void ifStatement(void) {
    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    bodyStatement();

    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) {
        if (match(TOKEN_IF)) {
            ifStatement();
        } else {
            bodyStatement();
        }
    }
    patchJump(elseJump);
}

static void whileStatement(void) {
    LoopContext ctx;
    int loopStart = currentChunk()->count;
    beginLoop(&ctx, loopStart);

    consume(TOKEN_LEFT_PAREN, "Expected '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);

    bodyStatement();

    emitLoop(loopStart);
    patchJump(exitJump);
    emitByte(OP_POP);
    endLoop(&ctx);
}

static void forStatement(void) {
    LoopContext ctx;
    beginScope();

    consume(TOKEN_IDENTIFIER, "Expected variable name after 'for'.");
    NyxToken varName = parser.previous;

    // "for key, value in ..." — two-variable iteration
    NyxToken valueName;
    bool twoVars = false;
    if (match(TOKEN_COMMA)) {
        consume(TOKEN_IDENTIFIER, "Expected second variable name.");
        valueName = parser.previous;
        twoVars = true;
    }

    consume(TOKEN_IN, "Expected 'in' after for variable.");

    // parse what we're iterating over
    expression();

    if (match(TOKEN_DOT_DOT)) {
        // Range-based for: for i in start..end { }
        addLocal(varName);
        markInitialized();

        expression(); // end value
        NyxToken endName = {TOKEN_IDENTIFIER, " for_end", 8, varName.line};
        addLocal(endName);
        markInitialized();

        int loopStart = currentChunk()->count;
        beginLoop(&ctx, loopStart);
        current->forLoop = true;

        int iterSlot = current->localCount - 2;
        int endSlot = current->localCount - 1;
        emitBytes(OP_GET_LOCAL, (uint8_t)iterSlot);
        emitBytes(OP_GET_LOCAL, (uint8_t)endSlot);
        emitByte(OP_LESS);

        int exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);

        bodyStatement();

        // patch continue jumps to the increment
        patchContinueJumps();

        emitBytes(OP_GET_LOCAL, (uint8_t)iterSlot);
        emitConstant(INT_VAL(1));
        emitByte(OP_ADD);
        emitBytes(OP_SET_LOCAL, (uint8_t)iterSlot);
        emitByte(OP_POP);

        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(OP_POP);
        endLoop(&ctx);
    } else {
        // Collection-based for: for item in collection { }
        // the collection goes into a hidden local
        NyxToken collName = {TOKEN_IDENTIFIER, " for_coll", 9, varName.line};
        addLocal(collName);
        markInitialized();

        // index counter — hidden, starts at 0
        emitConstant(INT_VAL(0));
        NyxToken idxName = {TOKEN_IDENTIFIER, " for_idx", 8, varName.line};
        addLocal(idxName);
        markInitialized();

        int collSlot = current->localCount - 2;
        int idxSlot = current->localCount - 1;

        int loopStart = currentChunk()->count;
        beginLoop(&ctx, loopStart);
        current->forLoop = true;

        // loop condition: idx < len(collection)
        emitBytes(OP_GET_LOCAL, (uint8_t)idxSlot);
        emitBytes(OP_GET_LOCAL, (uint8_t)collSlot);
        // get length via .len() method call
        uint8_t lenName = identifierConstant(
            &(NyxToken){TOKEN_IDENTIFIER, "len", 3, varName.line});
        emitBytes(OP_INVOKE, lenName);
        emitByte(0); // 0 args
        emitByte(OP_LESS);

        int exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);

        // create the iteration variable(s) in a new scope
        beginScope();

        if (twoVars) {
            // two-variable mode: key + value. uses .keys() to unify maps and lists
            // (lists return [0,1,2,...] from keys() — same codepath for everything)

            // key = collection.keys()[idx] — emit the whole chain
            emitBytes(OP_GET_LOCAL, (uint8_t)collSlot);
            uint8_t keysName = identifierConstant(
                &(NyxToken){TOKEN_IDENTIFIER, "keys", 4, varName.line});
            emitBytes(OP_INVOKE, keysName);
            emitByte(0);
            emitBytes(OP_GET_LOCAL, (uint8_t)idxSlot);
            emitByte(OP_INDEX_GET);
            addLocal(varName);
            markInitialized();

            // value = collection[key]
            int keySlot = current->localCount - 1;
            emitBytes(OP_GET_LOCAL, (uint8_t)collSlot);
            emitBytes(OP_GET_LOCAL, (uint8_t)keySlot);
            emitByte(OP_INDEX_GET);
            addLocal(valueName);
            markInitialized();
        } else {
            // single variable: for item in list — just index into it
            // item = collection[idx]
            emitBytes(OP_GET_LOCAL, (uint8_t)collSlot);
            emitBytes(OP_GET_LOCAL, (uint8_t)idxSlot);
            emitByte(OP_INDEX_GET);
            addLocal(varName);
            markInitialized();
        }

        if (match(TOKEN_LEFT_BRACE)) {
            block();
        } else {
            statement();
        }
        endScope();

        // patch continue jumps to the increment
        patchContinueJumps();

        // Increment index
        emitBytes(OP_GET_LOCAL, (uint8_t)idxSlot);
        emitConstant(INT_VAL(1));
        emitByte(OP_ADD);
        emitBytes(OP_SET_LOCAL, (uint8_t)idxSlot);
        emitByte(OP_POP);

        emitLoop(loopStart);
        patchJump(exitJump);
        emitByte(OP_POP);
        endLoop(&ctx);
    }

    endScope();
}

// Loop Context Helpers

static void beginLoop(LoopContext* ctx, int loopStart) {
    ctx->prevLoopStart = current->loopStart;
    ctx->prevLoopScopeDepth = current->loopScopeDepth;
    ctx->prevBreakCount = current->breakCount;
    ctx->prevContinueCount = current->continueCount;
    ctx->prevForLoop = current->forLoop;
    memcpy(ctx->prevBreakJumps, current->breakJumps, sizeof(int) * current->breakCount);
    memcpy(ctx->prevContinueJumps, current->continueJumps, sizeof(int) * current->continueCount);

    current->loopStart = loopStart;
    current->loopScopeDepth = current->scopeDepth;
    current->breakCount = 0;
    current->continueCount = 0;
    current->forLoop = false;
}

static void patchContinueJumps(void) {
    // Patch all continue forward-jumps to here (called before the increment in for-loops)
    for (int i = 0; i < current->continueCount; i++) {
        patchJump(current->continueJumps[i]);
    }
    current->continueCount = 0;
}

static void endLoop(LoopContext* ctx) {
    for (int i = 0; i < current->breakCount; i++) {
        patchJump(current->breakJumps[i]);
    }

    current->loopStart = ctx->prevLoopStart;
    current->loopScopeDepth = ctx->prevLoopScopeDepth;
    current->breakCount = ctx->prevBreakCount;
    current->continueCount = ctx->prevContinueCount;
    current->forLoop = ctx->prevForLoop;
    memcpy(current->breakJumps, ctx->prevBreakJumps, sizeof(int) * ctx->prevBreakCount);
    memcpy(current->continueJumps, ctx->prevContinueJumps, sizeof(int) * ctx->prevContinueCount);
}

static void breakStatement(void) {
    if (current->loopStart == -1) {
        error("Cannot use 'break' outside of a loop.");
        return;
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after 'break'.");

    // Pop locals in the loop's scope
    int depth = current->scopeDepth;
    for (int i = current->localCount - 1; i >= 0; i--) {
        if (current->locals[i].depth <= current->loopScopeDepth) break;
        if (current->locals[i].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
    }

    if (current->breakCount < 64) {
        current->breakJumps[current->breakCount++] = emitJump(OP_JUMP);
    } else {
        error("Too many break statements in one loop.");
    }
}

static void continueStatement(void) {
    if (current->loopStart == -1) {
        error("Cannot use 'continue' outside of a loop.");
        return;
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after 'continue'.");

    // Pop locals in inner scopes
    for (int i = current->localCount - 1; i >= 0; i--) {
        if (current->locals[i].depth <= current->loopScopeDepth) break;
        if (current->locals[i].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
    }

    if (current->forLoop) {
        // For-loops: emit forward jump, patched later at the increment
        if (current->continueCount < 64) {
            current->continueJumps[current->continueCount++] = emitJump(OP_JUMP);
        }
    } else {
        // While/loop: jump directly back to the loop condition
        emitLoop(current->loopStart);
    }
}

static void loopStatement(void) {
    LoopContext ctx;
    int loopStart = currentChunk()->count;
    beginLoop(&ctx, loopStart);

    bodyStatement();

    emitLoop(loopStart);
    endLoop(&ctx);
}

// match as expression: var x = match n { 1 => "one", _ => "other" };
// Strategy: match value is on the stack. Each arm: DUP, compare, if match
// then pop match value under result using SET_LOCAL trick.
static void matchExpr(bool canAssign) {
    (void)canAssign;
    // stash the match value in a hidden local. result goes in the same slot
    beginScope();
    expression(); // match value → stack

    NyxToken mv = {TOKEN_IDENTIFIER, " me", 3, parser.previous.line};
    addLocal(mv);
    markInitialized();
    int matchSlot = current->localCount - 1;

    consume(TOKEN_LEFT_BRACE, "Expected '{' after match expression.");

    int endJumps[256];
    int endJumpCount = 0;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        // wildcard: _
        bool isWildcard = (check(TOKEN_IDENTIFIER) && parser.current.length == 1 &&
                           parser.current.start[0] == '_');

        int nextArm = -1;

        if (isWildcard) {
            advance();
            // wildcard with optional guard
            if (match(TOKEN_IF)) {
                expression(); // guard condition
                nextArm = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP); // pop true
            }
        } else {
            // compare against the pattern
            emitBytes(OP_GET_LOCAL, (uint8_t)matchSlot);
            expression(); // pattern value
            emitByte(OP_EQUAL);

            // optional guard clause
            if (match(TOKEN_IF)) {
                int skipGuard = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP); // pop true from pattern match
                expression(); // guard condition
                nextArm = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP); // pop true from guard
                int skipFalse = emitJump(OP_JUMP);
                patchJump(skipGuard);
                emitByte(OP_POP); // pop false from pattern
                emitByte(OP_FALSE); // push false for nextArm pop
                patchJump(skipFalse);
            } else {
                nextArm = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP); // pop true
            }
        }

        consume(TOKEN_FAT_ARROW, "Expected '=>' after pattern.");
        if (match(TOKEN_LEFT_BRACE)) {
            beginScope();
            while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
                declaration();
            consume(TOKEN_RIGHT_BRACE, "Expected '}' after match arm block.");
            endScope();
            emitByte(OP_NIL);
        } else {
            expression();
        }
        emitBytes(OP_SET_LOCAL, (uint8_t)matchSlot);
        emitByte(OP_POP);

        if (endJumpCount < 256) endJumps[endJumpCount++] = emitJump(OP_JUMP);

        if (nextArm != -1) {
            patchJump(nextArm);
            emitByte(OP_POP); // pop false
        }
        match(TOKEN_COMMA);

        // bare wildcard is always last — done
        if (isWildcard && nextArm == -1) break;
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after match.");
    for (int i = 0; i < endJumpCount; i++) patchJump(endJumps[i]);

    // pull the result out of the hidden slot before scope cleanup
    emitBytes(OP_GET_LOCAL, (uint8_t)matchSlot);
    endScope(); // pops the hidden local (1 POP)
    // stack: [result] — clean

    current->lastExprType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);
}

static void matchStatement(void) {
    // match statement — pattern matching with optional guards
    beginScope();

    expression(); // evaluate the value to match on

    // stash match value as hidden local
    NyxToken matchName = {TOKEN_IDENTIFIER, " match_val", 10, parser.previous.line};
    addLocal(matchName);
    markInitialized();
    int matchSlot = current->localCount - 1;

    consume(TOKEN_LEFT_BRACE, "Expected '{' after match expression.");

    int endJumps[256];
    int endJumpCount = 0;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        // Wildcard detection handled inline below

        // wildcard: _
        if (check(TOKEN_IDENTIFIER) && parser.current.length == 1 &&
            parser.current.start[0] == '_') {
            advance();
            // wildcard arm
        } else {
            // Compile pattern expression
            emitBytes(OP_GET_LOCAL, (uint8_t)matchSlot);
            expression();
            emitByte(OP_EQUAL);

            // Check for guard: if condition
            if (match(TOKEN_IF)) {
                // If pattern didn't match, skip the guard
                int skipGuard = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP);

                // Evaluate guard condition
                expression();

                // If guard fails, jump to next arm
                int nextArm = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP);

                // Pattern + guard matched — compile arm body
                consume(TOKEN_FAT_ARROW, "Expected '=>' after match pattern.");
                if (match(TOKEN_LEFT_BRACE)) {
                    beginScope();
                    block();
                    endScope();
                } else {
                    expression();
                    emitByte(OP_POP);
                }

                if (endJumpCount < 256) {
                    endJumps[endJumpCount++] = emitJump(OP_JUMP);
                }

                // Guard failed
                patchJump(nextArm);
                emitByte(OP_POP);

                // Pattern didn't match
                patchJump(skipGuard);
                emitByte(OP_POP);

                match(TOKEN_COMMA);
                continue;
            }

            // No guard — just pattern match
            int nextArm = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);

            consume(TOKEN_FAT_ARROW, "Expected '=>' after match pattern.");
            if (match(TOKEN_LEFT_BRACE)) {
                beginScope();
                block();
                endScope();
            } else {
                expression();
                emitByte(OP_POP);
            }

            if (endJumpCount < 256) {
                endJumps[endJumpCount++] = emitJump(OP_JUMP);
            }

            patchJump(nextArm);
            emitByte(OP_POP);

            match(TOKEN_COMMA);
            continue;
        }

        // Wildcard arm
        consume(TOKEN_FAT_ARROW, "Expected '=>' after '_'.");
        if (match(TOKEN_LEFT_BRACE)) {
            beginScope();
            block();
            endScope();
        } else {
            expression();
            emitByte(OP_POP);
        }

        match(TOKEN_COMMA);
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after match body.");

    // patch all the escape hatches
    for (int i = 0; i < endJumpCount; i++) {
        patchJump(endJumps[i]);
    }

    endScope();
}

static void returnStatement(void) {
    if (current->type == TYPE_SCRIPT) {
        error("Cannot return from top-level code.");
    }

    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        if (current->type == TYPE_INITIALIZER) {
            error("Cannot return a value from an initializer.");
        }
        expression();
        // type-check the return value (if we're doing that)
        if (current->returnType.tag != NYX_TYPE_UNKNOWN &&
            current->lastExprType.tag != NYX_TYPE_UNKNOWN) {
            if (!nyx_type_compatible(current->returnType, current->lastExprType)) {
                error("Return type mismatch.");
            }
        }
        consume(TOKEN_SEMICOLON, "Expected ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void statement(void) {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_LOOP)) {
        loopStatement();
    } else if (match(TOKEN_MATCH)) {
        matchStatement();
    } else if (match(TOKEN_BREAK)) {
        breakStatement();
    } else if (match(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

// Declarations (bringing things into existence)

static void declareVariable(void) {
    if (current->scopeDepth == 0) return;

    NyxToken* name = &parser.previous;

    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }

    addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    // track which params have defaults so we can emit nil-checks
    int defaultParams[255];
    int defaultCount = 0;
    int requiredArity = 0;

    consume(TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            // varargs: ...rest
            if (match(TOKEN_DOT_DOT_DOT)) {
                current->function->isVariadic = true;
                current->function->arity++; // the varargs param counts as 1
                uint8_t constant = parseVariable("Expected varargs parameter name.");
                defineVariable(constant);
                // varargs must be last — bail
                break;
            }

            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Cannot have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expected parameter name.");
            if (match(TOKEN_COLON)) {
                NyxType paramType = parseTypeAnnotation();
                if (current->localCount > 0) {
                    current->locals[current->localCount - 1].type = paramType;
                }
            }
            defineVariable(constant);

            // default value: param = expr
            if (match(TOKEN_EQUAL)) {
                int paramSlot = current->localCount - 1;
                defaultParams[defaultCount++] = paramSlot;
                // if the caller didn't pass it (nil), use the default
                emitBytes(OP_GET_LOCAL, (uint8_t)paramSlot);
                emitByte(OP_NIL);
                emitByte(OP_EQUAL);
                int skipJump = emitJump(OP_JUMP_IF_FALSE);
                emitByte(OP_POP);
                expression(); // compile default value
                emitBytes(OP_SET_LOCAL, (uint8_t)paramSlot);
                emitByte(OP_POP);
                int endJump = emitJump(OP_JUMP);
                patchJump(skipJump);
                emitByte(OP_POP);
                patchJump(endJump);
            } else {
                requiredArity = current->function->arity;
            }
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

    // optional return type: -> int
    if (match(TOKEN_ARROW)) {
        current->returnType = parseTypeAnnotation();
    }

    consume(TOKEN_LEFT_BRACE, "Expected '{' before function body.");
    block();

    NyxObjFunction* fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant8(OBJ_VAL(fn)));

    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void method(void) {
    consume(TOKEN_IDENTIFIER, "Expected method name.");
    uint8_t constant = identifierConstant(&parser.previous);

    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
        memcmp(parser.previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }
    function(type);
    emitBytes(OP_METHOD, constant);
}

// enums compile down to a map. Color.RED is just Color["RED"] which is 0. simple
static void enumDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expected enum name.");
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    // build the backing map
    consume(TOKEN_LEFT_BRACE, "Expected '{' after enum name.");

    int count = 0;
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            if (check(TOKEN_RIGHT_BRACE)) break;
            consume(TOKEN_IDENTIFIER, "Expected enum variant name.");
            // key = variant name
            emitConstant(OBJ_VAL(nyx_copy_string(
                parser.previous.start, parser.previous.length)));
            // value = ordinal
            emitConstant(INT_VAL(count));
            count++;
        } while (match(TOKEN_COMMA));
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after enum variants.");
    emitBytes(OP_BUILD_MAP, (uint8_t)count);
    defineVariable(nameConstant);
}

static void classDeclaration(void) {
    consume(TOKEN_IDENTIFIER, "Expected class name.");
    NyxToken className = parser.previous;
    uint8_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();

    emitBytes(OP_CLASS, nameConstant);
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    // inheritance: class Dog : Animal
    if (match(TOKEN_COLON)) {
        consume(TOKEN_IDENTIFIER, "Expected superclass name.");
        variable(false); // push superclass

        if (identifiersEqual(&className, &parser.previous)) {
            error("A class cannot inherit from itself.");
        }

        // 'super' gets its own scope — it's special
        beginScope();
        addLocal((NyxToken){TOKEN_IDENTIFIER, "super", 5, parser.previous.line});
        markInitialized();
        defineVariable(0);

        namedVariable(className, false);
        emitByte(OP_INHERIT);
        classCompiler.hasSuperclass = true;
    }

    namedVariable(className, false); // push class for OP_METHOD
    consume(TOKEN_LEFT_BRACE, "Expected '{' before class body.");

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        // skip field declarations — they're parsed but don't emit code
        if (check(TOKEN_LET)) {
            advance(); // consume 'let'
            advance(); // consume field name
            if (match(TOKEN_COLON)) {
                skipTypeAnnotation();
            }
            consume(TOKEN_SEMICOLON, "Expected ';' after field declaration.");
        } else if (check(TOKEN_FN) || check(TOKEN_INIT)) {
            if (match(TOKEN_INIT)) {
                // init keyword → pretend it's an identifier for method purposes
                parser.previous.type = TOKEN_IDENTIFIER;
                uint8_t initConst = identifierConstant(&parser.previous);

                FunctionType type = TYPE_INITIALIZER;
                // compile init as TYPE_INITIALIZER so it auto-returns self
                Compiler compiler;
                initCompiler(&compiler, type);
                beginScope();

                consume(TOKEN_LEFT_PAREN, "Expected '(' after 'init'.");
                if (!check(TOKEN_RIGHT_PAREN)) {
                    do {
                        current->function->arity++;
                        if (current->function->arity > 255) {
                            errorAtCurrent("Cannot have more than 255 parameters.");
                        }
                        uint8_t param = parseVariable("Expected parameter name.");
                        if (match(TOKEN_COLON)) skipTypeAnnotation();
                        defineVariable(param);
                    } while (match(TOKEN_COMMA));
                }
                consume(TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

                consume(TOKEN_LEFT_BRACE, "Expected '{' before init body.");
                block();

                NyxObjFunction* fn = endCompiler();
                emitBytes(OP_CLOSURE, makeConstant8(OBJ_VAL(fn)));
                for (int i = 0; i < fn->upvalueCount; i++) {
                    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
                    emitByte(compiler.upvalues[i].index);
                }
                emitBytes(OP_METHOD, initConst);
            } else {
                // regular method
                advance(); // consume 'fn'
                method();
            }
        } else {
            errorAtCurrent("Expected method or field declaration in class body.");
            advance();
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expected '}' after class body.");
    emitByte(OP_POP); // pop the class

    if (classCompiler.hasSuperclass) {
        endScope();
    }

    currentClass = currentClass->enclosing;
}

static void fnDeclaration(void) {
    uint8_t global = parseVariable("Expected function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void varDeclaration(void) {
    uint8_t global = parseVariable("Expected variable name.");
    NyxType declaredType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);

    if (match(TOKEN_COLON)) {
        declaredType = parseTypeAnnotation();
    }

    if (match(TOKEN_EQUAL)) {
        expression();
        // no type annotation? infer it from whatever was assigned
        if (declaredType.tag == NYX_TYPE_UNKNOWN) {
            declaredType = current->lastExprType;
        } else if (current->lastExprType.tag != NYX_TYPE_UNKNOWN) {
            // type mismatch? yell at the user
            if (!nyx_type_compatible(declaredType, current->lastExprType)) {
                error("Type mismatch in 'var' declaration.");
            }
        }
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

    // stash the type info on the local
    if (current->scopeDepth > 0 && current->localCount > 0) {
        current->locals[current->localCount - 1].type = declaredType;
    }

    defineVariable(global);
}

static void letDeclaration(void) {
    uint8_t global = parseVariable("Expected variable name.");
    NyxType declaredType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);

    if (match(TOKEN_COLON)) {
        declaredType = parseTypeAnnotation();
    }

    if (match(TOKEN_EQUAL)) {
        expression();
        if (declaredType.tag != NYX_TYPE_UNKNOWN && current->lastExprType.tag != NYX_TYPE_UNKNOWN) {
            if (!nyx_type_compatible(declaredType, current->lastExprType)) {
                error("Type mismatch in 'let' declaration.");
            }
        }
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration.");

    if (current->scopeDepth > 0 && current->localCount > 0) {
        current->locals[current->localCount - 1].type = declaredType;
    }

    defineVariable(global);
}

static void constDeclaration(void) {
    uint8_t global = parseVariable("Expected constant name.");
    NyxType declaredType = NYX_TYPE_MAKE(NYX_TYPE_UNKNOWN);

    if (match(TOKEN_COLON)) {
        declaredType = parseTypeAnnotation();
    }

    consume(TOKEN_EQUAL, "Constants must be initialized.");
    expression();

    if (declaredType.tag != NYX_TYPE_UNKNOWN && current->lastExprType.tag != NYX_TYPE_UNKNOWN) {
        if (!nyx_type_compatible(declaredType, current->lastExprType)) {
            error("Type mismatch in 'const' declaration.");
        }
    }

    consume(TOKEN_SEMICOLON, "Expected ';' after constant declaration.");

    if (current->scopeDepth > 0 && current->localCount > 0) {
        current->locals[current->localCount - 1].type = declaredType;
    }

    defineVariable(global);
}

static void importDeclaration(void) {
    // import "module_name" or import module_name
    if (match(TOKEN_STRING)) {
        uint8_t constant = makeConstant8(OBJ_VAL(
            nyx_copy_string(parser.previous.start + 1, parser.previous.length - 2)));
        emitBytes(OP_IMPORT, constant);
    } else {
        consume(TOKEN_IDENTIFIER, "Expected module name after 'import'.");
        uint8_t constant = identifierConstant(&parser.previous);
        emitBytes(OP_IMPORT, constant);
    }
    // module runs as a function — pop the return value it leaves behind
    emitByte(OP_POP);
    consume(TOKEN_SEMICOLON, "Expected ';' after import.");
}

static void declaration(void) {
    if (match(TOKEN_CLASS)) {
        classDeclaration();
    } else if (match(TOKEN_ENUM)) {
        enumDeclaration();
    } else if (match(TOKEN_FN)) {
        fnDeclaration();
    } else if (match(TOKEN_VAR)) {
        varDeclaration();
    } else if (match(TOKEN_LET)) {
        letDeclaration();
    } else if (match(TOKEN_CONST)) {
        constDeclaration();
    } else if (match(TOKEN_IMPORT)) {
        importDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

// Public API

NyxObjFunction* nyx_compile(const char* source) {
    nyx_scanner_init(&parser.scanner, source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    NyxObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

void nyx_mark_compiler_roots(void) {
    Compiler* compiler = current;
    while (compiler != NULL) {
        nyx_mark_object((NyxObj*)compiler->function);
        compiler = compiler->enclosing;
    }
}
