#include <stdio.h>

#include "debug.h"
#include "object.h"
#include "value.h"


static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

static uint32_t readOperand(Chunk* chunk, int offset, bool isLong) {
  uint32_t index = chunk->code[offset + 1];

  if (isLong) {
    index = (index << 16) |
        ((uint32_t)chunk->code[offset + 2] << 8) |
        (uint32_t)chunk->code[offset + 3];
  }

  return index;
}

static int indexedInstruction(
    const char* name, Chunk* chunk, int offset, bool isLong) {
  uint32_t index = readOperand(chunk, offset, isLong);
  printf("%-28s %8u '", name, (unsigned int)index);
  printValue(chunk->constants.values[index]);
  printf("'\n");
  return offset + (isLong ? 4 : 2);
}

static int closureInstruction(
    const char* name, Chunk* chunk, int offset, bool isLong) {
  uint32_t constant = readOperand(chunk, offset, isLong);
  ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);

  offset = indexedInstruction(name, chunk, offset, isLong);

  for (int i = 0; i < function->upvalueCount; i++) {
    uint8_t flags = chunk->code[offset];
    bool longIndex = (flags & 2) != 0;
    uint32_t index = readOperand(chunk, offset, longIndex);

    printf(
        "%04d      |                     %s %u\n",
        offset, (flags & 1) != 0 ? "local" : "upvalue",
        (unsigned int)index);

    offset += longIndex ? 4 : 2;
  }

  return offset;
}

static int localInstruction(
    const char* name, Chunk* chunk, int offset, bool isLong) {
  uint32_t slot = readOperand(chunk, offset, isLong);
  printf("%-28s %8u\n", name, (unsigned int)slot);
  return offset + (isLong ? 4 : 2);
}

static int jumpInstruction(
    const char* name, int sign, Chunk* chunk, int offset) {
  uint16_t jump = ((uint16_t)chunk->code[offset + 1] << 8) |
      chunk->code[offset + 2];

  printf("%-28s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstruction(chunk, offset);
  }
}

int disassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);
  int line = getLine(chunk, offset);

  if (offset > 0 && line == getLine(chunk, offset - 1)) {
    printf("   | ");
  } else {
    printf("%4d ", line);
  }

  switch (chunk->code[offset]) {
    case OP_CONSTANT:
      return indexedInstruction("OP_CONSTANT", chunk, offset, false);
    case OP_CONSTANT_LONG:
      return indexedInstruction("OP_CONSTANT_LONG", chunk, offset, true);
    case OP_GET_LOCAL:
      return localInstruction("OP_GET_LOCAL", chunk, offset, false);
    case OP_GET_LOCAL_LONG:
      return localInstruction("OP_GET_LOCAL_LONG", chunk, offset, true);
    case OP_SET_LOCAL:
      return localInstruction("OP_SET_LOCAL", chunk, offset, false);
    case OP_SET_LOCAL_LONG:
      return localInstruction("OP_SET_LOCAL_LONG", chunk, offset, true);
    case OP_GET_GLOBAL:
      return indexedInstruction("OP_GET_GLOBAL", chunk, offset, false);
    case OP_GET_GLOBAL_LONG:
      return indexedInstruction("OP_GET_GLOBAL_LONG", chunk, offset, true);
    case OP_DEFINE_GLOBAL:
      return indexedInstruction("OP_DEFINE_GLOBAL", chunk, offset, false);
    case OP_DEFINE_GLOBAL_LONG:
      return indexedInstruction("OP_DEFINE_GLOBAL_LONG", chunk, offset, true);
    case OP_DEFINE_GLOBAL_CONST:
      return indexedInstruction("OP_DEFINE_GLOBAL_CONST", chunk, offset, false);
    case OP_DEFINE_GLOBAL_CONST_LONG:
      return indexedInstruction("OP_DEFINE_GLOBAL_CONST_LONG", chunk, offset, true);
    case OP_SET_GLOBAL:
      return indexedInstruction("OP_SET_GLOBAL", chunk, offset, false);
    case OP_SET_GLOBAL_LONG:
      return indexedInstruction("OP_SET_GLOBAL_LONG", chunk, offset, true);
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:
      return simpleInstruction("OP_LESS", offset);
    case OP_ADD:
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:
      return simpleInstruction("OP_DIVIDE", offset);
    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);
    case OP_PRINT:
      return simpleInstruction("OP_PRINT", offset);
    case OP_JUMP:
      return jumpInstruction("OP_JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
    case OP_LOOP:
      return jumpInstruction("OP_LOOP", -1, chunk, offset);
    case OP_CALL:
      return localInstruction("OP_CALL", chunk, offset, false);
    case OP_GET_UPVALUE:
      return localInstruction("OP_GET_UPVALUE", chunk, offset, false);
    case OP_GET_UPVALUE_LONG:
      return localInstruction("OP_GET_UPVALUE_LONG", chunk, offset, true);
    case OP_SET_UPVALUE:
      return localInstruction("OP_SET_UPVALUE", chunk, offset, false);
    case OP_SET_UPVALUE_LONG:
      return localInstruction("OP_SET_UPVALUE_LONG", chunk, offset, true);
    case OP_CLOSURE:
      return closureInstruction("OP_CLOSURE", chunk, offset, false);
    case OP_CLOSURE_LONG:
      return closureInstruction("OP_CLOSURE_LONG", chunk, offset, true);
    case OP_CLOSE_UPVALUE:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset);
    case OP_CLOSE_LOCAL:
      return localInstruction("OP_CLOSE_LOCAL", chunk, offset, false);
    case OP_CLOSE_LOCAL_LONG:
      return localInstruction("OP_CLOSE_LOCAL_LONG", chunk, offset, true);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    default:
      printf("Unknown opcode %d\n", chunk->code[offset]);
      return offset + 1;
  }
}
