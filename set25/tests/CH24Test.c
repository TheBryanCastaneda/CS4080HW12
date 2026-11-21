#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../c/object.h"
#include "../c/vm.h"

static Value global(const char* name) {
  Value value = NIL_VAL;
  ObjString* key = copyString(name, (int)strlen(name));
  assert(tableGet(&vm.globals, OBJ_VAL(key), &value));
  return value;
}

static void number(const char* name, double expected) {
  Value value = global(name);
  assert(IS_NUMBER(value));
  assert(AS_NUMBER(value) == expected);
}

static void ok(const char* source) {
  assert(interpretSource(source) == INTERPRET_OK);
  assert(vm.stackTop == vm.stack);
  assert(vm.frameCount == 0);
}

static void bad(const char* source, InterpretResult expected) {
  assert(interpretSource(source) == expected);
  assert(vm.stackTop == vm.stack);
  assert(vm.frameCount == 0);

  ok("var recovered = 123;");
  number("recovered", 123);
}

int main(void) {
  initVM();

  ok(
      "var out = 0;"
      "fun add(a, b) { return a + b; }"
      "out = 4 + add(5, 6);");
  number("out", 15);

  ok(
      "var alias = add;"
      "out = alias(2, 3);"
      "var same = alias == add;");
  number("out", 5);
  Value same = global("same");
  assert(IS_BOOL(same) && AS_BOOL(same));

  ok(
      "fun fib(n) {"
      "  if (n < 2) return n;"
      "  return fib(n - 1) + fib(n - 2);"
      "}"
      "out = fib(10);");
  number("out", 55);

  ok(
      "fun deep(n) {"
      "  var hold = n;"
      "  if (n == 0) return 0;"
      "  var answer = deep(n - 1);"
      "  return hold + answer;"
      "}"
      "out = deep(40);");
  number("out", 820);

  ok(
      "fun bare() { return; }"
      "fun implicit() { var a = 3; }"
      "var one = bare();"
      "var two = implicit();");
  assert(IS_NIL(global("one")));
  assert(IS_NIL(global("two")));

  ok(
      "fun identity(x) { return x; }"
      "out = identity(identity)(9);");
  number("out", 9);

  ok(
      "fun factory() {"
      "  fun inner(x) { return x * 2; }"
      "  return inner;"
      "}"
      "var twice = factory();"
      "out = twice(7);");
  number("out", 14);

  ok(
      "const fixed = 12;"
      "fun constants(x) {"
      "  const bonus = 3;"
      "  return fixed + x + bonus;"
      "}"
      "out = constants(5);");
  number("out", 20);

  ok(
      "fun looping() {"
      "  var sum = 0;"
      "  for (var i = 0; i < 6; i = i + 1) {"
      "    const step = i;"
      "    switch (i) {"
      "      case 2: continue;"
      "      default: sum = sum + step;"
      "    }"
      "  }"
      "  return sum;"
      "}"
      "out = looping();");
  number("out", 13);

  ok(
      "out = sqrt(81) + abs(-7) + len(\"hello\");"
      "var measured = clock();");
  number("out", 21);
  assert(IS_NUMBER(global("measured")));

  puts("Starting intentional error tests.");
  fflush(stdout);

  bad("clock(1);", INTERPRET_RUNTIME_ERROR);
  bad("sqrt();", INTERPRET_RUNTIME_ERROR);
  bad("sqrt(1, 2);", INTERPRET_RUNTIME_ERROR);
  bad("abs();", INTERPRET_RUNTIME_ERROR);
  bad("abs(1, 2);", INTERPRET_RUNTIME_ERROR);
  bad("len();", INTERPRET_RUNTIME_ERROR);
  bad("len(\"a\", \"b\");", INTERPRET_RUNTIME_ERROR);

  bad("sqrt(\"bad\");", INTERPRET_RUNTIME_ERROR);
  bad("sqrt(-1);", INTERPRET_RUNTIME_ERROR);
  bad("abs(nil);", INTERPRET_RUNTIME_ERROR);
  bad("len(12);", INTERPRET_RUNTIME_ERROR);

  bad("add(1);", INTERPRET_RUNTIME_ERROR);
  bad("add(1, 2, 3);", INTERPRET_RUNTIME_ERROR);
  bad("var n = 1; n();", INTERPRET_RUNTIME_ERROR);

  bad("return 1;", INTERPRET_COMPILE_ERROR);
  bad("fixed = 4;", INTERPRET_COMPILE_ERROR);
  bad(
      "fun broken() { const local = 3; local = 4; }",
      INTERPRET_COMPILE_ERROR);
  bad(
      "while (false) { fun brokenLoop() { continue; } }",
      INTERPRET_COMPILE_ERROR);

  bad(
      "fun errorLeaf() { sqrt(\"bad\"); }"
      "fun errorParent() { errorLeaf(); }"
      "errorParent();",
      INTERPRET_RUNTIME_ERROR);

  bad(
      "fun endless() { endless(); } endless();",
      INTERPRET_RUNTIME_ERROR);

  ok("out = add(10, 20);");
  number("out", 30);

  char source[30000];
  int used = snprintf(source, sizeof(source), "fun wide() {");

  for (int i = 0; i < 300; i++) {
    used += snprintf(
        source + used, sizeof(source) - (size_t)used,
        "var v%d = %d;", i, i);
  }

  snprintf(
      source + used, sizeof(source) - (size_t)used,
      "var nested = deep(5);"
      "v299 = v299 + nested;"
      "return v299;"
      "} out = wide();");

  ok(source);
  number("out", 314);

  used = snprintf(source, sizeof(source), "fun globalsWide() {");

  for (int i = 0; i < 300; i++) {
    used += snprintf(
        source + used, sizeof(source) - (size_t)used,
        "out = %d;", i);
  }

  snprintf(
      source + used, sizeof(source) - (size_t)used,
      "return out; } out = globalsWide();");

  ok(source);
  number("out", 299);

  freeVM();
  puts("Chapter 24 tests passed.");
  return 0;
}
