#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "vm.h"

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm.objects;
  vm.objects = object;
  return object;
}

ObjFunction* newFunction(void) {
  ObjFunction* function =
      (ObjFunction*)allocateObject(sizeof(ObjFunction), OBJ_FUNCTION);
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = NULL;
  initChunk(&function->chunk);
  return function;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = NULL;

  if (function->upvalueCount > 0) {
    upvalues = GROW_ARRAY(
        ObjUpvalue*, NULL, 0, function->upvalueCount);

    for (int i = 0; i < function->upvalueCount; i++) {
      upvalues[i] = NULL;
    }
  }

  ObjClosure* closure =
      (ObjClosure*)allocateObject(sizeof(ObjClosure), OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjUpvalue* newUpvalue(int slot) {
  assert(slot >= 0);

  ObjUpvalue* upvalue =
      (ObjUpvalue*)allocateObject(sizeof(ObjUpvalue), OBJ_UPVALUE);
  upvalue->slot = slot;
  upvalue->closed = NIL_VAL;
  upvalue->next = NULL;
  return upvalue;
}

ObjNative* newNative(int arity, NativeFn function) {
  ObjNative* native =
      (ObjNative*)allocateObject(sizeof(ObjNative), OBJ_NATIVE);
  native->arity = arity;
  native->function = function;
  return native;
}

static ObjString* allocateString(
    int length, const char* externalChars) {
  assert(length >= 0);

  size_t size = sizeof(ObjString);

  if (externalChars == NULL) {
    size += (size_t)length + 1;
  }

  ObjString* string = (ObjString*)allocateObject(size, OBJ_STRING);
  string->length = length;
  string->externalChars = externalChars;

  if (externalChars == NULL) {
    string->chars[length] = '\0';
  }

  return string;
}

ObjString* copyString(const char* chars, int length) {
  assert(chars != NULL);

  ObjString* string = allocateString(length, NULL);
  memcpy(string->chars, chars, (size_t)length);
  return string;
}

ObjString* borrowString(const char* chars, int length) {
  assert(chars != NULL);
  return allocateString(length, chars);
}

const char* stringChars(const ObjString* string) {
  if (string->externalChars != NULL) {
    return string->externalChars;
  }

  return string->chars;
}

ObjString* concatenateStrings(
    const ObjString* left, const ObjString* right) {
  if (left->length > INT_MAX - right->length) {
    fprintf(stderr, "String is too long.\n");
    exit(1);
  }

  int length = left->length + right->length;
  ObjString* string = allocateString(length, NULL);

  memcpy(
      string->chars,
      stringChars(left),
      (size_t)left->length);
  memcpy(
      string->chars + left->length,
      stringChars(right),
      (size_t)right->length);

  return string;
}

static void printFunction(const ObjFunction* function) {
  if (function->name == NULL) {
    fputs("<script>", stdout);
  } else {
    fputs("<fn ", stdout);
    fwrite(
        stringChars(function->name), 1,
        (size_t)function->name->length, stdout);
    putchar('>');
  }
}

void printObject(Value value) {
  switch (AS_OBJ(value)->type) {
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;

    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;

    case OBJ_UPVALUE:
      fputs("upvalue", stdout);
      break;

    case OBJ_NATIVE:
      fputs("<native fn>", stdout);
      break;

    case OBJ_STRING: {
      ObjString* string = AS_STRING(value);
      fwrite(stringChars(string), 1, (size_t)string->length, stdout);
      break;
    }
  }
}

void freeObjects(void) {
  Obj* object = vm.objects;

  while (object != NULL) {
    Obj* next = object->next;

    switch (object->type) {
      case OBJ_FUNCTION: {
        ObjFunction* function = (ObjFunction*)object;
        freeChunk(&function->chunk);
        reallocate(object, sizeof(ObjFunction), 0);
        break;
      }

      case OBJ_CLOSURE: {
        ObjClosure* closure = (ObjClosure*)object;
        FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
        reallocate(object, sizeof(ObjClosure), 0);
        break;
      }

      case OBJ_UPVALUE:
        reallocate(object, sizeof(ObjUpvalue), 0);
        break;

      case OBJ_NATIVE:
        reallocate(object, sizeof(ObjNative), 0);
        break;

      case OBJ_STRING: {
        ObjString* string = (ObjString*)object;
        size_t size = sizeof(ObjString);

        if (string->externalChars == NULL) {
          size += (size_t)string->length + 1;
        }

        reallocate(object, size, 0);
        break;
      }
    }

    object = next;
  }

  vm.objects = NULL;
}
