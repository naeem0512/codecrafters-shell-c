# POSIX-Compliant Shell Implementation

---



This project is a custom POSIX-compliant shell implementation in C, created as part of the CodeCrafters "Build Your Own Shell" challenge.

🔗 GitHub Repo: [github.com/naeem0512/codecrafters-shell-c](https://github.com/naeem0512/codecrafters-shell-c)

---

## 🚀 Features

- **Command Execution**: Runs external programs with arguments

- **Built-in Commands**:
  - `echo`: Handles quotes and escapes  
  - `exit`: Terminates the shell  
  - `type`: Identifies command types (builtins or executables)  
  - `pwd`: Prints current working directory  
  - `cd`: Supports absolute paths, relative paths, and `~`  

- **Tab Completion**: For both built-in and external commands

- **Quoting Mechanisms**:
  - Single quotes (`'`)
  - Double quotes (`"`)
  - Backslashes (`\`) for escape sequences

- **Output Redirection**:
  - Standard Output: `>`, `>>`  
  - Standard Error: `2>`, `2>>`

- **Pipeline Support**:
  - Execute pipelines like: `cmd1 | cmd2 | cmd3`

---

## ⚙️ Building and Running

### 🔧 Prerequisites

- GCC or another C compiler  
- CMake  
- Readline library (`libreadline-dev` on Debian/Ubuntu)

---

### 🧱 Build Instructions

```bash
# Clone the repository
git clone https://github.com/naeem0512/codecrafters-shell-c.git
cd codecrafters-shell-c
````
```bash
# Build the project
cmake .
make
````

---

### ▶️ Run the Shell

```bash
./your_program.sh
```


---

## 🛠️ Implementation Details

This shell was built using standard C libraries and POSIX system calls. Key components include:

* **Command Parsing**: Lexical handling of quotes, escapes, and control symbols
* **Process Management**: `fork()`, `exec()`, and wait handling
* **File Descriptors**: For redirection and pipelines
* **Signal Handling**: Graceful response to keyboard interrupts (e.g., `Ctrl+C`)
* **Environment Expansion**: Support for home (`~`) and `$PATH` resolution

---

## 📚 Learning Outcomes

During this project, I deepened my understanding of:

* POSIX-compliant shell behavior and standards
* Process creation and inter-process communication
* File descriptor control and I/O redirection
* Building command interpreters and pipelines from scratch
* Command parsing with lexical rules

---

## 🙏 Acknowledgements

This project was built as part of the CodeCrafters **"Build Your Own Shell"** challenge.

---

## 📄 License

This project is licensed under the **MIT License**.
