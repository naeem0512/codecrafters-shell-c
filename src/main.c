#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_ARGS 10
#define MAX_ARG_LENGTH 100
#define MAX_PATH_LENGTH 1024

// List of builtin commands
const char *builtins[] = {"echo", "exit", "type", "pwd", "cd"};
const int num_builtins = 5;

int is_builtin(const char *cmd) {
    for (int i = 0; i < num_builtins; i++) {
        if (strcmp(cmd, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

char* find_executable(const char *cmd) {
    char *path = getenv("PATH");
    if (!path) return NULL;

    // Make a copy of PATH since strtok modifies its argument
    char path_copy[MAX_PATH_LENGTH];
    strncpy(path_copy, path, MAX_PATH_LENGTH - 1);
    path_copy[MAX_PATH_LENGTH - 1] = '\0';

    // Split PATH into directories
    char *dir = strtok(path_copy, ":");
    while (dir != NULL) {
        char full_path[MAX_PATH_LENGTH];
        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", dir, cmd);
        
        // Check if file exists and is executable
        if (access(full_path, X_OK) == 0) {
            return strdup(full_path);
        }
        
        dir = strtok(NULL, ":");
    }
    
    return NULL;
}

void handle_echo(char *input) {
    // Skip "echo " part (5 characters)
    char *str = input + 5;
    int first_arg = 1;
    int last_was_quoted = 0;  // Track if last argument was quoted
    
    while (*str) {
        // Skip leading spaces
        while (*str == ' ') str++;
        if (*str == '\0') break;
        
        // Check if this is an adjacent quoted string
        int is_adjacent = last_was_quoted && str > input + 5 && *(str - 1) == '\'';
        
        // Print space between arguments, but not between adjacent quoted strings
        if (!first_arg && !is_adjacent) {
            printf(" ");
        }
        first_arg = 0;
        
        // Handle quoted argument
        if (*str == '\'') {
            str++; // Skip opening quote
            last_was_quoted = 1;  // Mark this as a quoted argument
            
            while (*str != '\0' && *str != '\'') {
                printf("%c", *str++);
            }
            if (*str == '\'') {
                str++; // Skip closing quote
            } else {
                fprintf(stderr, "Error: Unmatched single quote\n");
                return;
            }
        } else {
            // Handle unquoted argument
            last_was_quoted = 0;  // Mark this as an unquoted argument
            while (*str != '\0' && *str != ' ') {
                printf("%c", *str++);
            }
        }
    }
    printf("\n");
}

void handle_type(char *input) {
    // Skip "type " part (5 characters)
    char *cmd = input + 5;
    
    // First check if it's a builtin
    if (is_builtin(cmd)) {
        printf("%s is a shell builtin\n", cmd);
        return;
    }
    
    // Then check if it's an executable in PATH
    char *exec_path = find_executable(cmd);
    if (exec_path) {
        printf("%s is %s\n", cmd, exec_path);
        free(exec_path);
        return;
    }
    
    // If not found anywhere
    printf("%s: not found\n", cmd);
}

void handle_pwd(void) {
    char cwd[MAX_PATH_LENGTH];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
}

void handle_cd(char *input) {
    // Skip "cd " part (3 characters)
    char *path = input + 3;
    
    // Skip leading spaces
    while (*path == ' ') path++;
    
    // Check if path is empty
    if (*path == '\0') {
        fprintf(stderr, "cd: missing argument\n");
        return;
    }
    
    char expanded_path[MAX_PATH_LENGTH];
    
    // Handle ~ character
    if (*path == '~') {
        char *home = getenv("HOME");
        if (home == NULL) {
            fprintf(stderr, "cd: HOME environment variable not set\n");
            return;
        }
        
        // If path is just ~, use home directory directly
        if (*(path + 1) == '\0') {
            strncpy(expanded_path, home, MAX_PATH_LENGTH - 1);
            expanded_path[MAX_PATH_LENGTH - 1] = '\0';
        } else {
            // If path is ~/something, concatenate home and the rest of the path
            snprintf(expanded_path, MAX_PATH_LENGTH, "%s%s", home, path + 1);
        }
        path = expanded_path;
    }
    
    // Try to change directory
    if (chdir(path) != 0) {
        // Get the error message
        fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
    }
}

void execute_command(char *input) {
    char *args[MAX_ARGS];
    int arg_count = 0;
    char *str = input;
    
    // Parse arguments respecting quotes
    while (arg_count < MAX_ARGS - 1) {
        // Skip leading spaces
        while (*str == ' ') str++;
        if (*str == '\0') break;
        
        // Allocate space for this argument
        args[arg_count] = malloc(MAX_ARG_LENGTH);
        if (args[arg_count] == NULL) {
            perror("malloc failed");
            // Free previously allocated args
            for (int i = 0; i < arg_count; i++) {
                free(args[i]);
            }
            return;
        }
        
        int i = 0;
        // Handle quoted argument
        if (*str == '\'') {
            str++; // Skip opening quote
            while (*str != '\0' && *str != '\'' && i < MAX_ARG_LENGTH - 1) {
                args[arg_count][i++] = *str++;
            }
            if (*str == '\'') {
                str++; // Skip closing quote
            } else {
                fprintf(stderr, "Error: Unmatched single quote\n");
                free(args[arg_count]);
                // Free previously allocated args
                for (int i = 0; i < arg_count; i++) {
                    free(args[i]);
                }
                return;
            }
        } else {
            // Handle unquoted argument
            while (*str != '\0' && *str != ' ' && i < MAX_ARG_LENGTH - 1) {
                args[arg_count][i++] = *str++;
            }
        }
        args[arg_count][i] = '\0';
        
        // Only add non-empty arguments
        if (i > 0) {
            arg_count++;
        } else {
            free(args[arg_count]);
        }
    }
    args[arg_count] = NULL;  // NULL terminate the argument list
    
    if (arg_count == 0) return;  // Empty command
    
    // Find the executable
    char *exec_path = find_executable(args[0]);
    if (!exec_path) {
        printf("%s: command not found\n", args[0]);
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        return;
    }
    
    // Fork and execute
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        free(exec_path);
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        return;
    }
    
    if (pid == 0) {
        // Child process
        execv(exec_path, args);
        // If execv returns, it failed
        perror("execv failed");
        free(exec_path);
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        exit(1);
    } else {
        // Parent process
        free(exec_path);
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        int status;
        waitpid(pid, &status, 0);
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
    
    // Check for pwd command
    if (strcmp(input, "pwd") == 0) {
      handle_pwd();
      continue;
    }
    
    // Check for cd command
    if (strncmp(input, "cd ", 3) == 0) {
      handle_cd(input);
      continue;
    }
    
    // Execute external command
    execute_command(input);
  }
  
  return 0;
}

