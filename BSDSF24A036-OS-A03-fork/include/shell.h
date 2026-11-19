#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LEN 512
#define MAXARGS 64     
#define MAX_CMDS 16    // max commands in one pipeline (a | b | c ...)
#define MAX_JOBS 32    // max background jobs tracked at once
#define PROMPT "FCIT> "
#define HISTORY_SIZE 20

// Function prototypes
char** tokenize(char* cmdline);
int execute(char** arglist, int background);
int has_operators(char** arglist);
int handle_builtin(char** arglist);

// History (history.c)
void add_to_history(const char* cmdline);
void show_history(void);
char* expand_history(const char* cmdline);

// Runs one full command line (tokenize + ; & | < >), returns exit status (main.c)
int execute_line(char* line);

// if-then-else-fi (control.c)
int is_if_line(char* line);
int run_if_block(char* first_line);

// Shell variables (vars.c)
void set_var(const char* name, const char* value);
const char* get_var(const char* name);
void print_vars(void);
int is_assignment(const char* token);
void do_assignment(const char* token);
void expand_variables(char** arglist);

// Background jobs (jobs.c)
void add_job(pid_t pids[], int npids, char** argv);
void reap_jobs(void);
void print_jobs(void);

// Readline setup and Tab completion (completion.c)
void init_readline(void);

#endif // SHELL_H
