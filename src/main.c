#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  while(1) {
    printf("$ ");

    char input[1024];
    fgets(input, sizeof(input), stdin);

    input[strcspn(input, NEWLINE)] = '\0';

    if (strcmp(input, "exit") == 0) {
      break;
    } else if (strncmp(input, "echo", 5) == 0) {
      printf("%s\n", input + 5);
    } else {
      printf("%s: command not found\n", input);
    }
  }

  return 0;
}
