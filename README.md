POSIX-Compliant Shell Implementation




This project is a custom POSIX-compliant shell implementation in C, created as part of the CodeCrafters "Build Your Own Shell" challenge.
Features

Command Execution: Executes external programs with arguments
Built-in Commands:

echo: Display text with proper handling of quotes and escapes
exit: Terminate the shell
type: Identify command types (builtins or executables)
pwd: Print the current working directory
cd: Change directory with support for absolute paths, relative paths, and the ~ character


Tab Completion: For both built-in and external commands
Quoting Mechanisms:

Single quotes (')
Double quotes (")
Backslashes (\)
Proper handling of escape sequences within quotes


Output Redirection:

Standard output (>, >>)
Standard error (2>, 2>>)


Pipeline Support: Multiple commands in a pipeline (cmd1 | cmd2 | cmd3 | ...)

Building and Running
Prerequisites

GCC or another C compiler
CMake
Readline library (libreadline-dev on Debian/Ubuntu systems)

Build Instructions
bash# Clone the repository
git clone https://github.com/yourusername/shell-implementation.git
cd shell-implementation

# Build the project
cmake .
make
Running the Shell
bash./your_program.sh
Implementation Details
The shell is implemented using standard C libraries and system calls. Key components include:

Command Parsing: Handles quotes, escapes, and special characters
Process Management: Uses fork(), exec() and related system calls
File Descriptors: Manages I/O for redirections and pipelines
Signal Handling: Ensures proper behavior with keyboard interrupts
Environment Variable Expansion: For home directory (~)
Executable Resolution: Searches PATH to find executable files

Learning Outcomes
Through this project, I gained deeper knowledge of:

POSIX standards and shell behavior
Process creation and management in Unix-like systems
File descriptor manipulation
Command parsing and lexical analysis
Input/output redirection
Inter-process communication through pipes

Acknowledgements
This project was completed as part of the CodeCrafters "Build Your Own Shell" challenge.