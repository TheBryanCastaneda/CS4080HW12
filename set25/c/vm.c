#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#ifndef VM_LOCAL_IP
#define VM_LOCAL_IP 1
#endif

#ifndef VM_WRAP_ALL
#define VM_WRAP_ALL 0
#endif

VM vm;

static ObjUpvalue* captureUpvalue(int slot) {
  ObjUpvalue* previous = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;

  while (upvalue != NULL && upvalue->slot > slot) {
    previous = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->slot == slot) {
    return upvalue;
  }

  ObjUpvalue* created = newUpvalue(slot);
  created->next = upvalue;

  if (previous == NULL) {
    vm.openUpvalues = created;
  } else {
    previous->next = created;
  }

  return created;
}

static void closeUpvalues(int firstSlot) {
  while (vm.openUpvalues != NULL &&
      vm.openUpvalues->slot >= firstSlot) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = vm.stack[upvalue->slot];
    upvalue->slot = -1;
    vm.openUpvalues = upvalue->next;
    upvalue->next = NULL;
  }
}

static void resetStack(void) {
  closeUpvalues(0);
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
}

void push(Value value) {
  int count = vm.stack == NULL
      ? 0
      : (int)(vm.stackTop - vm.stack);

  if (vm.stackCapacity < count + 1) {
    int oldCapacity = vm.stackCapacity;
    vm.stackCapacity = GROW_CAPACITY(oldCapacity);
    vm.stack = GROW_ARRAY(
        Value, vm.stack, oldCapacity, vm.stackCapacity);
    vm.stackTop = vm.stack + count;
  }

  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop(void) {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

static bool clockNative(
    int argCount, const Value* args, Value* result, const char** error) {
  (void)argCount;
  (void)args;
  (void)error;
  *result = NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
  return true;
}

static bool sqrtNative(
    int argCount, const Value* args, Value* result, const char** error) {
  (void)argCount;

  if (!IS_NUMBER(args[0])) {
    *error = "sqrt needs a number.";
    return false;
  }

  double number = AS_NUMBER(args[0]);

  if (number < 0) {
    *error = "sqrt needs a number that is zero or more.";
    return false;
  }

  *result = NUMBER_VAL(sqrt(number));
  return true;
}

static bool absNative(
    int argCount, const Value* args, Value* result, const char** error) {
  (void)argCount;

  if (!IS_NUMBER(args[0])) {
    *error = "abs needs a number.";
    return false;
  }

  *result = NUMBER_VAL(fabs(AS_NUMBER(args[0])));
  return true;
}

static bool lenNative(
    int argCount, const Value* args, Value* result, const char** error) {
  (void)argCount;

  if (!IS_STRING(args[0])) {
    *error = "len needs a string.";
    return false;
  }

  *result = NUMBER_VAL(AS_STRING(args[0])->length);
  return true;
}

static void defineNative(const char* name, int arity, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(arity, function)));
  tableSet(&vm.globals, peek(1), peek(0));
  pop();
  pop();
}

void initVM(void) {
  vm.stack = NULL;
  vm.stackCapacity = 0;
  vm.objects = NULL;
  vm.openUpvalues = NULL;
  initTable(&vm.globals);
  initTable(&vm.globalConstants);
  resetStack();
  defineNative("clock", 0, clockNative);
  defineNative("sqrt", 1, sqrtNative);
  defineNative("abs", 1, absNative);
  defineNative("len", 1, lenNative);
}

void freeVM(void) {
  resetStack();
  freeTable(&vm.globals);
  freeTable(&vm.globalConstants);
  freeObjects();
  FREE_ARRAY(Value, vm.stack, vm.stackCapacity);
  vm.stack = NULL;
  vm.stackTop = NULL;
  vm.stackCapacity = 0;
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void runtimeError(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fprintf(stderr, "\n");

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->function;
    int instruction = (int)(frame->ip - function->chunk.code) - 1;
    fprintf(stderr, "[line %d] in ", getLine(&function->chunk, instruction));

    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(
          stderr, "%.*s()\n", function->name->length,
          stringChars(function->name));
    }
  }

  resetStack();
}

static bool callFunction(
    ObjFunction* function, ObjClosure* closure, int argCount) {
  if (argCount != function->arity) {
    runtimeError(
        "Expected %d arguments but got %d.", function->arity, argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->function = function;
  frame->closure = closure;
  frame->ip = function->chunk.code;
  frame->slotStart = (int)(vm.stackTop - vm.stack) - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_CLOSURE(callee)) {
    ObjClosure* closure = AS_CLOSURE(callee);
    return callFunction(closure->function, closure, argCount);
  }

  if (IS_FUNCTION(callee)) {
    return callFunction(AS_FUNCTION(callee), NULL, argCount);
  }

  if (IS_NATIVE(callee)) {
    ObjNative* native = AS_NATIVE(callee);

    if (argCount != native->arity) {
      runtimeError(
          "Expected %d arguments but got %d.", native->arity, argCount);
      return false;
    }

    Value result = NIL_VAL;
    const char* error = NULL;

    if (!native->function(
        argCount, vm.stackTop - argCount, &result, &error)) {
      runtimeError("%s", error != NULL ? error : "Native function failed.");
      return false;
    }

    vm.stackTop -= argCount + 1;
    push(result);
    return true;
  }

  runtimeError("Can only call functions.");
  return false;
}

static bool getGlobal(Value name, Value* value) {
  if (tableGet(&vm.globals, name, value)) {
    return true;
  }

  ObjString* string = AS_STRING(name);
  runtimeError(
      "Undefined variable '%.*s'.",
      string->length,
      stringChars(string));

  return false;
}

static bool checkMutableGlobal(Value name) {
  Value ignored;

  if (!tableGet(&vm.globalConstants, name, &ignored)) {
    return true;
  }

  ObjString* string = AS_STRING(name);
  runtimeError(
      "Can't modify constant '%.*s'.",
      string->length,
      stringChars(string));

  return false;
}

static InterpretResult run(void) {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];

#if VM_LOCAL_IP
  register uint8_t* ip = frame->ip;
#define IP ip
#define SAVE_IP() (frame->ip = ip)
#define LOAD_IP() (ip = frame->ip)
#else
#define IP frame->ip
#define SAVE_IP() ((void)0)
#define LOAD_IP() ((void)0)
#endif

#define READ_BYTE() (*IP++)
#define READ_INDEX(isLong) \
    ((isLong) \
        ? (IP += 3, \
            ((uint32_t)IP[-3] << 16) | \
            ((uint32_t)IP[-2] << 8) | (uint32_t)IP[-1]) \
        : (uint32_t)READ_BYTE())
#define READ_SHORT() \
    (IP += 2, (uint16_t)(((uint16_t)IP[-2] << 8) | IP[-1]))
#define FAIL(...) \
    do { \
      SAVE_IP(); \
      runtimeError(__VA_ARGS__); \
      return INTERPRET_RUNTIME_ERROR; \
    } while (false)

#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        FAIL("Operands must be numbers."); \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  for (;;) {
    uint8_t instruction = READ_BYTE();

    switch (instruction) {
      case OP_CONSTANT:
      case OP_CONSTANT_LONG: {
        uint32_t index = READ_INDEX(instruction == OP_CONSTANT_LONG);
        push(frame->function->chunk.constants.values[index]);
        break;
      }

      case OP_NIL:
        push(NIL_VAL);
        break;

      case OP_TRUE:
        push(BOOL_VAL(true));
        break;

      case OP_FALSE:
        push(BOOL_VAL(false));
        break;

      case OP_POP:
        pop();
        break;

      case OP_GET_LOCAL:
      case OP_GET_LOCAL_LONG: {
        uint32_t slot = READ_INDEX(instruction == OP_GET_LOCAL_LONG);
        push(vm.stack[frame->slotStart + slot]);
        break;
      }

      case OP_SET_LOCAL:
      case OP_SET_LOCAL_LONG: {
        uint32_t slot = READ_INDEX(instruction == OP_SET_LOCAL_LONG);
        vm.stack[frame->slotStart + slot] = peek(0);
        break;
      }

      case OP_GET_UPVALUE:
      case OP_GET_UPVALUE_LONG: {
        uint32_t index = READ_INDEX(instruction == OP_GET_UPVALUE_LONG);
        ObjUpvalue* upvalue = frame->closure->upvalues[index];
        Value value = upvalue->slot >= 0
            ? vm.stack[upvalue->slot]
            : upvalue->closed;
        push(value);
        break;
      }

      case OP_SET_UPVALUE:
      case OP_SET_UPVALUE_LONG: {
        uint32_t index = READ_INDEX(instruction == OP_SET_UPVALUE_LONG);
        ObjUpvalue* upvalue = frame->closure->upvalues[index];

        if (upvalue->slot >= 0) {
          vm.stack[upvalue->slot] = peek(0);
        } else {
          upvalue->closed = peek(0);
        }

        break;
      }

      case OP_GET_GLOBAL:
      case OP_GET_GLOBAL_LONG: {
        uint32_t index = READ_INDEX(instruction == OP_GET_GLOBAL_LONG);
        Value name = frame->function->chunk.constants.values[index];
        Value value;

        SAVE_IP();

        if (!getGlobal(name, &value)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        push(value);
        break;
      }

      case OP_DEFINE_GLOBAL:
      case OP_DEFINE_GLOBAL_LONG:
      case OP_DEFINE_GLOBAL_CONST:
      case OP_DEFINE_GLOBAL_CONST_LONG: {
        bool isConstant =
            instruction == OP_DEFINE_GLOBAL_CONST ||
            instruction == OP_DEFINE_GLOBAL_CONST_LONG;

        bool isLong =
            instruction == OP_DEFINE_GLOBAL_LONG ||
            instruction == OP_DEFINE_GLOBAL_CONST_LONG;

        uint32_t index = READ_INDEX(isLong);
        Value name = frame->function->chunk.constants.values[index];

        SAVE_IP();

        if (!checkMutableGlobal(name)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        tableSet(&vm.globals, name, peek(0));

        if (isConstant) {
          tableSet(&vm.globalConstants, name, BOOL_VAL(true));
        }

        pop();
        break;
      }

      case OP_SET_GLOBAL:
      case OP_SET_GLOBAL_LONG: {
        uint32_t index = READ_INDEX(instruction == OP_SET_GLOBAL_LONG);
        Value name = frame->function->chunk.constants.values[index];
        Value existing;

        SAVE_IP();

        if (!getGlobal(name, &existing)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        if (!checkMutableGlobal(name)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        tableSet(&vm.globals, name, peek(0));
        break;
      }

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }

      case OP_GREATER:
        BINARY_OP(BOOL_VAL, >);
        break;

      case OP_LESS:
        BINARY_OP(BOOL_VAL, <);
        break;

      case OP_ADD:
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          ObjString* right = AS_STRING(pop());
          ObjString* left = AS_STRING(pop());
          push(OBJ_VAL(concatenateStrings(left, right)));
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          FAIL("Operands must be two numbers or two strings.");
        }
        break;

      case OP_SUBTRACT:
        BINARY_OP(NUMBER_VAL, -);
        break;

      case OP_MULTIPLY:
        BINARY_OP(NUMBER_VAL, *);
        break;

      case OP_DIVIDE:
        BINARY_OP(NUMBER_VAL, /);
        break;

      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;

      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          FAIL("Operand must be a number.");
        }

        vm.stackTop[-1] = NUMBER_VAL(-AS_NUMBER(vm.stackTop[-1]));
        break;

      case OP_PRINT:
        printValue(pop());
        printf("\n");
        break;

      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        IP += offset;
        break;
      }

      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();

        if (isFalsey(peek(0))) {
          IP += offset;
        }

        break;
      }

      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        IP -= offset;
        break;
      }

      case OP_CALL: {
        int argCount = READ_BYTE();
        SAVE_IP();

        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        frame = &vm.frames[vm.frameCount - 1];
        LOAD_IP();
        break;
      }

      case OP_CLOSURE:
      case OP_CLOSURE_LONG: {
        uint32_t index = READ_INDEX(instruction == OP_CLOSURE_LONG);
        ObjFunction* function =
            AS_FUNCTION(frame->function->chunk.constants.values[index]);

        if (!VM_WRAP_ALL && function->upvalueCount == 0) {
          push(OBJ_VAL(function));
          break;
        }

        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));

        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t flags = READ_BYTE();
          uint32_t captured = READ_INDEX((flags & 2) != 0);

          if ((flags & 1) != 0) {
            closure->upvalues[i] =
                captureUpvalue(frame->slotStart + (int)captured);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[captured];
          }
        }

        break;
      }

      case OP_CLOSE_UPVALUE:
        closeUpvalues((int)(vm.stackTop - vm.stack) - 1);
        pop();
        break;

      case OP_CLOSE_LOCAL:
      case OP_CLOSE_LOCAL_LONG: {
        uint32_t slot = READ_INDEX(instruction == OP_CLOSE_LOCAL_LONG);
        closeUpvalues(frame->slotStart + (int)slot);
        break;
      }

      case OP_RETURN: {
        Value result = pop();
        int slotStart = frame->slotStart;
        closeUpvalues(slotStart);
        vm.frameCount--;
        vm.stackTop = vm.stack + slotStart;

        if (vm.frameCount == 0) {
          resetStack();
          return INTERPRET_OK;
        }

        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        LOAD_IP();
        break;
      }

      default:
        FAIL("Unknown opcode.");
    }
  }

#undef BINARY_OP
#undef FAIL
#undef READ_SHORT
#undef READ_INDEX
#undef READ_BYTE
#undef LOAD_IP
#undef SAVE_IP
#undef IP
}

InterpretResult interpretFunction(ObjFunction* function) {
  resetStack();
  push(OBJ_VAL(function));

  ObjClosure* closure = NULL;

  if (VM_WRAP_ALL || function->upvalueCount > 0) {
    closure = newClosure(function);
    vm.stackTop[-1] = OBJ_VAL(closure);
  }

  if (!callFunction(function, closure, 0)) {
    return INTERPRET_RUNTIME_ERROR;
  }

  return run();
}

InterpretResult interpretSource(const char* source) {
  ObjFunction* function = compile(source);

  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }

  return interpretFunction(function);
}
