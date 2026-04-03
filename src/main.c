#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, nullptr);

  while(1) {
    printf("$ ");

    char input[1024];
    char *res = fgets(input, sizeof input, stdin);
    if(res == NULL) {
      if (feof(stdin)) {
        printf("Exiting shell\n");
        return 0;
      }

      perror("fgets");
      return 1;
    }

    const size_t newline_index = strcspn(input, NEWLINE);
    input[newline_index] = '\0';

    if (strcmp(input, "exit") == 0) {
      break;
    } else if (strncmp(input, "echo ", 5) == 0) {
      printf("%s" NEWLINE, input + 5);
    } else if (strncmp(input, "type ", 5) == 0) {
      if (strcmp(input + 5, "echo") == 0) {
        printf("%s is a shell builtin" NEWLINE, input + 5);
      } else if (strcmp(input + 5, "exit") == 0) {
        printf("%s is a shell builtin" NEWLINE, input + 5);
      } else if (strcmp(input + 5, "type") == 0) {
        printf("%s is a shell builtin" NEWLINE, input + 5);
      } else {
        printf("%s: not found" NEWLINE, input + 5);
      }
    } else {
      printf("%s: not found" NEWLINE, input);
    }
  }

  return 0;
}
