#include <assert.h>
#include <stdio.h>
#include <time.h>

#include "../c/compiler.h"
#include "../c/object.h"
#include "../c/vm.h"

#ifndef VM_WRAP_ALL
#define VM_WRAP_ALL 0
#endif

#ifndef VM_LOCAL_IP
#define VM_LOCAL_IP 1
#endif

#define RUNS 5

static double sample(const char* source, double expected) {
  initVM();

  ObjFunction* function = compile(source);
  assert(function != NULL);

  clock_t start = clock();
  InterpretResult status = interpretFunction(function);
  clock_t finish = clock();

  assert(start != (clock_t)-1);
  assert(finish != (clock_t)-1);
  assert(status == INTERPRET_OK);
  assert(vm.frameCount == 0);
  assert(vm.stackTop == vm.stack);
  assert(vm.openUpvalues == NULL);

  Value result = NIL_VAL;
  ObjString* key = copyString("result", 6);
  assert(tableGet(&vm.globals, OBJ_VAL(key), &result));
  assert(IS_NUMBER(result));
  assert(AS_NUMBER(result) == expected);

  double elapsed = (double)(finish - start) / CLOCKS_PER_SEC;

  freeVM();
  return elapsed;
}

static void benchmark(
    const char* name, const char* source, double expected) {
  double times[RUNS];

  sample(source, expected);

  for (int i = 0; i < RUNS; i++) {
    times[i] = sample(source, expected);

    printf(
        "%s run %d: %.6f seconds; checksum: %.0f\n",
        name, i + 1, times[i], expected);
  }

  for (int i = 1; i < RUNS; i++) {
    double value = times[i];
    int j = i - 1;

    while (j >= 0 && times[j] > value) {
      times[j + 1] = times[j];
      j--;
    }

    times[j + 1] = value;
  }

  printf("%s median: %.6f seconds\n\n", name, times[RUNS / 2]);
}

int main(void) {
  const char* plainCalls =
      "fun step(x) { return x * 3 + 7; }"
      "fun work(f, n) {"
      "  var total = 0;"
      "  for (var i = 0; i < n; i = i + 1) {"
      "    total = total + f(i);"
      "  }"
      "  return total;"
      "}"
      "var result = work(step, 2000000);";

  const char* capturedCalls =
      "fun makeStep(offset) {"
      "  fun step(x) { return x * 3 + offset; }"
      "  return step;"
      "}"
      "var step = makeStep(7);"
      "fun work(f, n) {"
      "  var total = 0;"
      "  for (var i = 0; i < n; i = i + 1) {"
      "    total = total + f(i);"
      "  }"
      "  return total;"
      "}"
      "var result = work(step, 2000000);";

  const char* plainCreation =
      "fun factory(base) {"
      "  fun step(x) { return x + 1; }"
      "  return step;"
      "}"
      "fun work(make, n) {"
      "  var total = 0;"
      "  for (var i = 0; i < n; i = i + 1) {"
      "    var f = make(1);"
      "    total = total + f(i);"
      "  }"
      "  return total;"
      "}"
      "var result = work(factory, 500000);";

  const char* capturedCreation =
      "fun factory(base) {"
      "  fun step(x) { return x + base; }"
      "  return step;"
      "}"
      "fun work(make, n) {"
      "  var total = 0;"
      "  for (var i = 0; i < n; i = i + 1) {"
      "    var f = make(1);"
      "    total = total + f(i);"
      "  }"
      "  return total;"
      "}"
      "var result = work(factory, 500000);";

  double callCount = 2000000.0;
  double callExpected =
      3.0 * callCount * (callCount - 1.0) / 2.0 + 7.0 * callCount;

  double creationCount = 500000.0;
  double creationExpected =
      creationCount * (creationCount + 1.0) / 2.0;

  printf(
      "VM_WRAP_ALL=%d; VM_LOCAL_IP=%d\n\n",
      VM_WRAP_ALL, VM_LOCAL_IP);

  benchmark("plain calls", plainCalls, callExpected);
  benchmark("captured calls", capturedCalls, callExpected);
  benchmark("plain creation", plainCreation, creationExpected);
  benchmark("captured creation", capturedCreation, creationExpected);

  return 0;
}
