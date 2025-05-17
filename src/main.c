#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    
    // Print error message for the command
    printf("%s: command not found\n", input);
  }
  
  return 0;
}
