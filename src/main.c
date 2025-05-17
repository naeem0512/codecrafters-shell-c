#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 10
#define MAX_ARG_LENGTH 100

// List of builtin commands
const char *builtins[] = {"echo", "exit", "type"};
const int num_builtins = 3;

int is_builtin(const char *cmd) {
    for (int i = 0; i < num_builtins; i++) {
        if (strcmp(cmd, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

void handle_echo(char *input) {
    // Skip "echo " part (5 characters)
    char *args = input + 5;
    
    // Print the rest of the input (arguments)
    printf("%s\n", args);
}

void handle_type(char *input) {
    // Skip "type " part (5 characters)
    char *cmd = input + 5;
    
    if (is_builtin(cmd)) {
        printf("%s is a shell builtin\n", cmd);
    } else {
        printf("%s: not found\n", cmd);
    }
}

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  while (1) {
    printf("$ ");
    
    // Wait for user input
    char input[100];
    if (fgets(input, 100, stdin) == NULL) {
      break;  // Exit on EOF (Ctrl+D)
    }
    
    // Remove newline character from input
    input[strcspn(input, "\n")] = 0;
    
    // Check for exit command
    if (strcmp(input, "exit 0") == 0) {
      exit(0);
    }
    
    // Check for echo command
    if (strncmp(input, "echo ", 5) == 0) {
      handle_echo(input);
      continue;
    }
    
    // Check for type command
    if (strncmp(input, "type ", 5) == 0) {
      handle_type(input);
      continue;
    }
    
    // Print error message for other commands
    printf("%s: command not found\n", input);
  }
  
  return 0;
}
