#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLINE "\n"

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  while(1) {
    printf("$ ");

    char command[1024];
    fgets(command, sizeof(command), stdin);

    if (strcmp("exit", command) == 0) {
      break;
    }

    command[strcspn(command, NEWLINE)] = '\0';
    printf("%s: command not found\n", command);
  }

  return 0;
}
