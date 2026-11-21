#include <stdio.h>
#include <stdlib.h>

#include "vm.h"

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");

  if (file == NULL) {
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }

  long size = ftell(file);

  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  char* source = malloc((size_t)size + 1);

  if (source == NULL) {
    fclose(file);
    return NULL;
  }

  size_t bytesRead = fread(source, 1, (size_t)size, file);
  fclose(file);

  if (bytesRead != (size_t)size) {
    free(source);
    return NULL;
  }

  source[bytesRead] = '\0';
  return source;
}

static void repl(void) {
  char line[1024];

  for (;;) {
    printf("> ");

    if (fgets(line, sizeof(line), stdin) == NULL) {
      printf("\n");
      break;
    }

    interpretSource(line);
  }
}

int main(int argc, const char* argv[]) {
  initVM();
  int exitCode = 0;

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    char* source = readFile(argv[1]);

    if (source == NULL) {
      fprintf(stderr, "Couldnt read ze file '%s'.\n", argv[1]);
      exitCode = 74;
    } else {
      InterpretResult result = interpretSource(source);
      free(source);

      if (result == INTERPRET_COMPILE_ERROR) {
        exitCode = 65;
      } else if (result == INTERPRET_RUNTIME_ERROR) {
        exitCode = 70;
      }
    }
  } else {
    fprintf(stderr, "Usage: clox [path]\n");
    exitCode = 64;
  }

  freeVM();
  return exitCode;
}
