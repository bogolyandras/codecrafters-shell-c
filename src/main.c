#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vector.h"

#define NEWLINE "\n"

int main(int argc, char *argv[]) {

  // Flush after every printf
  setbuf(stdout, NULL);

  Vector tokens;
  vector_init(&tokens, sizeof(char *));

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
    const size_t input_length = strlen(input);

    if (newline_index == input_length) {
      fprintf(stderr, "failed to find newline");
      return 1;
    }
    input[newline_index] = '\0';
    vector_clear(&tokens);

    char *token = strtok(input, " ");
    if (token != NULL) {
      vector_push(&tokens, token);
    }
    while (token != NULL) {
      token = strtok(NULL, " ");
      if (token != NULL) {
        vector_push(&tokens, token);
      }
    }

    if (tokens.size == 0) {
      printf("nothing found, continuing" NEWLINE);
      continue;
    }

    char *first_parameter = vector_get(&tokens, 0);

    if (strcmp(first_parameter, "exit") == 0) {
      break;
    }

    if (strcmp(first_parameter, "echo") == 0) {
      for (size_t i = 1; i < tokens.size; i++) {
        printf("%s ", (char *)vector_get(&tokens, i));
      }
      printf(NEWLINE);
    } else if (strcmp(first_parameter, "type") == 0) {
      if (tokens.size < 2) {
        printf("type: missing operand" NEWLINE);
        continue;
      }

      char *second_parameter = vector_get(&tokens, 1);
      if (strcmp(second_parameter, "echo") == 0) {
        printf("%s is a shell builtin" NEWLINE, second_parameter);
      } else if (strcmp(second_parameter, "exit") == 0) {
        printf("%s is a shell builtin" NEWLINE, second_parameter);
      } else if (strcmp(second_parameter, "type") == 0) {
        printf("%s is a shell builtin" NEWLINE, second_parameter);
      } else {
        printf("%s: not found" NEWLINE, second_parameter);
      }
    } else {
      printf("%s: not found" NEWLINE, first_parameter);
    }
  }

  return 0;
}
