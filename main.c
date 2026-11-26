/*
 * main.c - gshell, the Gaucho Shell
 *
 * This is the only file you edit and submit. main() and the prompt loop
 * are done for you. Every other function below has a comment describing
 * what it does. Work through the checkpoints in the writeup in order:
 *
 *   Checkpoint 1: tokenize, parse_pipeline, and the exit built-in
 *   Checkpoint 2: run_command and run_pipeline for a single command
 *   Checkpoint 3: input and output redirection
 *   Checkpoint 4: pipelines
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/** The longest input line is 100 characters, plus 1 for the '\0' */
#define MAX_LINE 101

/** A 100-character line has at most 50 whitespace-separated tokens */
#define MAX_TOKENS 50

/** A pipeline can never have more commands than the line has tokens */
#define MAX_COMMANDS MAX_TOKENS

/**
 * One command of a pipeline. For example, the line
 *
 *     /bin/sort -r < in.txt | /usr/bin/head -n 3
 *
 * is a pipeline of two commands. The first has argv {"/bin/sort", "-r",
 * NULL}, input_file "in.txt", and output_file NULL.
 */
typedef struct {
    /** The executable path and its arguments, followed by NULL (as execve expects) */
    char *argv[MAX_TOKENS + 1];
    /** The file named after <, or NULL if the command has no input redirection */
    char *input_file;
    /** The file named after >, or NULL if the command has no output redirection */
    char *output_file;
} command_t;

/**
 * Splits `line` into its whitespace-separated tokens and stores a pointer
 * to each one in `tokens`. Changes the contents of `line`.
 *
 * Returns the number of tokens.
 *
 * __attribute__((unused)) stops the compiler from warning that a function
 * is never called. You can remove it once you call the function.
 */
__attribute__((unused))
static int tokenize(char *line, char *tokens[]) {
    // TODO (Checkpoint 1)
    int count = 0;
    char *save;
    char *token = strtok_r(line, " \t\n\r\f\v", &save);
    while (token != NULL) {
        if (count == MAX_TOKENS) {
            return -1;
        }
        tokens[count++] = token;
        token = strtok_r(NULL, " \t\n\r\f\v", &save);
    }
    return count;
}

/**
 * Groups `tokens` into the commands of a pipeline and fills in one
 * command_t per command in `commands`.
 *
 * Returns the number of commands, or -1 if the tokens are not a valid
 * pipeline.
 */
__attribute__((unused))
static int parse_pipeline(char *tokens[], int num_tokens, command_t commands[]) {
    // TODO (Checkpoint 1)
    if (num_tokens <= 0) {
        return -1;
    }
    int num_commands = 0;
    int argc = 0;
    memset(commands, 0, sizeof(command_t) * MAX_COMMANDS);
    for (int i = 0; i < num_tokens; i++) {
        char *token = tokens[i];
        command_t *cmd = &commands[num_commands];
        if (strcmp(token, "|") == 0) {
            if (argc == 0 || i == num_tokens - 1) {
                return -1;
            }
            cmd->argv[argc] = NULL;
            num_commands++;
            argc = 0;
        } else if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
            char **file = strcmp(token, "<") == 0 ? &cmd->input_file : &cmd->output_file;
            if (*file != NULL || i + 1 == num_tokens) {
                return -1;
            }
            char *name = tokens[++i];
            if (strcmp(name, "<") == 0 || strcmp(name, ">") == 0 || strcmp(name, "|") == 0) {
                return -1;
            }
            *file = name;
        } else {
            cmd->argv[argc++] = token;
        }
    }
    if (argc == 0) {
        return -1;
    }
    commands[num_commands].argv[argc] = NULL;
    return num_commands + 1;
}

/**
 * Starts `cmd` in a child process. The child reads its standard input
 * from `in_fd` and writes its standard output to `out_fd`, unless the
 * command redirects them to a file. Does not wait for the child.
 *
 * Returns the child's process ID, or -1 if no child was started.
 */
__attribute__((unused))
static pid_t run_command(command_t *cmd, int in_fd, int out_fd) {
    // TODO (Checkpoints 2 and 3)
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        if (cmd->input_file != NULL) {
            int fd = open(cmd->input_file, O_RDONLY);
            if (fd < 0) {
                perror(cmd->input_file);
                _exit(1);
            }
            if (dup2(fd, STDIN_FILENO) < 0) {
                perror("dup2");
                _exit(1);
            }
            close(fd);
        } else if (in_fd != STDIN_FILENO && dup2(in_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
        if (cmd->output_file != NULL) {
            int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0) {
                perror(cmd->output_file);
                _exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) < 0) {
                perror("dup2");
                _exit(1);
            }
            close(fd);
        } else if (out_fd != STDOUT_FILENO && dup2(out_fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            _exit(1);
        }
        if (in_fd > STDERR_FILENO) {
            close(in_fd);
        }
        if (out_fd > STDERR_FILENO && out_fd != in_fd) {
            close(out_fd);
        }
        extern char **environ;
        execve(cmd->argv[0], cmd->argv, environ);
        perror(cmd->argv[0]);
        _exit(127);
    }
    return pid;
}

/**
 * Runs the `num_commands` commands of a pipeline, with each command's
 * standard output connected to the next command's standard input. Then
 * waits for all of them and prints each one's exit status, in order.
 */
__attribute__((unused))
static void run_pipeline(command_t commands[], int num_commands) {
    // TODO (Checkpoints 2 and 4)
    pid_t children[MAX_COMMANDS];
    int started = 0;
    int in_fd = STDIN_FILENO;
    for (int i = 0; i < num_commands; i++) {
        int fds[2];
        int out_fd = STDOUT_FILENO;
        if (i < num_commands - 1) {
            if (pipe(fds) < 0) {
                perror("pipe");
                break;
            }
            if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0) {
                perror("fcntl");
                close(fds[0]);
                close(fds[1]);
                break;
            }
            out_fd = fds[1];
        }
        pid_t pid = run_command(&commands[i], in_fd, out_fd);
        if (in_fd != STDIN_FILENO) {
            close(in_fd);
        }
        in_fd = STDIN_FILENO;
        if (out_fd != STDOUT_FILENO) {
            close(out_fd);
            if (pid < 0) {
                close(fds[0]);
            } else {
                in_fd = fds[0];
            }
        }
        if (pid < 0) {
            break;
        }
        children[started++] = pid;
    }
    if (in_fd != STDIN_FILENO) {
        close(in_fd);
    }
    int statuses[MAX_COMMANDS];
    for (int i = 0; i < started; i++) {
        int status;
        pid_t result;
        do {
            result = waitpid(children[i], &status, 0);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            perror("waitpid");
            statuses[i] = 1;
        } else if (WIFEXITED(status)) {
            statuses[i] = WEXITSTATUS(status);
        } else {
            statuses[i] = 128 + WTERMSIG(status);
        }
    }
    for (int i = 0; i < started; i++) {
        printf("exit status: %d\n", statuses[i]);
    }
}

/**
 * Runs one line of input: either the exit built-in, or a pipeline of
 * commands. Prints "Invalid command" to stderr if the line is not a valid
 * pipeline.
 */
static void parse_and_run_command(char *line) {
    // TODO (Checkpoint 1): this is not the correct way to check for the
    // exit command. For example, the line "  exit  " should also exit.
    char *tokens[MAX_TOKENS];
    command_t commands[MAX_COMMANDS];
    int num_tokens = tokenize(line, tokens);
    int num_commands = parse_pipeline(tokens, num_tokens, commands);
    if (num_commands < 0) {
        fprintf(stderr, "Invalid command\n");
        return;
    }
    if (num_commands == 1 && strcmp(commands[0].argv[0], "exit") == 0) {
        exit(0);
    }
    run_pipeline(commands, num_commands);
}

int main(void) {
    char line[MAX_LINE];

    while (1) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        // fgets keeps the '\n' at the end of the line. strcspn(line, "\n")
        // returns the index of the first '\n' in line (or of the final '\0'
        // if there is none), so this replaces the newline with '\0'.
        line[strcspn(line, "\n")] = '\0';
        parse_and_run_command(line);
    }

    return 0;
}
