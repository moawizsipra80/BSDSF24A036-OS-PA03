#include "shell.h"

/*
 * One command of a pipeline, e.g. for   sort < in.txt | uniq > out.txt
 *   cmd 0: argv = {"sort", NULL}, infile = "in.txt", outfile = NULL
 *   cmd 1: argv = {"uniq", NULL}, infile = NULL,     outfile = "out.txt"
 * The strings point into arglist (no copies), so nothing extra to free.
 */
typedef struct {
    char* argv[MAXARGS + 1];
    char* infile;    // from "< file", or NULL
    char* outfile;   // from "> file", or NULL
} command_t;

// Returns 1 if the line uses <, > or |
int has_operators(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        if (strcmp(arglist[i], "<") == 0 || strcmp(arglist[i], ">") == 0 ||
            strcmp(arglist[i], "|") == 0)
            return 1;
    }
    return 0;
}

/*
 * Splits arglist at "|" into commands and pulls out "< file" / "> file".
 * Returns the number of commands, or -1 on a syntax error.
 */
static int parse_pipeline(char** arglist, command_t cmds[]) {
    int ncmds = 0;
    int argc = 0;
    cmds[0].infile = cmds[0].outfile = NULL;

    for (int i = 0; arglist[i] != NULL; i++) {
        char* tok = arglist[i];

        if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0) {
            char* file = arglist[i + 1];
            if (file == NULL || strcmp(file, "<") == 0 || strcmp(file, ">") == 0 ||
                strcmp(file, "|") == 0) {
                fprintf(stderr, "syntax error: expected a file name after '%s'\n", tok);
                return -1;
            }
            if (tok[0] == '<') cmds[ncmds].infile = file;
            else               cmds[ncmds].outfile = file;
            i++;   // skip the file name, it is not an argument
        } else if (strcmp(tok, "|") == 0) {
            if (argc == 0) {
                fprintf(stderr, "syntax error near '|'\n");
                return -1;
            }
            if (ncmds + 1 == MAX_CMDS) {
                fprintf(stderr, "too many commands in pipeline (max %d)\n", MAX_CMDS);
                return -1;
            }
            cmds[ncmds].argv[argc] = NULL;   // finish this command
            ncmds++;
            argc = 0;
            cmds[ncmds].infile = cmds[ncmds].outfile = NULL;
        } else {
            cmds[ncmds].argv[argc++] = tok;  // normal argument
        }
    }

    if (argc == 0) {   // e.g. "ls |" or "> out" with no command
        fprintf(stderr, "syntax error: missing command\n");
        return -1;
    }
    cmds[ncmds].argv[argc] = NULL;
    return ncmds + 1;
}

/*
 * Runs in the CHILD, after fork() and before execvp():
 * open the file and dup2() it onto stdin (0) or stdout (1).
 */
static void apply_redirection(command_t* cmd) {
    if (cmd->infile != NULL) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            perror(cmd->infile);
            exit(1);
        }
        dup2(fd, STDIN_FILENO);   // stdin now reads from the file
        close(fd);                // fd 0 still refers to the file
    }
    if (cmd->outfile != NULL) {
        // create if missing, empty it if it exists; permissions rw-r--r--
        int fd = open(cmd->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror(cmd->outfile);
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);  // stdout now writes to the file
        close(fd);
    }
}

/*
 * Runs one command, or a pipeline cmd0 | cmd1 | ... | cmdN-1.
 * For N commands we create N-1 pipes and fork N children:
 *   child i reads from pipe i-1 (if i > 0) and writes to pipe i (if i < N-1).
 *
 * background == 0: wait for the children; return the exit status of the
 *                  last command (0 = success).
 * background == 1: do NOT wait; record the job and return 0 immediately.
 *                  The children are reaped later by reap_jobs().
 */
int execute(char** arglist, int background) {
    command_t cmds[MAX_CMDS];
    int ncmds = parse_pipeline(arglist, cmds);
    if (ncmds < 0) {
        return -1;
    }

    int pipes[MAX_CMDS - 1][2];   // pipes[i][0] = read end, pipes[i][1] = write end
    for (int i = 0; i < ncmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            for (int j = 0; j < i; j++) { close(pipes[j][0]); close(pipes[j][1]); }
            return -1;
        }
    }

    // Flush our own output so a child does not inherit and repeat it
    fflush(stdout);

    pid_t pids[MAX_CMDS];
    int started = 0;
    for (int i = 0; i < ncmds; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            break;
        }
        if (pid == 0) {
            // ---------- CHILD ----------
            if (i > 0)          dup2(pipes[i - 1][0], STDIN_FILENO);  // read from previous command
            if (i < ncmds - 1)  dup2(pipes[i][1], STDOUT_FILENO);     // write to next command

            // Close ALL pipe fds in the child. Otherwise a reader never sees
            // EOF, because a write end would still be open somewhere.
            for (int j = 0; j < ncmds - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            // File redirection comes after the pipe setup, so "> file" wins
            apply_redirection(&cmds[i]);

            // Built-ins like help/history can also be piped or redirected
            if (handle_builtin(cmds[i].argv)) {
                exit(0);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "%s: command not found\n", cmds[i].argv[0]);
            exit(127);
        }
        pids[started++] = pid;   // ---------- PARENT ----------
    }

    // The parent uses no pipe ends: close them all so readers get EOF
    for (int i = 0; i < ncmds - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (started < ncmds) {   // a fork failed: clean up what did start
        for (int i = 0; i < started; i++) waitpid(pids[i], NULL, 0);
        return -1;
    }

    if (background) {
        // Parent does not wait: remember the job and go back to the prompt
        add_job(pids, started, arglist);
        return 0;
    }

    // Foreground: wait for every child in the pipeline
    int status = 0;
    for (int i = 0; i < started; i++) {
        waitpid(pids[i], &status, 0);
    }

    // Exit status of the last command (status of the whole pipeline)
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + WTERMSIG(status);   // killed by a signal
}
