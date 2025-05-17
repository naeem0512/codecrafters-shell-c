#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>  // For mkdir
#include <errno.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS 10
#define MAX_ARG_LENGTH 100
#define MAX_PATH_LENGTH 1024

// List of builtin commands
const char *builtins[] = {"echo", "exit", "type", "pwd", "cd"};
const int num_builtins = 5;

// Structure to hold redirection information
typedef struct {
    int fd;           // File descriptor to redirect (1 for stdout)
    char *filename;   // Target filename
    int append;       // Whether to append (>>) or truncate (>)
} Redirection;

// Function to generate completions for builtin commands
char* command_generator(const char* text, int state) {
    static int list_index, len;
    const char *name;
    
    // If this is a new word to complete, initialize now
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    // Return the next builtin command that matches
    while (list_index < num_builtins) {
        name = builtins[list_index++];
        if (strncmp(name, text, len) == 0) {
            // Allocate memory for the completion string with null terminator only
            // Let readline handle adding the space automatically
            char *completion = malloc(strlen(name) + 1);
            if (completion) {
                strcpy(completion, name);
                return completion;
            }
        }
    }
    
    return NULL;  // No more matches
}

// Function to attempt completion
char** command_completion(const char* text, int start, int end) {
    char **matches = NULL;
    
    // Only complete at the start of the line
    if (start == 0) {
        matches = rl_completion_matches(text, command_generator);
    }
    
    return matches;
}

// Initialize readline
void init_readline(void) {
    // Tell readline to use our completion function
    rl_attempted_completion_function = command_completion;
    
    // Disable filename completion
    rl_completer_quote_characters = "";
    rl_completer_word_break_characters = " \t\n\"\\'`@$><=;|&{(";
}

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

// Parse redirection operators from command
Redirection* parse_redirection(char **input) {
    char *str = *input;
    Redirection *redir = NULL;
    char *redirection_start = NULL;
    int redirection_length = 0;
    
    // Look for redirection operators
    char *redirect_pos = NULL;
    char *current = str;
    while (*current != '\0') {
        if (*current == '>') {
            redirect_pos = current;
            break;
        }
        current++;
    }
    
    if (redirect_pos != NULL) {
        // Found a redirection operator
        redirection_start = redirect_pos;
        
        redir = malloc(sizeof(Redirection));
        if (!redir) {
            perror("malloc failed");
            return NULL;
        }
        
        // Check for file descriptor
        if (redirect_pos > str && *(redirect_pos - 1) >= '0' && *(redirect_pos - 1) <= '9') {
            redir->fd = *(redirect_pos - 1) - '0';
            // Remove the file descriptor from input
            *(redirect_pos - 1) = ' ';
        } else {
            redir->fd = 1;  // Default to stdout
        }
        
        // Check for append operator
        if (*(redirect_pos + 1) == '>') {
            redir->append = 1;
            redirect_pos += 2;
        } else {
            redir->append = 0;
            redirect_pos++;
        }
        
        // Skip spaces after operator
        while (*redirect_pos == ' ') redirect_pos++;
        
        // Get filename
        char *filename_start = redirect_pos;
        while (*redirect_pos != '\0' && *redirect_pos != ' ') redirect_pos++;
        int filename_len = redirect_pos - filename_start;
        
        redir->filename = malloc(filename_len + 1);
        if (!redir->filename) {
            perror("malloc failed");
            free(redir);
            return NULL;
        }
        strncpy(redir->filename, filename_start, filename_len);
        redir->filename[filename_len] = '\0';
        
        // Calculate the length of the redirection part
        redirection_length = redirect_pos - redirection_start;
        
        // Create a new string with redirection part removed
        int new_length = strlen(str) - redirection_length;
        char *new_str = malloc(new_length + 1);
        if (!new_str) {
            perror("malloc failed");
            free(redir->filename);
            free(redir);
            return NULL;
        }
        
        // Copy part before redirection
        strncpy(new_str, str, redirection_start - str);
        
        // Add a null terminator at the end of the command part
        new_str[redirection_start - str] = '\0';
        
        // Replace the original string with the new one
        *input = new_str;
    }
    
    return redir;
}

void handle_echo(char *input) {
    // Make a copy of the input string because parse_redirection will modify it
    char *input_copy = strdup(input);
    if (!input_copy) {
        perror("strdup failed");
        return;
    }
    
    // Parse redirection if any
    Redirection *redir = parse_redirection(&input_copy);
    int original_stdout = -1;
    int original_stderr = -1;
    int output_fd = -1;
    
    if (redir) {
        // Only redirect if the specified fd matches
        // For echo, we only care about stdout (fd=1) or stderr (fd=2)
        if (redir->fd == 1) {
            // Save original stdout
            original_stdout = dup(STDOUT_FILENO);
            if (original_stdout == -1) {
                perror("dup failed");
                free(redir->filename);
                free(redir);
                free(input_copy);
                return;
            }
            
            // Open output file
            int flags = O_WRONLY | O_CREAT;
            flags |= redir->append ? O_APPEND : O_TRUNC;
            output_fd = open(redir->filename, flags, 0644);
            if (output_fd == -1) {
                perror("open failed");
                close(original_stdout);
                free(redir->filename);
                free(redir);
                free(input_copy);
                return;
            }
            
            // Redirect stdout
            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                perror("dup2 failed");
                close(original_stdout);
                close(output_fd);
                free(redir->filename);
                free(redir);
                free(input_copy);
                return;
            }
            close(output_fd);
        } else if (redir->fd == 2) {
            // Save original stderr
            original_stderr = dup(STDERR_FILENO);
            if (original_stderr == -1) {
                perror("dup failed");
                free(redir->filename);
                free(redir);
                free(input_copy);
                return;
            }
            
            // Open output file
            int flags = O_WRONLY | O_CREAT;
            flags |= redir->append ? O_APPEND : O_TRUNC;
            output_fd = open(redir->filename, flags, 0644);
            if (output_fd == -1) {
                perror("open failed");
                close(original_stderr);
                free(redir->filename);
                free(redir);
                free(input_copy);
                return;
            }
            
            // Redirect stderr
            if (dup2(output_fd, STDERR_FILENO) == -1) {
                perror("dup2 failed");
                close(original_stderr);
                close(output_fd);
                free(redir->filename);
                free(redir);
                free(input_copy);
                return;
            }
            close(output_fd);
        }
    }
    
    // Skip "echo " part (5 characters)
    char *str = input_copy + 5;
    int first_arg = 1;
    int last_was_quoted = 0;  // Track if last argument was quoted
    
    while (*str) {
        // Skip leading spaces
        while (*str == ' ') str++;
        if (*str == '\0') break;
        
        // Check if this is an adjacent quoted string
        int is_adjacent = last_was_quoted && str > input_copy + 5 && 
                         ((*(str - 1) == '\'') || (*(str - 1) == '"'));
        
        // Print space between arguments, but not between adjacent quoted strings
        if (!first_arg && !is_adjacent) {
            printf(" ");
        }
        first_arg = 0;
        
        // Handle quoted argument
        if (*str == '\'' || *str == '"') {
            char quote = *str;  // Remember which quote we're handling
            str++; // Skip opening quote
            last_was_quoted = 1;  // Mark this as a quoted argument
            
            while (*str != '\0' && *str != quote) {
                if (quote == '"' && *str == '\\') {
                    // Handle backslash in double quotes
                    str++; // Skip the backslash
                    if (*str == '\0') {
                        fprintf(stderr, "Error: Unmatched backslash\n");
                        goto cleanup;
                    }
                    // Only escape special characters in double quotes
                    if (*str == '\\' || *str == '$' || *str == '"' || *str == '\n') {
                        printf("%c", *str++);
                    } else {
                        // For other characters, print the backslash and the character
                        printf("\\%c", *str++);
                    }
                } else {
                    printf("%c", *str++);
                }
            }
            if (*str == quote) {
                str++; // Skip closing quote
            } else {
                fprintf(stderr, "Error: Unmatched %c\n", quote);
                goto cleanup;
            }
        } else {
            // Handle unquoted argument
            last_was_quoted = 0;  // Mark this as an unquoted argument
            while (*str != '\0' && *str != ' ') {
                if (*str == '\\') {
                    str++; // Skip the backslash
                    if (*str == '\0') {
                        fprintf(stderr, "Error: Unmatched backslash\n");
                        goto cleanup;
                    }
                    if (*str == '\n') {
                        // Handle line continuation
                        str++; // Skip the newline
                    } else {
                        // Preserve the literal value of the next character
                        printf("%c", *str++);
                    }
                } else {
                    printf("%c", *str++);
                }
            }
        }
    }
    printf("\n");
    
cleanup:
    // Restore stdout if redirected
    if (original_stdout != -1) {
        if (dup2(original_stdout, STDOUT_FILENO) == -1) {
            perror("dup2 failed");
        }
        close(original_stdout);
    }
    
    // Restore stderr if redirected
    if (original_stderr != -1) {
        if (dup2(original_stderr, STDERR_FILENO) == -1) {
            perror("dup2 failed");
        }
        close(original_stderr);
    }
    
    if (redir) {
        free(redir->filename);
        free(redir);
    }
    
    free(input_copy);
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
    // Make a copy of the input string because parse_redirection will modify it
    char *input_copy = strdup(input);
    if (!input_copy) {
        perror("strdup failed");
        return;
    }
    
    // Parse redirection if any
    Redirection *redir = parse_redirection(&input_copy);
    
    char *args[MAX_ARGS];
    int arg_count = 0;
    char *str = input_copy;
    
    // Parse arguments respecting quotes and escapes
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
            if (redir) {
                free(redir->filename);
                free(redir);
            }
            free(input_copy);
            return;
        }
        
        int i = 0;
        // Handle quoted argument
        if (*str == '\'' || *str == '"') {
            char quote = *str;  // Remember which quote we're handling
            str++; // Skip opening quote
            
            while (*str != '\0' && *str != quote && i < MAX_ARG_LENGTH - 1) {
                if (quote == '"' && *str == '\\') {
                    // Handle backslash in double quotes
                    str++; // Skip the backslash
                    if (*str == '\0') {
                        fprintf(stderr, "Error: Unmatched backslash\n");
                        free(args[arg_count]);
                        // Free previously allocated args
                        for (int i = 0; i < arg_count; i++) {
                            free(args[i]);
                        }
                        if (redir) {
                            free(redir->filename);
                            free(redir);
                        }
                        free(input_copy);
                        return;
                    }
                    // Only escape special characters in double quotes
                    if (*str == '\\' || *str == '$' || *str == '"' || *str == '\n') {
                        args[arg_count][i++] = *str++;
                    } else {
                        // For other characters, keep the backslash and the character
                        args[arg_count][i++] = '\\';
                        args[arg_count][i++] = *str++;
                    }
                } else {
                    args[arg_count][i++] = *str++;
                }
            }
            if (*str == quote) {
                str++; // Skip closing quote
            } else {
                fprintf(stderr, "Error: Unmatched %c\n", quote);
                free(args[arg_count]);
                // Free previously allocated args
                for (int i = 0; i < arg_count; i++) {
                    free(args[i]);
                }
                if (redir) {
                    free(redir->filename);
                    free(redir);
                }
                free(input_copy);
                return;
            }
        } else {
            // Handle unquoted argument
            while (*str != '\0' && *str != ' ' && i < MAX_ARG_LENGTH - 1) {
                if (*str == '\\') {
                    str++; // Skip the backslash
                    if (*str == '\0') {
                        fprintf(stderr, "Error: Unmatched backslash\n");
                        free(args[arg_count]);
                        // Free previously allocated args
                        for (int i = 0; i < arg_count; i++) {
                            free(args[i]);
                        }
                        if (redir) {
                            free(redir->filename);
                            free(redir);
                        }
                        free(input_copy);
                        return;
                    }
                    if (*str == '\n') {
                        // Handle line continuation
                        str++; // Skip the newline
                    } else {
                        // Preserve the literal value of the next character
                        args[arg_count][i++] = *str++;
                    }
                } else {
                    args[arg_count][i++] = *str++;
                }
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
    
    if (arg_count == 0) {
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
        return;  // Empty command
    }
    
    // Find the executable
    char *exec_path = find_executable(args[0]);
    if (!exec_path) {
        // For command not found, we need to handle stderr redirection
        if (redir && redir->fd == 2) {
            // Open the error file
            int flags = O_WRONLY | O_CREAT;
            flags |= redir->append ? O_APPEND : O_TRUNC;
            int error_fd = open(redir->filename, flags, 0666);
            if (error_fd != -1) {
                // Save original stderr
                int original_stderr = dup(STDERR_FILENO);
                if (original_stderr != -1) {
                    // Redirect stderr to the file
                    if (dup2(error_fd, STDERR_FILENO) != -1) {
                        fprintf(stderr, "%s: command not found\n", args[0]);
                        // Restore original stderr
                        dup2(original_stderr, STDERR_FILENO);
                        close(original_stderr);
                    }
                }
                close(error_fd);
            }
        } else {
            fprintf(stderr, "%s: command not found\n", args[0]);
        }
        
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
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
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
        return;
    }
    
    if (pid == 0) {
        // Child process
        if (redir) {
            // Create parent directories if they don't exist
            char *last_slash = strrchr(redir->filename, '/');
            if (last_slash != NULL) {
                char dir_path[MAX_PATH_LENGTH];
                strncpy(dir_path, redir->filename, last_slash - redir->filename);
                dir_path[last_slash - redir->filename] = '\0';
                
                if (strlen(dir_path) > 0) {
                    // Create directory recursively (like mkdir -p)
                    char *p = dir_path;
                    while (*p != '\0') {
                        if (*p == '/') {
                            *p = '\0';  // Temporarily terminate string
                            if (strlen(dir_path) > 0) {
                                if (mkdir(dir_path, 0777) == -1 && errno != EEXIST) {
                                    fprintf(stderr, "mkdir failed for %s: %s\n", dir_path, strerror(errno));
                                }
                            }
                            *p = '/';  // Restore slash
                        }
                        p++;
                    }
                    // Create the final directory level
                    if (mkdir(dir_path, 0777) == -1 && errno != EEXIST) {
                        fprintf(stderr, "mkdir failed for %s: %s\n", dir_path, strerror(errno));
                    }
                }
            }
            
            // Open output file with full permissions
            int flags = O_WRONLY | O_CREAT;
            flags |= redir->append ? O_APPEND : O_TRUNC;
            int output_fd = open(redir->filename, flags, 0666);
            if (output_fd == -1) {
                fprintf(stderr, "open failed for %s: %s\n", redir->filename, strerror(errno));
                exit(1);
            }
            
            // Redirect the appropriate file descriptor
            if (dup2(output_fd, redir->fd) == -1) {
                fprintf(stderr, "dup2 failed: %s\n", strerror(errno));
                close(output_fd);
                exit(1);
            }
            close(output_fd);
        }
        
        // Execute the command
        execv(exec_path, args);
        
        // If execv returns, it failed
        fprintf(stderr, "execv failed for %s: %s\n", exec_path, strerror(errno));
        exit(1);
    } else {
        // Parent process
        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);
        
        // Free resources in parent
        free(exec_path);
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
    }
}

int main(int argc, char *argv[]) {
    // Initialize readline
    init_readline();
    
    // Flush after every printf
    setbuf(stdout, NULL);
    
    while (1) {
        // Use readline to get input
        char *input = readline("$ ");
        if (!input) {
            break;  // Exit on EOF (Ctrl+D)
        }
        
        // Add non-empty commands to history
        if (strlen(input) > 0) {
            add_history(input);
        }
        
        // Check for exit command
        if (strcmp(input, "exit 0") == 0) {
            free(input);
            exit(0);
        }
        
        // Check for echo command
        if (strncmp(input, "echo ", 5) == 0) {
            handle_echo(input);
            free(input);
            continue;
        }
        
        // Check for type command
        if (strncmp(input, "type ", 5) == 0) {
            handle_type(input);
            free(input);
            continue;
        }
        
        // Check for pwd command
        if (strcmp(input, "pwd") == 0) {
            handle_pwd();
            free(input);
            continue;
        }
        
        // Check for cd command
        if (strncmp(input, "cd ", 3) == 0) {
            handle_cd(input);
            free(input);
            continue;
        }
        
        // Execute external command
        execute_command(input);
        
        // Free the input string
        free(input);
    }
    
    return 0;
}

