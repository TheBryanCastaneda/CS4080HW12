#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../c/object.h"
#include "../c/vm.h"

#ifndef VM_WRAP_ALL
#define VM_WRAP_ALL 0
#endif

#ifndef VM_LOCAL_IP
#define VM_LOCAL_IP 1
#endif

static Value globalValue(const char* name) {
  Value value = NIL_VAL;
  ObjString* key = copyString(name, (int)strlen(name));
  assert(tableGet(&vm.globals, OBJ_VAL(key), &value));
  return value;
}

static void number(const char* name, double expected) {
  Value value = globalValue(name);
  assert(IS_NUMBER(value));
  assert(AS_NUMBER(value) == expected);
}

static void ok(const char* source) {
  assert(interpretSource(source) == INTERPRET_OK);
  assert(vm.frameCount == 0);
  assert(vm.stackTop == vm.stack);
  assert(vm.openUpvalues == NULL);
}

static void bad(const char* source, InterpretResult expected) {
  assert(interpretSource(source) == expected);
  assert(vm.frameCount == 0);
  assert(vm.stackTop == vm.stack);
  assert(vm.openUpvalues == NULL);

  ok("var recovered25 = 123;");
  number("recovered25", 123);
}

static void append(
    char* source, size_t capacity, size_t* used,
    const char* format, ...) {
  assert(*used < capacity);

  va_list args;
  va_start(args, format);
  int written = vsnprintf(
      source + *used, capacity - *used, format, args);
  va_end(args);

  assert(written >= 0);
  assert((size_t)written < capacity - *used);
  *used += (size_t)written;
}

int main(void) {
  initVM();

  ok(
      "var out25 = 0;"
      "fun plain25(n) { return n + 1; }"
      "out25 = plain25(4);");
  number("out25", 5);

  Value plain = globalValue("plain25");
  assert(VM_WRAP_ALL ? IS_CLOSURE(plain) : IS_FUNCTION(plain));

  ok(
      "fun counter25(n) {"
      "  fun next() { n = n + 1; return n; }"
      "  return next;"
      "}"
      "var counterA25 = counter25(0);"
      "var counterB25 = counter25(10);"
      "var counterAlias25 = counterA25;"
      "var resultA25 = counterA25();"
      "var resultB25 = counterB25();"
      "var resultC25 = counterAlias25();");
  number("resultA25", 1);
  number("resultB25", 11);
  number("resultC25", 2);

  Value counterA = globalValue("counterA25");
  Value counterB = globalValue("counterB25");
  Value counterAlias = globalValue("counterAlias25");
  assert(IS_CLOSURE(counterA));
  assert(valuesEqual(counterA, counterAlias));
  assert(!valuesEqual(counterA, counterB));

  ok(
      "var getter25;"
      "var setter25;"
      "fun shared25() {"
      "  var value = 10;"
      "  fun get() { return value; }"
      "  fun set(n) { value = n; }"
      "  getter25 = get;"
      "  setter25 = set;"
      "}"
      "shared25();"
      "setter25(42);"
      "out25 = getter25();");
  number("out25", 42);

  ok(
      "fun live25() {"
      "  var value = 1;"
      "  fun change() { value = value + 2; }"
      "  change();"
      "  return value;"
      "}"
      "out25 = live25();");
  number("out25", 3);

  ok(
      "var blockA25;"
      "var blockB25;"
      "{"
      "  var value = 10;"
      "  fun get() { return value; }"
      "  blockA25 = get;"
      "}"
      "{"
      "  var value = 20;"
      "  fun get() { return value; }"
      "  blockB25 = get;"
      "}"
      "out25 = blockA25() * 100 + blockB25();");
  number("out25", 1020);

  ok(
      "fun factorialMaker25() {"
      "  fun fact(n) {"
      "    if (n < 2) return 1;"
      "    return n * fact(n - 1);"
      "  }"
      "  return fact;"
      "}"
      "var factorial25 = factorialMaker25();"
      "out25 = factorial25(6);");
  number("out25", 720);

  ok(
      "const frozen25 = 5;"
      "fun shadow25() {"
      "  var frozen25 = 6;"
      "  fun next() {"
      "    frozen25 = frozen25 + 1;"
      "    return frozen25;"
      "  }"
      "  return next;"
      "}"
      "var unlocked25 = shadow25();"
      "out25 = unlocked25();");
  number("out25", 7);

  ok(
      "var firstLoop25;"
      "var lastLoop25;"
      "fun captureLoop25() {"
      "  for (var i = 0; i < 3; i = i + 1) {"
      "    switch (i) {"
      "      case 0:"
      "        var value = 10;"
      "        fun get() { return i + value; }"
      "        firstLoop25 = get;"
      "        continue;"
      "      default:"
      "        var value = 20;"
      "        fun get() { return i + value; }"
      "        lastLoop25 = get;"
      "    }"
      "  }"
      "}"
      "captureLoop25();"
      "out25 = firstLoop25() * 100 + lastLoop25();");
  number("out25", 1022);

  ok(
      "var early25;"
      "var late25;"
      "for (var i = 0; i < 2; i = i + 1) {"
      "  for (var j = 0; j < 2; j = j + 1) {"
      "    fun get() { return i * 10 + j; }"
      "    if (i == 0 and j == 1) early25 = get;"
      "    late25 = get;"
      "  }"
      "}"
      "out25 = early25() * 100 + late25();");
  number("out25", 111);

  char source[100000];
  size_t used = 0;
  int oldCapacity = vm.stackCapacity;

  append(source, sizeof(source), &used, "fun grow25() {");

  for (int i = 0; i < 700; i++) {
    append(source, sizeof(source), &used, "var g%d = %d;", i, i);
  }

  append(
      source, sizeof(source), &used,
      "return g699; } fun wide25() {");

  for (int i = 0; i < 300; i++) {
    append(source, sizeof(source), &used, "var v%d = %d;", i, i);
  }

  append(
      source, sizeof(source), &used,
      "fun beforeGrowth() { return v299; }"
      "var amount = grow25();"
      "v299 = v299 + amount;"
      "if (beforeGrowth() != v299) return -1;"
      "var chosen = nil;"
      "for (var i = 0; i < 2; i = i + 1) {"
      "  fun read() { return v299 + i; }"
      "  if (i == 0) chosen = read;"
      "}"
      "return chosen;"
      "}"
      "var wideSaved25 = wide25();"
      "out25 = wideSaved25();");

  ok(source);
  number("out25", 998);
  assert(vm.stackCapacity > oldCapacity);

  used = 0;
  append(source, sizeof(source), &used, "fun manyFactory25() {");

  for (int i = 0; i < 300; i++) {
    append(source, sizeof(source), &used, "var x%d = %d;", i, i);
  }

  append(
      source, sizeof(source), &used,
      "fun middle() {"
      "  fun inner() {"
      "    var total = 0;");

  for (int i = 0; i < 300; i++) {
    append(source, sizeof(source), &used, "total = total + x%d;", i);
  }

  append(
      source, sizeof(source), &used,
      "x299 = x299 + 1;"
      "return total + x299;"
      "}"
      "return inner;"
      "}"
      "return middle;"
      "}"
      "var manyMiddle25 = manyFactory25();"
      "var manySaved25 = manyMiddle25();"
      "out25 = manySaved25();");

  ok(source);
  number("out25", 45150);

  Value many = globalValue("manySaved25");
  assert(IS_CLOSURE(many));
  assert(AS_CLOSURE(many)->upvalueCount == 300);

  ok("out25 = manySaved25();");
  number("out25", 45152);

  bad(
      "fun invalidConst25() {"
      "  const value = 1;"
      "  fun inner() { value = 2; }"
      "  return inner;"
      "}",
      INTERPRET_COMPILE_ERROR);

  bad(
      "fun invalidDeepConst25() {"
      "  const value = 1;"
      "  fun middle() {"
      "    fun inner() { value = 2; }"
      "    return inner;"
      "  }"
      "  return middle;"
      "}",
      INTERPRET_COMPILE_ERROR);

  bad(
      "const poison25 = 3; return 1;",
      INTERPRET_COMPILE_ERROR);

  ok("var poison25 = 4;");
  number("poison25", 4);

  bad("counterA25(1);", INTERPRET_RUNTIME_ERROR);

  ok(
      "var rescued25;"
      "fun fail25() {"
      "  var value = 73;"
      "  fun saved() { value = value + 1; return value; }"
      "  rescued25 = saved;"
      "  sqrt(\"bad\");"
      "}");

  bad("fail25();", INTERPRET_RUNTIME_ERROR);
  ok("out25 = rescued25();");
  number("out25", 74);

  ok("var rescuedBlock25;");

  bad(
      "{"
      "  var value = 88;"
      "  fun saved() { return value; }"
      "  rescuedBlock25 = saved;"
      "  len(2);"
      "}",
      INTERPRET_RUNTIME_ERROR);

  ok("out25 = rescuedBlock25();");
  number("out25", 88);

  ok(
      "var rescuedRecursion25;"
      "fun explode25(n) {"
      "  var value = n;"
      "  fun saved() { return value; }"
      "  if (n == 0) rescuedRecursion25 = saved;"
      "  explode25(n + 1);"
      "}");

  bad("explode25(0);", INTERPRET_RUNTIME_ERROR);
  ok("out25 = rescuedRecursion25();");
  number("out25", 0);

  freeVM();

  printf(
      "CH25 closure tests passed (wrap=%d, local IP=%d).\n",
      VM_WRAP_ALL, VM_LOCAL_IP);

  return 0;
}
