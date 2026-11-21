#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "table.h"
#include "vm.h"

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef struct {
  Token name;
  int depth;
  bool isConstant;
  bool isCaptured;
} Local;

typedef struct {
  uint32_t index;
  bool isLocal;
  bool isConstant;
} Upvalue;

typedef struct Loop {
  struct Loop* enclosing;
  int continueTarget;
  int scopeDepth;
} Loop;

typedef enum {
  TYPE_FUNCTION,
  TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction* function;
  FunctionType type;
  Table nameConstants;
  Local* locals;
  int localCount;
  int localCapacity;
  Upvalue* upvalues;
  int upvalueCapacity;
  int scopeDepth;
  Loop* loop;
} Compiler;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL
} Precedence;

static Parser parser;
static Compiler* current;
static Chunk* compilingChunk;
static Table pendingConstants;

static void expression(void);
static void declaration(void);
static void statement(void);
static void parsePrecedence(Precedence precedence);

static void errorAt(const Token* token, const char* message) {
  if (parser.panicMode) return;

  parser.panicMode = true;
  parser.hadError = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type != TOKEN_ERROR) {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void advance(void) {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();

    if (parser.current.type != TOKEN_ERROR) break;

    errorAt(&parser.current, parser.current.start);
  }
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;

  advance();
  return true;
}

static bool consume(TokenType type, const char* message) {
  if (check(type)) {
    advance();
    return true;
  }

  errorAt(&parser.current, message);
  return false;
}

static void emitByte(uint8_t byte) {
  writeChunk(compilingChunk, byte, parser.previous.line);
}

static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return compilingChunk->count - 2;
}

static void patchJump(int offset) {
  int distance = compilingChunk->count - offset - 2;

  if (distance > UINT16_MAX) {
    error("Jump too large");
  }

  compilingChunk->code[offset] = (uint8_t)(distance >> 8);
  compilingChunk->code[offset + 1] = (uint8_t)distance;
}

static void emitLoop(int target) {
  emitByte(OP_LOOP);
  int distance = compilingChunk->count - target + 2;

  if (distance > UINT16_MAX) {
    error("Loop too large");
  }

  emitByte((uint8_t)(distance >> 8));
  emitByte((uint8_t)distance);
}

static void emitIndexed(uint8_t shortOp, uint8_t longOp, int index) {
  if (index <= 255) {
    emitByte(shortOp);
    emitByte((uint8_t)index);
  } else {
    emitByte(longOp);
    emitByte((uint8_t)(index >> 16));
    emitByte((uint8_t)(index >> 8));
    emitByte((uint8_t)index);
  }
}

static int makeConstant(Value value) {
  if (compilingChunk->constants.count > 0xffffff) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return addConstant(compilingChunk, value);
}

static void emitConstant(Value value) {
  emitIndexed(OP_CONSTANT, OP_CONSTANT_LONG, makeConstant(value));
}

static bool lookupName(Table* table, const Token* name, Value* value) {
  ObjString probe;
  probe.obj.type = OBJ_STRING;
  probe.obj.next = NULL;
  probe.length = name->length;
  probe.externalChars = name->start;

  return tableGet(table, OBJ_VAL(&probe), value);
}

static int identifierConstant(const Token* name) {
  Value existing;

  if (lookupName(&current->nameConstants, name, &existing)) {
    return (int)AS_NUMBER(existing);
  }

  Value key = OBJ_VAL(copyString(name->start, name->length));
  int index = makeConstant(key);

  tableSet(&current->nameConstants, key, NUMBER_VAL(index));
  return index;
}

static bool globalIsConstant(const Token* name) {
  Value ignored;

  return lookupName(&pendingConstants, name, &ignored) ||
      lookupName(&vm.globalConstants, name, &ignored);
}

static bool namesEqual(const Token* a, const Token* b) {
  return a->length == b->length &&
      memcmp(a->start, b->start, (size_t)a->length) == 0;
}

static int addLocal(Token name, bool isConstant) {
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];

    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (namesEqual(&name, &local->name)) {
      error("Already a variable with this name.");
      return -1;
    }
  }

  if (current->localCount > 0xffffff) {
    error("Too many local variables.");
    return -1;
  }

  if (current->localCapacity < current->localCount + 1) {
    int oldCapacity = current->localCapacity;
    current->localCapacity = GROW_CAPACITY(oldCapacity);
    current->locals = GROW_ARRAY(
        Local, current->locals, oldCapacity, current->localCapacity);
  }

  int slot = current->localCount++;
  current->locals[slot] = (Local){name, -1, isConstant, false};

  return slot;
}

static void initCompiler(
    Compiler* compiler, FunctionType type, const Token* name) {
  memset(compiler, 0, sizeof(*compiler));
  compiler->enclosing = current;
  compiler->type = type;
  compiler->function = newFunction();
  initTable(&compiler->nameConstants);
  current = compiler;
  compilingChunk = &compiler->function->chunk;

  if (type == TYPE_FUNCTION) {
    compiler->function->name = copyString(name->start, name->length);
  }

  Token hidden = {TOKEN_IDENTIFIER, "", 0, name->line};
  int slot = addLocal(hidden, true);
  compiler->locals[slot].depth = 0;
}

static ObjFunction* endCompiler(void) {
  emitByte(OP_NIL);
  emitByte(OP_RETURN);
  ObjFunction* function = current->function;
  freeTable(&current->nameConstants);
  FREE_ARRAY(Local, current->locals, current->localCapacity);
  current = current->enclosing;
  compilingChunk = current != NULL ? &current->function->chunk : NULL;
  return function;
}

static int resolveLocal(Compiler* compiler, const Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];

    if (namesEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Cannot read local variable in its own start");
      }

      return i;
    }
  }

  return -1;
}

static int addUpvalue(
    Compiler* compiler, uint32_t index, bool isLocal, bool isConstant) {
  int count = compiler->function->upvalueCount;

  for (int i = 0; i < count; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];

    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (count > 0xffffff) {
    error("Too many closure variables in function.");
    return 0;
  }

  if (compiler->upvalueCapacity < count + 1) {
    int oldCapacity = compiler->upvalueCapacity;
    compiler->upvalueCapacity = GROW_CAPACITY(oldCapacity);
    compiler->upvalues = GROW_ARRAY(
        Upvalue, compiler->upvalues,
        oldCapacity, compiler->upvalueCapacity);
  }

  compiler->upvalues[count] = (Upvalue){index, isLocal, isConstant};
  compiler->function->upvalueCount++;
  return count;
}

static int resolveUpvalue(Compiler* compiler, const Token* name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }

  int local = resolveLocal(compiler->enclosing, name);

  if (local >= 0) {
    Local* captured = &compiler->enclosing->locals[local];
    captured->isCaptured = true;

    return addUpvalue(
        compiler, (uint32_t)local, true, captured->isConstant);
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);

  if (upvalue >= 0) {
    return addUpvalue(
        compiler, (uint32_t)upvalue, false,
        compiler->enclosing->upvalues[upvalue].isConstant);
  }

  return -1;
}

static void beginScope(void) {
  current->scopeDepth++;
}

static void endScope(void) {
  current->scopeDepth--;

  while (current->localCount > 0 &&
      current->locals[current->localCount - 1].depth > current->scopeDepth) {
    Local* local = &current->locals[current->localCount - 1];
    emitByte(local->isCaptured ? OP_CLOSE_UPVALUE : OP_POP);
    current->localCount--;
  }
}

static Precedence precedenceFor(TokenType type) {
  switch (type) {
    case TOKEN_LEFT_PAREN:
      return PREC_CALL;

    case TOKEN_OR:
      return PREC_OR;

    case TOKEN_AND:
      return PREC_AND;

    case TOKEN_BANG_EQUAL:
    case TOKEN_EQUAL_EQUAL:
      return PREC_EQUALITY;

    case TOKEN_GREATER:
    case TOKEN_GREATER_EQUAL:
    case TOKEN_LESS:
    case TOKEN_LESS_EQUAL:
      return PREC_COMPARISON;

    case TOKEN_PLUS:
    case TOKEN_MINUS:
      return PREC_TERM;

    case TOKEN_STAR:
    case TOKEN_SLASH:
      return PREC_FACTOR;

    default:
      return PREC_NONE;
  }
}

static void number(void) {
  emitConstant(NUMBER_VAL(strtod(parser.previous.start, NULL)));
}

static void string(void) {
  emitConstant(OBJ_VAL(copyString(
      parser.previous.start + 1,
      parser.previous.length - 2)));
}

static void grouping(void) {
  expression();
  consume(TOKEN_RIGHT_PAREN, ") after expression");
}

static void unary(void) {
  TokenType operatorType = parser.previous.type;
  parsePrecedence(PREC_UNARY);

  emitByte(operatorType == TOKEN_MINUS ? OP_NEGATE : OP_NOT);
}

static void variable(bool canAssign) {
  Token name = parser.previous;
  int slot = resolveLocal(current, &name);
  int upvalue;
  int index;
  uint8_t getOp;
  uint8_t getLongOp;
  uint8_t setOp;
  uint8_t setLongOp;
  bool isConstant;

  if (slot >= 0) {
    index = slot;
    getOp = OP_GET_LOCAL;
    getLongOp = OP_GET_LOCAL_LONG;
    setOp = OP_SET_LOCAL;
    setLongOp = OP_SET_LOCAL_LONG;
    isConstant = current->locals[slot].isConstant;
  } else if ((upvalue = resolveUpvalue(current, &name)) >= 0) {
    index = upvalue;
    getOp = OP_GET_UPVALUE;
    getLongOp = OP_GET_UPVALUE_LONG;
    setOp = OP_SET_UPVALUE;
    setLongOp = OP_SET_UPVALUE_LONG;
    isConstant = current->upvalues[upvalue].isConstant;
  } else {
    index = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    getLongOp = OP_GET_GLOBAL_LONG;
    setOp = OP_SET_GLOBAL;
    setLongOp = OP_SET_GLOBAL_LONG;
    isConstant = globalIsConstant(&name);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    if (isConstant) {
      errorAt(&name, "Cant assign to constant.");
    }

    expression();
    emitIndexed(setOp, setLongOp, index);
  } else {
    emitIndexed(getOp, getLongOp, index);
  }
}

static void andExpression(void) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  parsePrecedence(PREC_AND);
  patchJump(endJump);
}

static void orExpression(void) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);
  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void binary(void) {
  TokenType operatorType = parser.previous.type;
  parsePrecedence((Precedence)(precedenceFor(operatorType) + 1));

  switch (operatorType) {
    case TOKEN_PLUS:
      emitByte(OP_ADD);
      break;

    case TOKEN_MINUS:
      emitByte(OP_SUBTRACT);
      break;

    case TOKEN_STAR:
      emitByte(OP_MULTIPLY);
      break;

    case TOKEN_SLASH:
      emitByte(OP_DIVIDE);
      break;

    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
      break;

    case TOKEN_BANG_EQUAL:
      emitByte(OP_EQUAL);
      emitByte(OP_NOT);
      break;

    case TOKEN_GREATER:
      emitByte(OP_GREATER);
      break;

    case TOKEN_GREATER_EQUAL:
      emitByte(OP_LESS);
      emitByte(OP_NOT);
      break;

    case TOKEN_LESS:
      emitByte(OP_LESS);
      break;

    case TOKEN_LESS_EQUAL:
      emitByte(OP_GREATER);
      emitByte(OP_NOT);
      break;

    default:
      break;
  }
}

static void callExpression(void) {
  int argCount = 0;

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      argCount++;

      if (argCount > 255) {
        error("Too many arguments.");
      }
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, ") after arguments");
  emitByte(OP_CALL);
  emitByte((uint8_t)argCount);
}

static void parsePrecedence(Precedence precedence) {
  advance();
  bool canAssign = precedence <= PREC_ASSIGNMENT;

  switch (parser.previous.type) {
    case TOKEN_NUMBER:
      number();
      break;

    case TOKEN_STRING:
      string();
      break;

    case TOKEN_LEFT_PAREN:
      grouping();
      break;

    case TOKEN_MINUS:
    case TOKEN_BANG:
      unary();
      break;

    case TOKEN_IDENTIFIER:
      variable(canAssign);
      break;

    case TOKEN_NIL:
      emitByte(OP_NIL);
      break;

    case TOKEN_TRUE:
      emitByte(OP_TRUE);
      break;

    case TOKEN_FALSE:
      emitByte(OP_FALSE);
      break;

    default:
      error("Expect expression");
      return;
  }

  while (precedence <= precedenceFor(parser.current.type)) {
    advance();

    if (parser.previous.type == TOKEN_LEFT_PAREN) {
      callExpression();
    } else if (parser.previous.type == TOKEN_AND) {
      andExpression();
    } else if (parser.previous.type == TOKEN_OR) {
      orExpression();
    } else {
      binary();
    }
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static void expression(void) {
  parsePrecedence(PREC_ASSIGNMENT);
}

static void printStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "; after value");
  emitByte(OP_PRINT);
}

static void expressionStatement(void) {
  expression();
  consume(TOKEN_SEMICOLON, "; after expression");
  emitByte(OP_POP);
}

static void block(void) {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "} after block");
}

static void funDeclaration(void) {
  if (!consume(TOKEN_IDENTIFIER, "Expect function name.")) return;

  Token name = parser.previous;
  bool isLocal = current->scopeDepth > 0;
  int index;

  if (isLocal) {
    index = addLocal(name, false);

    if (index >= 0) {
      current->locals[index].depth = current->scopeDepth;
    }
  } else {
    if (globalIsConstant(&name)) {
      errorAt(&name, "Const name is already taken");
    }

    index = identifierConstant(&name);
  }

  Compiler compiler;
  initCompiler(&compiler, TYPE_FUNCTION, &name);
  beginScope();
  consume(TOKEN_LEFT_PAREN, "( after function name");

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;

      if (current->function->arity > 255) {
        error("Too many parameters.");
      }

      if (!consume(TOKEN_IDENTIFIER, "Expect parameter name.")) break;

      int slot = addLocal(parser.previous, false);

      if (slot >= 0) {
        current->locals[slot].depth = current->scopeDepth;
      }
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, ") after parameters");
  consume(TOKEN_LEFT_BRACE, "{ before function body");
  block();
  ObjFunction* function = endCompiler();

  emitIndexed(OP_CLOSURE, OP_CLOSURE_LONG, makeConstant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    Upvalue* upvalue = &compiler.upvalues[i];
    uint8_t flags = upvalue->isLocal ? 1 : 0;

    emitIndexed(
        flags, (uint8_t)(flags | 2), (int)upvalue->index);
  }

  FREE_ARRAY(Upvalue, compiler.upvalues, compiler.upvalueCapacity);

  if (!isLocal) {
    emitIndexed(OP_DEFINE_GLOBAL, OP_DEFINE_GLOBAL_LONG, index);
  }
}

static void returnStatement(void) {
  if (current->type == TYPE_SCRIPT) {
    error("Cannot return from top level.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitByte(OP_NIL);
  } else {
    expression();
    consume(TOKEN_SEMICOLON, "; after return value");
  }

  emitByte(OP_RETURN);
}

static void varDeclaration(bool isConstant) {
  if (!consume(TOKEN_IDENTIFIER, "Expect variable name.")) return;

  Token name = parser.previous;
  int index = -1;

  if (current->scopeDepth > 0) {
    index = addLocal(name, isConstant);
  } else {
    if (globalIsConstant(&name)) {
      errorAt(&name, "Const name is already taken");
    }

    index = identifierConstant(&name);

    if (isConstant) {
      Value key = compilingChunk->constants.values[index];
      tableSet(&pendingConstants, key, BOOL_VAL(true));
    }
  }

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    if (isConstant) {
      error("constant needs a starting value");
    }

    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "; after variable declaration");

  if (current->scopeDepth > 0) {
    if (index >= 0) {
      current->locals[index].depth = current->scopeDepth;
    }
  } else if (isConstant) {
    emitIndexed(
        OP_DEFINE_GLOBAL_CONST,
        OP_DEFINE_GLOBAL_CONST_LONG,
        index);
  } else {
    emitIndexed(OP_DEFINE_GLOBAL, OP_DEFINE_GLOBAL_LONG, index);
  }
}

static void ifStatement(void) {
  consume(TOKEN_LEFT_PAREN, "( after if");
  expression();
  consume(TOKEN_RIGHT_PAREN, ") after condition");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);
  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) {
    statement();
  }

  patchJump(elseJump);
}

static void whileStatement(void) {
  int start = compilingChunk->count;

  consume(TOKEN_LEFT_PAREN, "( after while");
  expression();
  consume(TOKEN_RIGHT_PAREN, ") after condition");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);

  Loop loop = {current->loop, start, current->scopeDepth};
  current->loop = &loop;
  statement();
  current->loop = loop.enclosing;

  emitLoop(start);
  patchJump(exitJump);
  emitByte(OP_POP);
}

static void forStatement(void) {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "( after for");

  int initialLocalCount = current->localCount;

  if (match(TOKEN_VAR)) {
    varDeclaration(false);
  } else if (match(TOKEN_CONST)) {
    varDeclaration(true);
  } else if (!match(TOKEN_SEMICOLON)) {
    expressionStatement();
  }

  int loopSlot = current->localCount > initialLocalCount
      ? initialLocalCount
      : -1;

  int conditionStart = compilingChunk->count;
  int exitJump = -1;

  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "; after loop condition");
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  int bodyJump = emitJump(OP_JUMP);
  int continueTarget = compilingChunk->count;

  if (loopSlot >= 0) {
    emitIndexed(OP_CLOSE_LOCAL, OP_CLOSE_LOCAL_LONG, loopSlot);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, ") after for clauses");
  }

  emitLoop(conditionStart);
  patchJump(bodyJump);

  Loop loop = {current->loop, continueTarget, current->scopeDepth};
  current->loop = &loop;
  statement();
  current->loop = loop.enclosing;

  emitLoop(continueTarget);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }

  endScope();
}

static void continueStatement(void) {
  consume(TOKEN_SEMICOLON, "; after continue");

  if (current->loop == NULL) {
    error("continue needs a loop");
    return;
  }

  for (int i = current->localCount - 1; i >= 0; i--) {
    if (current->locals[i].depth <= current->loop->scopeDepth) {
      break;
    }

    emitByte(current->locals[i].isCaptured ? OP_CLOSE_UPVALUE : OP_POP);
  }

  emitLoop(current->loop->continueTarget);
}

static void caseBody(void) {
  while (!check(TOKEN_CASE) && !check(TOKEN_DEFAULT) &&
      !check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }
}

static void switchStatement(void) {
  consume(TOKEN_LEFT_PAREN, "( after switch");
  beginScope();
  expression();
  consume(TOKEN_RIGHT_PAREN, ") after switch value");

  Token hidden = {TOKEN_IDENTIFIER, "", 0, parser.previous.line};
  int slot = addLocal(hidden, true);

  if (slot >= 0) {
    current->locals[slot].depth = current->scopeDepth;
  } else {
    slot = 0;
  }

  consume(TOKEN_LEFT_BRACE, "{ before cases");

  int* endJumps = NULL;
  int endCount = 0;
  int endCapacity = 0;
  bool hasDefault = false;

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    if (match(TOKEN_CASE)) {
      if (hasDefault) {
        error("case after default");
      }

      emitIndexed(OP_GET_LOCAL, OP_GET_LOCAL_LONG, slot);
      expression();
      consume(TOKEN_COLON, ": after case");
      emitByte(OP_EQUAL);

      int nextCase = emitJump(OP_JUMP_IF_FALSE);
      emitByte(OP_POP);

      beginScope();
      caseBody();
      endScope();

      if (endCapacity < endCount + 1) {
        int oldCapacity = endCapacity;
        endCapacity = GROW_CAPACITY(oldCapacity);
        endJumps = GROW_ARRAY(
            int, endJumps, oldCapacity, endCapacity);
      }

      endJumps[endCount++] = emitJump(OP_JUMP);
      patchJump(nextCase);
      emitByte(OP_POP);
    } else if (match(TOKEN_DEFAULT)) {
      if (hasDefault) {
        error("default already used");
      }

      hasDefault = true;
      consume(TOKEN_COLON, ": after default");

      beginScope();
      caseBody();
      endScope();
    } else {
      errorAt(&parser.current, "Expect case or default");
      advance();
    }
  }

  consume(TOKEN_RIGHT_BRACE, "} after switch");

  for (int i = 0; i < endCount; i++) {
    patchJump(endJumps[i]);
  }

  FREE_ARRAY(int, endJumps, endCapacity);
  endScope();
}

static void synchronize(void) {
  parser.panicMode = false;

  while (!check(TOKEN_EOF)) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.current.type) {
      case TOKEN_FUN:
      case TOKEN_RETURN:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_FOR:
      case TOKEN_SWITCH:
      case TOKEN_CASE:
      case TOKEN_DEFAULT:
      case TOKEN_CONTINUE:
      case TOKEN_VAR:
      case TOKEN_CONST:
      case TOKEN_PRINT:
      case TOKEN_LEFT_BRACE:
      case TOKEN_RIGHT_BRACE:
        return;

      default:
        advance();
    }
  }
}

static void statement(void) {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_SWITCH)) {
    switchStatement();
  } else if (match(TOKEN_CONTINUE)) {
    continueStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

static void declaration(void) {
  if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration(false);
  } else if (match(TOKEN_CONST)) {
    varDeclaration(true);
  } else {
    statement();
  }

  if (parser.panicMode) {
    synchronize();
  }
}

ObjFunction* compile(const char* source) {
  initScanner(source);
  memset(&parser, 0, sizeof(parser));
  parser.current = (Token){TOKEN_EOF, "", 0, 1};
  parser.previous = parser.current;
  current = NULL;
  initTable(&pendingConstants);

  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT, &parser.current);
  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = endCompiler();
  FREE_ARRAY(Upvalue, compiler.upvalues, compiler.upvalueCapacity);
  freeTable(&pendingConstants);
  return parser.hadError ? NULL : function;
}
