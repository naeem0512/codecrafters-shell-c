#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 10
#define MAX_ARG_LENGTH 100

void handle_echo(char *input) {
    // Skip "echo " part (5 characters)
    char *args = input + 5;
    
    // Print the rest of the input (arguments)
    printf("%s\n", args);
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
    
    // Print error message for other commands
    printf("%s: command not found\n", input);
  }
  
  return 0;
}
