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
#include <dirent.h>

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

// Structure to hold completion matches
typedef struct {
    char **matches;
    int count;
    int capacity;
} CompletionMatches;

// Structure to hold pipeline components
typedef struct {
    char **commands;        // Array of commands
    char ***args;           // Array of argument arrays
    int *arg_counts;        // Array of argument counts
    int num_commands;       // Number of commands in pipeline
} Pipeline;

// Function to find executables in PATH that match a prefix
char* find_executable_match(const char *prefix) {
    char *path = getenv("PATH");
    if (!path) return NULL;

    // Make a copy of PATH since strtok modifies its argument
    char path_copy[MAX_PATH_LENGTH];
    strncpy(path_copy, path, MAX_PATH_LENGTH - 1);
    path_copy[MAX_PATH_LENGTH - 1] = '\0';

    // Split PATH into directories
    char *dir = strtok(path_copy, ":");
    while (dir != NULL) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                // Skip . and .. entries
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }

                // Check if the entry starts with our prefix
                if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
                    // Construct full path
                    char full_path[MAX_PATH_LENGTH];
                    snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", dir, entry->d_name);
                    
                    // Check if it's executable
                    if (access(full_path, X_OK) == 0) {
                        closedir(d);
                        return strdup(entry->d_name);  // Return just the executable name
                    }
                }
            }
            closedir(d);
        }
        dir = strtok(NULL, ":");
    }
    
    return NULL;
}

// Function to find all executables in PATH that match a prefix
CompletionMatches* find_all_executable_matches(const char *prefix) {
    CompletionMatches *matches = malloc(sizeof(CompletionMatches));
    if (!matches) return NULL;
    
    matches->matches = NULL;
    matches->count = 0;
    matches->capacity = 0;
    
    char *path = getenv("PATH");
    if (!path) return matches;

    // Make a copy of PATH since strtok modifies its argument
    char path_copy[MAX_PATH_LENGTH];
    strncpy(path_copy, path, MAX_PATH_LENGTH - 1);
    path_copy[MAX_PATH_LENGTH - 1] = '\0';

    // Split PATH into directories
    char *dir = strtok(path_copy, ":");
    while (dir != NULL) {
        DIR *d = opendir(dir);
        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                // Skip . and .. entries
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }

                // Check if the entry starts with our prefix
                if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0) {
                    // Construct full path
                    char full_path[MAX_PATH_LENGTH];
                    snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", dir, entry->d_name);
                    
                    // Check if it's executable
                    if (access(full_path, X_OK) == 0) {
                        // Grow the matches array if needed
                        if (matches->count >= matches->capacity) {
                            int new_capacity = matches->capacity == 0 ? 4 : matches->capacity * 2;
                            char **new_matches = realloc(matches->matches, new_capacity * sizeof(char*));
                            if (!new_matches) {
                                // Clean up on failure
                                for (int i = 0; i < matches->count; i++) {
                                    free(matches->matches[i]);
                                }
                                free(matches->matches);
                                free(matches);
                                closedir(d);
                                return NULL;
                            }
                            matches->matches = new_matches;
                            matches->capacity = new_capacity;
                        }
                        
                        // Add the match
                        matches->matches[matches->count] = strdup(entry->d_name);
                        if (!matches->matches[matches->count]) {
                            // Clean up on failure
                            for (int i = 0; i < matches->count; i++) {
                                free(matches->matches[i]);
                            }
                            free(matches->matches);
                            free(matches);
                            closedir(d);
                            return NULL;
                        }
                        matches->count++;
                    }
                }
            }
            closedir(d);
        }
        dir = strtok(NULL, ":");
    }
    
    return matches;
}

// Function to free completion matches
void free_completion_matches(CompletionMatches *matches) {
    if (matches) {
        for (int i = 0; i < matches->count; i++) {
            free(matches->matches[i]);
        }
        free(matches->matches);
        free(matches);
    }
}

// Function to generate completions for commands
char* command_generator(const char* text, int state) {
    static int list_index, len;
    static CompletionMatches *external_matches;
    static int external_match_index;
    const char *name;
    
    // If this is a new word to complete, initialize now
    if (!state) {
        list_index = 0;
        len = strlen(text);
        external_match_index = 0;
        
        // Free any previous matches
        if (external_matches) {
            free_completion_matches(external_matches);
            external_matches = NULL;
        }
    }
    
    // First try builtin commands
    while (list_index < num_builtins) {
        name = builtins[list_index++];
        if (strncmp(name, text, len) == 0) {
            char *completion = malloc(strlen(name) + 1);
            if (completion) {
                strcpy(completion, name);
                return completion;
            }
        }
    }
    
    // If no more builtins, try external executables
    if (list_index >= num_builtins) {
        if (!external_matches) {
            external_matches = find_all_executable_matches(text);
            if (!external_matches) return NULL;
        }
        
        if (external_match_index < external_matches->count) {
            return strdup(external_matches->matches[external_match_index++]);
        }
    }
    
    return NULL;  // No more matches
}

// Function to attempt completion
char** command_completion(const char* text, int start, int end) {
    char **matches = NULL;
    
    // Only complete at the start of the line
    if (start == 0) {
        // Get all matches
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

// Function to parse a pipeline command
Pipeline* parse_pipeline(char *input) {
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) {
        perror("malloc failed");
        return NULL;
    }
    
    // Initialize pipeline structure
    pipeline->commands = NULL;
    pipeline->args = NULL;
    pipeline->arg_counts = NULL;
    pipeline->num_commands = 0;
    
    // Count number of commands (number of pipes + 1)
    int num_pipes = 0;
    char *p = input;
    int in_quotes = 0;
    char quote_char = 0;
    
    // Count pipes, but ignore those inside quotes
    while (*p) {
        if (*p == '\'' || *p == '"') {
            if (!in_quotes) {
                in_quotes = 1;
                quote_char = *p;
            } else if (*p == quote_char) {
                in_quotes = 0;
            }
        } else if (*p == '|' && !in_quotes) {
            num_pipes++;
        }
        p++;
    }
    
    int num_commands = num_pipes + 1;
    
    // Allocate arrays
    pipeline->commands = malloc(num_commands * sizeof(char*));
    pipeline->args = malloc(num_commands * sizeof(char**));
    pipeline->arg_counts = malloc(num_commands * sizeof(int));
    
    if (!pipeline->commands || !pipeline->args || !pipeline->arg_counts) {
        perror("malloc failed");
        goto cleanup;
    }
    
    // Initialize arrays
    for (int i = 0; i < num_commands; i++) {
        pipeline->commands[i] = NULL;
        pipeline->args[i] = malloc(MAX_ARGS * sizeof(char*));
        if (!pipeline->args[i]) {
            perror("malloc failed");
            goto cleanup;
        }
        pipeline->arg_counts[i] = 0;
    }
    
    // Split input into commands
    char *cmd_start = input;
    for (int i = 0; i < num_commands; i++) {
        // Find end of this command, respecting quotes
        char *cmd_end = cmd_start;
        in_quotes = 0;
        quote_char = 0;
        
        while (*cmd_end) {
            if (*cmd_end == '\'' || *cmd_end == '"') {
                if (!in_quotes) {
                    in_quotes = 1;
                    quote_char = *cmd_end;
                } else if (*cmd_end == quote_char) {
                    in_quotes = 0;
                }
            } else if (*cmd_end == '|' && !in_quotes) {
                break;
            }
            cmd_end++;
        }
        
        if (*cmd_end == '|') {
            *cmd_end = '\0';  // Temporarily terminate string
        }
        
        // Parse this command and its arguments
        char *str = cmd_start;
        while (pipeline->arg_counts[i] < MAX_ARGS - 1) {
            // Skip leading spaces
            while (*str == ' ') str++;
            if (*str == '\0') break;
            
            // Allocate space for this argument
            pipeline->args[i][pipeline->arg_counts[i]] = malloc(MAX_ARG_LENGTH);
            if (!pipeline->args[i][pipeline->arg_counts[i]]) {
                perror("malloc failed");
                goto cleanup;
            }
            
            int j = 0;
            // Handle quoted argument
            if (*str == '\'' || *str == '"') {
                char quote = *str;
                str++; // Skip opening quote
                
                while (*str != '\0' && *str != quote && j < MAX_ARG_LENGTH - 1) {
                    if (quote == '"' && *str == '\\') {
                        str++; // Skip backslash
                        if (*str == '\0') {
                            fprintf(stderr, "Error: Unmatched backslash\n");
                            goto cleanup;
                        }
                        if (*str == '\\' || *str == '$' || *str == '"' || *str == '\n') {
                            pipeline->args[i][pipeline->arg_counts[i]][j++] = *str++;
                        } else {
                            pipeline->args[i][pipeline->arg_counts[i]][j++] = '\\';
                            pipeline->args[i][pipeline->arg_counts[i]][j++] = *str++;
                        }
                    } else {
                        pipeline->args[i][pipeline->arg_counts[i]][j++] = *str++;
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
                while (*str != '\0' && *str != ' ' && j < MAX_ARG_LENGTH - 1) {
                    if (*str == '\\') {
                        str++; // Skip backslash
                        if (*str == '\0') {
                            fprintf(stderr, "Error: Unmatched backslash\n");
                            goto cleanup;
                        }
                        if (*str == '\n') {
                            str++; // Skip newline
                        } else {
                            pipeline->args[i][pipeline->arg_counts[i]][j++] = *str++;
                        }
                    } else {
                        pipeline->args[i][pipeline->arg_counts[i]][j++] = *str++;
                    }
                }
            }
            pipeline->args[i][pipeline->arg_counts[i]][j] = '\0';
            
            if (j > 0) {
                pipeline->arg_counts[i]++;
            } else {
                free(pipeline->args[i][pipeline->arg_counts[i]]);
            }
        }
        pipeline->args[i][pipeline->arg_counts[i]] = NULL;
        
        // Set command name
        if (pipeline->arg_counts[i] > 0) {
            pipeline->commands[i] = strdup(pipeline->args[i][0]);
        }
        
        // Move to next command
        if (*cmd_end == '\0' && i < num_commands - 1) {
            *cmd_end = '|';  // Restore pipe character
            cmd_start = cmd_end + 1;
            // Skip spaces after pipe
            while (*cmd_start == ' ') cmd_start++;
        }
    }
    
    pipeline->num_commands = num_commands;
    return pipeline;
    
cleanup:
    // Free allocated memory on error
    if (pipeline) {
        for (int i = 0; i < num_commands; i++) {
            if (pipeline->args && pipeline->args[i]) {
                for (int j = 0; j < pipeline->arg_counts[i]; j++) {
                    free(pipeline->args[i][j]);
                }
                free(pipeline->args[i]);
            }
            free(pipeline->commands[i]);
        }
        free(pipeline->commands);
        free(pipeline->args);
        free(pipeline->arg_counts);
        free(pipeline);
    }
    return NULL;
}

// Function to free pipeline structure
void free_pipeline(Pipeline *pipeline) {
    if (pipeline) {
        for (int i = 0; i < pipeline->num_commands; i++) {
            free(pipeline->commands[i]);
            for (int j = 0; j < pipeline->arg_counts[i]; j++) {
                free(pipeline->args[i][j]);
            }
            free(pipeline->args[i]);
        }
        free(pipeline->commands);
        free(pipeline->args);
        free(pipeline->arg_counts);
        free(pipeline);
    }
}

// Echo command without redirection handling - for pipeline use
void pipeline_echo(char **args) {
    // Start from args[1] to skip the command name
    int first_arg = 1;
    
    for (int i = 1; args[i] != NULL; i++) {
        // Print space between arguments
        if (i > 1) {
            printf(" ");
        }
        
        // Print the argument directly
        printf("%s", args[i]);
    }
    printf("\n");
    fflush(stdout);  // Make sure output is flushed to the pipe
}

// Function to execute a built-in command with pipe redirection
int execute_builtin_with_pipe(const char *cmd, char **args, int pipe_in, int pipe_out) {
    // Save original file descriptors
    int original_stdin = -1;
    int original_stdout = -1;
    
    // Set up input redirection if needed
    if (pipe_in != -1) {
        original_stdin = dup(STDIN_FILENO);
        if (original_stdin == -1) {
            perror("dup failed");
            return 1;
        }
        if (dup2(pipe_in, STDIN_FILENO) == -1) {
            perror("dup2 failed");
            close(original_stdin);
            return 1;
        }
        close(pipe_in);
    }
    
    // Set up output redirection if needed
    if (pipe_out != -1) {
        original_stdout = dup(STDOUT_FILENO);
        if (original_stdout == -1) {
            perror("dup failed");
            if (original_stdin != -1) {
                dup2(original_stdin, STDIN_FILENO);
                close(original_stdin);
            }
            return 1;
        }
        if (dup2(pipe_out, STDOUT_FILENO) == -1) {
            perror("dup2 failed");
            if (original_stdin != -1) {
                dup2(original_stdin, STDIN_FILENO);
                close(original_stdin);
            }
            close(original_stdout);
            return 1;
        }
        close(pipe_out);
    }
    
    int status = 0;
    
    // Execute the appropriate built-in command
    if (strcmp(cmd, "echo") == 0) {
        // Use specialized pipeline echo for direct output
        pipeline_echo(args);
    } else if (strcmp(cmd, "type") == 0) {
        // Handle 'type' for pipelines
        if (args[1] != NULL) {
            // Check if it's a builtin
            if (is_builtin(args[1])) {
                printf("%s is a shell builtin\n", args[1]);
            } else {
                // Check if it's an executable in PATH
                char *exec_path = find_executable(args[1]);
                if (exec_path) {
                    printf("%s is %s\n", args[1], exec_path);
                    free(exec_path);
                } else {
                    printf("%s: not found\n", args[1]);
                }
            }
        }
    } else if (strcmp(cmd, "pwd") == 0) {
        // PWD is simple, just call the handler
        handle_pwd();
    } else if (strcmp(cmd, "cd") == 0) {
        // CD needs to be handled carefully
        if (args[1] != NULL) {
            // Try to change directory directly
            if (chdir(args[1]) != 0) {
                fprintf(stderr, "cd: %s: %s\n", args[1], strerror(errno));
                status = 1;
            }
        } else {
            fprintf(stderr, "cd: missing argument\n");
            status = 1;
        }
    } else if (strcmp(cmd, "exit") == 0) {
        exit(0);
    }
    
    // Restore original file descriptors
    if (original_stdin != -1) {
        dup2(original_stdin, STDIN_FILENO);
        close(original_stdin);
    }
    if (original_stdout != -1) {
        dup2(original_stdout, STDOUT_FILENO);
        close(original_stdout);
    }
    
    return status;
}

// Function to execute a pipeline
void execute_pipeline(Pipeline *pipeline) {
    if (!pipeline || pipeline->num_commands < 2) {
        return;
    }
    
    // Create array of pipes
    int (*pipefd)[2] = malloc((pipeline->num_commands - 1) * sizeof(int[2]));
    if (!pipefd) {
        perror("malloc failed");
        free_pipeline(pipeline);
        return;
    }
    
    // Create all pipes
    for (int i = 0; i < pipeline->num_commands - 1; i++) {
        if (pipe(pipefd[i]) == -1) {
            perror("pipe failed");
            // Close any pipes we've already created
            for (int j = 0; j < i; j++) {
                close(pipefd[j][0]);
                close(pipefd[j][1]);
            }
            free(pipefd);
            free_pipeline(pipeline);
            return;
        }
    }
    
    // Create processes for each command
    pid_t *pids = malloc(pipeline->num_commands * sizeof(pid_t));
    if (!pids) {
        perror("malloc failed");
        // Close all pipes
        for (int i = 0; i < pipeline->num_commands - 1; i++) {
            close(pipefd[i][0]);
            close(pipefd[i][1]);
        }
        free(pipefd);
        free_pipeline(pipeline);
        return;
    }
    
    // Create a process for each command
    for (int i = 0; i < pipeline->num_commands; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork failed");
            // Kill any processes we've already created
            for (int j = 0; j < i; j++) {
                kill(pids[j], SIGTERM);
            }
            // Close all pipes
            for (int j = 0; j < pipeline->num_commands - 1; j++) {
                close(pipefd[j][0]);
                close(pipefd[j][1]);
            }
            free(pids);
            free(pipefd);
            free_pipeline(pipeline);
            return;
        }
        
        if (pids[i] == 0) {
            // Child process
            // Close all pipe ends we don't need
            for (int j = 0; j < pipeline->num_commands - 1; j++) {
                if (j != i - 1) close(pipefd[j][0]);
                if (j != i) close(pipefd[j][1]);
            }
            
            // Set up input redirection
            if (i > 0) {
                if (dup2(pipefd[i-1][0], STDIN_FILENO) == -1) {
                    perror("dup2 failed");
                    exit(1);
                }
                close(pipefd[i-1][0]);
            }
            
            // Set up output redirection
            if (i < pipeline->num_commands - 1) {
                if (dup2(pipefd[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2 failed");
                    exit(1);
                }
                close(pipefd[i][1]);
            }
            
            // Execute the command
            if (is_builtin(pipeline->commands[i])) {
                // For built-in commands, we need to handle them specially
                if (strcmp(pipeline->commands[i], "echo") == 0) {
                    // Use specialized pipeline echo for direct output
                    pipeline_echo(pipeline->args[i]);
                } else {
                    // For other built-ins, use the standard handler
                    execute_builtin_with_pipe(pipeline->commands[i], pipeline->args[i], -1, -1);
                }
                exit(0);
            } else {
                // For external commands, find and execute them
                char *exec_path = find_executable(pipeline->commands[i]);
                if (!exec_path) {
                    fprintf(stderr, "%s: command not found\n", pipeline->commands[i]);
                    exit(1);
                }
                execv(exec_path, pipeline->args[i]);
                fprintf(stderr, "execv failed for %s: %s\n", exec_path, strerror(errno));
                free(exec_path);
                exit(1);
            }
        }
    }
    
    // Parent process
    // Close all pipe ends
    for (int i = 0; i < pipeline->num_commands - 1; i++) {
        close(pipefd[i][0]);
        close(pipefd[i][1]);
    }
    
    // Wait for all children to complete
    for (int i = 0; i < pipeline->num_commands; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    free(pids);
    free(pipefd);
    free_pipeline(pipeline);
}

// Function to create directories recursively
void mkdir_recursive(const char *path) {
    char tmp[MAX_PATH_LENGTH];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;  // Remove trailing slash if present
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;  // Temporarily terminate string at this slash
            mkdir(tmp, 0777);  // Create directory (ignore errors)
            *p = '/';  // Restore slash
        }
    }
    
    mkdir(tmp, 0777);  // Create final directory
}

void execute_command(char *input) {
    // Check for pipeline
    Pipeline *pipeline = parse_pipeline(input);
    if (pipeline && pipeline->num_commands > 1) {
        execute_pipeline(pipeline);
        return;
    }
    
    // If not a pipeline or only has one command, continue with normal command execution
    // Make a copy of the input string because parse_redirection will modify it
    char *input_copy = strdup(input);
    if (!input_copy) {
        perror("strdup failed");
        if (pipeline) free_pipeline(pipeline);
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
            if (pipeline) free_pipeline(pipeline);
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
                        if (pipeline) free_pipeline(pipeline);
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
                if (pipeline) free_pipeline(pipeline);
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
                        if (pipeline) free_pipeline(pipeline);
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
        if (pipeline) free_pipeline(pipeline);
        return;  // Empty command
    }
    
    // Check for builtin commands
    if (strcmp(args[0], "echo") == 0) {
        handle_echo(input);
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
        if (pipeline) free_pipeline(pipeline);
        return;
    } else if (strcmp(args[0], "exit") == 0) {
        // Free allocated memory before exiting
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
        if (pipeline) free_pipeline(pipeline);
        exit(0);
    } else if (strcmp(args[0], "type") == 0) {
        if (arg_count > 1) {
            // Check if it's a builtin
            if (is_builtin(args[1])) {
                printf("%s is a shell builtin\n", args[1]);
            } else {
                // Check if it's an executable in PATH
                char *exec_path = find_executable(args[1]);
                if (exec_path) {
                    printf("%s is %s\n", args[1], exec_path);
                    free(exec_path);
                } else {
                    printf("%s: not found\n", args[1]);
                }
            }
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
        if (pipeline) free_pipeline(pipeline);
        return;
    } else if (strcmp(args[0], "pwd") == 0) {
        handle_pwd();
        // Free allocated arguments
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        if (redir) {
            free(redir->filename);
            free(redir);
        }
        free(input_copy);
        if (pipeline) free_pipeline(pipeline);
        return;
    } else if (strcmp(args[0], "cd") == 0) {
        if (arg_count > 1) {
            char expanded_path[MAX_PATH_LENGTH];
            
            // Handle ~ character
            if (args[1][0] == '~') {
                char *home = getenv("HOME");
                if (home == NULL) {
                    fprintf(stderr, "cd: HOME environment variable not set\n");
                    // Free allocated arguments
                    for (int i = 0; i < arg_count; i++) {
                        free(args[i]);
                    }
                    if (redir) {
                        free(redir->filename);
                        free(redir);
                    }
                    free(input_copy);
                    if (pipeline) free_pipeline(pipeline);
                    return;
                }
                
                // If path is just ~, use home directory directly
                if (args[1][1] == '\0') {
                    strncpy(expanded_path, home, MAX_PATH_LENGTH - 1);
                    expanded_path[MAX_PATH_LENGTH - 1] = '\0';
                } else {
                    // If path is ~/something, concatenate home and the rest of the path
                    snprintf(expanded_path, MAX_PATH_LENGTH, "%s%s", home, args[1] + 1);
                }
                
                // Try to change directory
                if (chdir(expanded_path) != 0) {
                    fprintf(stderr, "cd: %s: %s\n", expanded_path, strerror(errno));
                }
            } else {
                // Try to change directory
                if (chdir(args[1]) != 0) {
                    fprintf(stderr, "cd: %s: %s\n", args[1], strerror(errno));
                }
            }
        } else {
            fprintf(stderr, "cd: missing argument\n");
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
        if (pipeline) free_pipeline(pipeline);
        return;
    }
    
    // Find the executable
    char *exec_path = find_executable(args[0]);
    if (!exec_path) {
        // For command not found, we need to handle stderr redirection
        if (redir && redir->fd == 2) {
            // Create parent directories if they don't exist
            char *last_slash = strrchr(redir->filename, '/');
            if (last_slash != NULL) {
                char dir_path[MAX_PATH_LENGTH];
                strncpy(dir_path, redir->filename, last_slash - redir->filename);
                dir_path[last_slash - redir->filename] = '\0';
                
                if (strlen(dir_path) > 0) {
                    mkdir_recursive(dir_path);
                }
            }
            
            // Open output file with full permissions
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
        if (pipeline) free_pipeline(pipeline);
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
        if (pipeline) free_pipeline(pipeline);
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
                    mkdir_recursive(dir_path);
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
        if (pipeline) free_pipeline(pipeline);
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
        
        // Execute the command
        execute_command(input);
        
        // Free the input string
        free(input);
    }
    
    return 0;
}