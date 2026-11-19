#include "shell.h"

// Characters that are tokens on their own, even without spaces around them
static int is_operator(char c) {
    return c == '<' || c == '>' || c == '|' || c == ';' || c == '&';
}

/*
 * Splits a command line into a NULL-terminated array of malloc'd tokens.
 *   - Words are separated by spaces/tabs.
 *   - <, >, |, ; and & always become separate tokens, so "ls>out" works too.
 *   - Text inside "double" or 'single' quotes stays in one token and the
 *     quotes are removed, so  grep "root"  passes  root  to grep.
 * Returns NULL for an empty line or if there are too many tokens.
 */
char** tokenize(char* cmdline) {
    if (cmdline == NULL) {
        return NULL;
    }

    char** arglist = malloc(sizeof(char*) * (MAXARGS + 1));
    int argnum = 0;
    char* cp = cmdline;
    char buf[MAX_LEN];   // builds the current token

    while (*cp != '\0') {
        while (*cp == ' ' || *cp == '\t') cp++;   // skip whitespace
        if (*cp == '\0') break;

        if (argnum == MAXARGS) {
            fprintf(stderr, "Too many arguments (max %d)\n", MAXARGS);
            for (int i = 0; i < argnum; i++) free(arglist[i]);
            free(arglist);
            return NULL;
        }

        int len = 0;
        if (is_operator(*cp)) {
            buf[len++] = *cp++;                   // operator is a 1-char token
        } else {
            while (*cp != '\0' && *cp != ' ' && *cp != '\t' && !is_operator(*cp)) {
                if (*cp == '"' || *cp == '\'') {
                    char quote = *cp++;           // skip opening quote
                    while (*cp != '\0' && *cp != quote) {
                        if (len < MAX_LEN - 1) buf[len++] = *cp;
                        cp++;
                    }
                    if (*cp == quote) cp++;       // skip closing quote
                } else {
                    if (len < MAX_LEN - 1) buf[len++] = *cp;
                    cp++;
                }
            }
        }
        buf[len] = '\0';
        arglist[argnum++] = strdup(buf);          // exact-size copy
    }

    if (argnum == 0) {   // empty or whitespace-only line
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}
/*
 * handle_builtin()
 * Checks whether arglist[0] is a built-in command. If it is, runs it
 * inside the shell process and returns 1. Otherwise returns 0 so the
 * caller runs it as an external command with fork-exec-wait.
 *
 * cd and exit must be built-ins: a child process changing its own
 * directory or exiting has no effect on the parent shell.
 */
int handle_builtin(char** arglist) {
    char* cmd = arglist[0];

    if (strcmp(cmd, "exit") == 0) {
        printf("Shell exited.\n");
        exit(0);
    }

    if (strcmp(cmd, "cd") == 0) {
        // "cd" with no argument goes to the HOME directory, like bash
        char* dir = arglist[1];
        if (dir == NULL) {
            dir = getenv("HOME");
            if (dir == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }
        }
        if (chdir(dir) != 0) {
            perror("cd");
        }
        return 1;
    }

    if (strcmp(cmd, "help") == 0) {
        printf("Built-in commands:\n");
        printf("  cd <directory>  Change the current working directory\n");
        printf("  exit            Exit the shell\n");
        printf("  help            Show this help message\n");
        printf("  jobs            List background jobs\n");
        printf("  history         Show the last %d commands\n", HISTORY_SIZE);
        printf("  !n              Re-run command number n from history\n");
        printf("  set             List all shell variables and their values\n");
        printf("\nVariables:\n");
        printf("  NAME=value      Assign a value to a shell variable\n");
        printf("  $NAME           Replaced by the variable's value\n");
        printf("\nControl structure:\n");
        printf("  if cmd / then / else / fi   Run a block based on cmd's exit status\n");
        printf("\nOperators:\n");
        printf("  cmd < file      Read input from file\n");
        printf("  cmd > file      Write output to file\n");
        printf("  cmd1 | cmd2     Pipe output of cmd1 into cmd2\n");
        printf("  cmd1 ; cmd2     Run cmd1, then cmd2\n");
        printf("  cmd &           Run cmd in the background\n");
        return 1;
    }

    if (strcmp(cmd, "history") == 0) {
        show_history();
        return 1;
    }

    if (strcmp(cmd, "set") == 0) {
        print_vars();
        return 1;
    }

    if (strcmp(cmd, "jobs") == 0) {
        print_jobs();
        return 1;
    }

    return 0; // not a built-in
}
