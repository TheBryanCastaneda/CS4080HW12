#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value) isObjType(value, OBJ_NATIVE)
#define IS_STRING(value) isObjType(value, OBJ_STRING)

#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value) ((ObjNative*)AS_OBJ(value))
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))

typedef enum {
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_CLOSURE,
  OBJ_UPVALUE
} ObjType;

struct Obj {
  ObjType type;
  Obj* next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name;
} ObjFunction;

typedef struct ObjUpvalue {
  Obj obj;
  int slot;
  Value closed;
  struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;

typedef bool (*NativeFn)(
    int argCount, const Value* args, Value* result, const char** error);

typedef struct {
  Obj obj;
  int arity;
  NativeFn function;
} ObjNative;

struct ObjString {
  Obj obj;
  int length;
  const char* externalChars;
  char chars[];
};

ObjFunction* newFunction(void);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(int slot);
ObjNative* newNative(int arity, NativeFn function);
ObjString* copyString(const char* chars, int length);
ObjString* borrowString(const char* chars, int length);
ObjString* concatenateStrings(
    const ObjString* left, const ObjString* right);
const char* stringChars(const ObjString* string);
void printObject(Value value);
void freeObjects(void);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
