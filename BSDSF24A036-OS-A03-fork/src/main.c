#include "shell.h"

/*
 * Runs one command (which may contain < > |).
 * A simple built-in (cd, exit, ...) runs inside the shell process.
 * Anything else, any line with < > |, and any background command goes to
 * execute(), which does fork-exec(-wait).
 */
static int run_command(char** argv, int background) {
    // Variable assignment(s) like  MSG=hello  or  A=1 B=2 : handled
    // inside the shell (no fork), otherwise the child's copy would be lost.
    int all_assignments = 1;
    for (int i = 0; argv[i] != NULL; i++) {
        if (!is_assignment(argv[i])) { all_assignments = 0; break; }
    }
    if (all_assignments) {
        for (int i = 0; argv[i] != NULL; i++) do_assignment(argv[i]);
        return 0;
    }

    if (!background && !has_operators(argv) && handle_builtin(argv)) {
        return 0;
    }
    return execute(argv, background);
}

/*
 * Splits the tokens of one line at ";" and "&" and runs each part in order.
 *   "a ; b"  -> run a, WAIT for it, then run b
 *   "a & b"  -> start a in the background, then run b straight away
 *   "a &"    -> start a in the background
 * Returns the exit status of the last command run.
 */
static int run_line(char** arglist) {
    char* part[MAXARGS + 1];   // tokens of the current command (pointers only)
    int n = 0;
    int status = 0;

    for (int i = 0; ; i++) {
        char* tok = arglist[i];
        int is_semi = (tok != NULL && strcmp(tok, ";") == 0);
        int is_amp  = (tok != NULL && strcmp(tok, "&") == 0);

        if (tok != NULL && !is_semi && !is_amp) {
            part[n++] = tok;   // ordinary token: add to current command
            continue;
        }

        // Reached ";", "&" or the end of the line: run what we collected
        part[n] = NULL;
        if (n > 0) {
            status = run_command(part, is_amp);
        } else if (is_semi || is_amp) {
            fprintf(stderr, "syntax error near '%s'\n", tok);
            return 1;
        }
        n = 0;

        if (tok == NULL) break;
    }
    return status;
}

/*
 * Tokenizes and runs one command line, then frees the tokens.
 * Used by the main loop and by the if-then-else-fi code.
 * Returns the exit status (0 = success).
 */
int execute_line(char* line) {
    char** arglist = tokenize(line);
    if (arglist == NULL) {
        return 0;   // empty line
    }

    // Expansion step: replace $NAME with its value BEFORE running anything
    expand_variables(arglist);

    int status = run_line(arglist);

    for (int i = 0; arglist[i] != NULL; i++) {
        free(arglist[i]);
    }
    free(arglist);
    return status;
}

int main() {
    char* cmdline;

    init_readline();

    while (1) {
        // Reap finished background jobs (zombies) before every prompt
        reap_jobs();

        // readline() prints the prompt, handles line editing, arrow keys and
        // Tab, and returns a malloc'd line (NULL on Ctrl+D).
        cmdline = readline(PROMPT);
        if (cmdline == NULL) {
            break;
        }

        // !n must be handled before tokenizing and before saving to history:
        // the line is replaced by command n and then runs as if typed.
        if (cmdline[0] == '!') {
            char* expanded = expand_history(cmdline);
            free(cmdline);
            if (expanded == NULL) {
                continue;   // invalid or out of range; error already printed
            }
            cmdline = expanded;
            printf("%s\n", cmdline);   // show which command is being re-run
        }

        add_to_history(cmdline);       // our own list, used by "history" and !n
        if (cmdline[0] != '\0') {
            add_history(cmdline);      // readline's list, used by Up/Down arrows
        }

        if (is_if_line(cmdline)) {
            run_if_block(cmdline);     // reads more lines up to "fi"
        } else {
            execute_line(cmdline);
        }
        free(cmdline);
    }

    printf("\nShell exited.\n");
    return 0;
}
