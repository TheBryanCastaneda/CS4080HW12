#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64

typedef struct {
  ObjFunction* function;
  ObjClosure* closure;
  uint8_t* ip;
  int slotStart;
} CallFrame;

typedef struct {
  CallFrame frames[FRAMES_MAX];
  int frameCount;
  Value* stack;
  Value* stackTop;
  int stackCapacity;
  Obj* objects;
  ObjUpvalue* openUpvalues;
  Table globals;
  Table globalConstants;
} VM;

extern VM vm;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM(void);
void freeVM(void);
InterpretResult interpretFunction(ObjFunction* function);
InterpretResult interpretSource(const char* source);
void push(Value value);
Value pop(void);

#endif
