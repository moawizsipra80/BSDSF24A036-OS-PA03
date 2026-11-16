#ifndef SHELL_H
#define SHELL_H
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LEN 4096
#define MAXARGS 128
#define HISTORY_SIZE 20
#define MAX_JOBS 64
#define PROMPT "FCIT> "

typedef struct {
    pid_t pid;
    char *command;
} Job;

typedef struct Variable {
    char *name;
    char *value;
    struct Variable *next;
} Variable;

typedef struct {
    char *history[HISTORY_SIZE];
    int history_count;
    int history_start;
    Job jobs[MAX_JOBS];
    int job_count;
    Variable *variables;
    int should_exit;
} ShellState;

char *read_cmd(const char *prompt);
char **shell_completion(const char *text, int start, int end);
char **tokenize(const char *cmdline);
void free_tokens(char **tokens);
int execute_line(char *line, ShellState *state);
int execute_command_tokens(char **tokens, ShellState *state);
int handle_builtin(char **args, ShellState *state, int in_child);
void reap_jobs(ShellState *state);
void free_state(ShellState *state);
char *expand_variables(char *value, ShellState *state);
int execute_if_block(ShellState *state, char *first_line);
void add_history_entry(ShellState *state, const char *line);
int resolve_history(char **line, ShellState *state);

#endif
