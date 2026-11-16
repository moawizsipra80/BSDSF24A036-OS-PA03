#include "shell.h"

static void print_help(void) {
    printf("Built-in commands:\n");
    printf("  cd <directory>  Change the current directory\n");
    printf("  exit            Exit the shell\n");
    printf("  help            Show this help message\n");
    printf("  history         Show the command history\n");
    printf("  jobs            Show background jobs\n");
    printf("  set             Show shell variables\n");
}

static int set_variable(const char *assignment, ShellState *state) {
    const char *separator = strchr(assignment, '=');
    if (separator == NULL || separator == assignment) {
        return 0;
    }
    size_t name_length = (size_t)(separator - assignment);
    for (size_t i = 0; i < name_length; i++) {
        if ((assignment[i] < 'A' || assignment[i] > 'Z') &&
            (assignment[i] < 'a' || assignment[i] > 'z') &&
            (assignment[i] < '0' || assignment[i] > '9') &&
            assignment[i] != '_') {
            return 0;
        }
    }
    char *name = strndup(assignment, name_length);
    char *value = strdup(separator + 1);
    if (name == NULL || value == NULL) {
        perror("strdup");
        free(name);
        free(value);
        return -1;
    }
    for (Variable *variable = state->variables; variable != NULL; variable = variable->next) {
        if (strcmp(variable->name, name) == 0) {
            free(variable->value);
            variable->value = value;
            free(name);
            return 1;
        }
    }
    Variable *variable = calloc(1, sizeof(*variable));
    if (variable == NULL) {
        perror("calloc");
        free(name);
        free(value);
        return -1;
    }
    variable->name = name;
    variable->value = value;
    variable->next = state->variables;
    state->variables = variable;
    return 1;
}

static void print_history(ShellState *state) {
    for (int i = 0; i < state->history_count; i++) {
        int index = (state->history_start + i) % HISTORY_SIZE;
        printf("%d %s\n", i + 1, state->history[index]);
    }
}

static void print_jobs(ShellState *state) {
    for (int i = 0; i < state->job_count; i++) {
        printf("[%d] %s\n", state->jobs[i].pid, state->jobs[i].command);
    }
}

int handle_builtin(char **args, ShellState *state, int in_child) {
    if (args == NULL || args[0] == NULL) {
        return 0;
    }
    if (strcmp(args[0], "exit") == 0) {
        if (!in_child) state->should_exit = 1;
        return 1;
    }
    if (strcmp(args[0], "cd") == 0) {
        const char *directory = args[1] == NULL ? getenv("HOME") : args[1];
        if (directory == NULL || chdir(directory) != 0) {
            perror("cd");
        }
        return 1;
    }
    if (strcmp(args[0], "help") == 0) {
        print_help();
        return 1;
    }
    if (strcmp(args[0], "history") == 0) {
        print_history(state);
        return 1;
    }
    if (strcmp(args[0], "jobs") == 0) {
        print_jobs(state);
        return 1;
    }
    if (strcmp(args[0], "set") == 0) {
        for (Variable *variable = state->variables; variable != NULL; variable = variable->next) {
            printf("%s=%s\n", variable->name, variable->value);
        }
        return 1;
    }
    return 0;
}

static int expand_args(char **args, ShellState *state) {
    for (int i = 0; args[i] != NULL; i++) {
        if (args[i][0] == '$') {
            char *expanded = expand_variables(args[i], state);
            if (expanded == NULL) {
                return -1;
            }
            free(args[i]);
            args[i] = expanded;
        }
    }
    return 0;
}

static void add_job(ShellState *state, pid_t pid, const char *command) {
    if (state->job_count == MAX_JOBS) {
        fprintf(stderr, "Job list is full\n");
        return;
    }
    state->jobs[state->job_count].pid = pid;
    state->jobs[state->job_count].command = strdup(command);
    state->job_count++;
}

void reap_jobs(ShellState *state) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < state->job_count; i++) {
            if (state->jobs[i].pid == pid) {
                free(state->jobs[i].command);
                state->jobs[i] = state->jobs[state->job_count - 1];
                state->job_count--;
                break;
            }
        }
    }
}

static int execute_pipeline(char **tokens, ShellState *state, const char *command) {
    char **stages[MAXARGS];
    int stage_count = 0;
    int start = 0;
    int background = 0;
    pid_t pids[MAXARGS];
    int previous_read = -1;
    int status = 0;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strcmp(tokens[i], "&") == 0 && tokens[i + 1] == NULL) {
            background = 1;
            free(tokens[i]);
            tokens[i] = NULL;
        }
        if (tokens[i] != NULL && strcmp(tokens[i], "|") == 0) {
            free(tokens[i]);
            tokens[i] = NULL;
            stages[stage_count++] = &tokens[start];
            start = i + 1;
        }
    }
    stages[stage_count++] = &tokens[start];
    for (int stage = 0; stage < stage_count; stage++) {
        int pipe_fds[2] = {-1, -1};
        char *input_file = NULL;
        char *output_file = NULL;
        char **args = stages[stage];
        int argc = 0;

        while (args[argc] != NULL) {
            if (strcmp(args[argc], "<") == 0 || strcmp(args[argc], ">") == 0) {
                int output = args[argc][0] == '>';
                char **file = output ? &output_file : &input_file;
                if (args[argc + 1] == NULL) {
                    fprintf(stderr, "Redirection requires a filename\n");
                    return 1;
                }
                *file = strdup(args[argc + 1]);
                if (*file == NULL) {
                    perror("strdup");
                    return 1;
                }
                free(args[argc]);
                free(args[argc + 1]);
                int remaining = 0;
                while (args[argc + 2 + remaining] != NULL) {
                    args[argc + remaining] = args[argc + 2 + remaining];
                    remaining++;
                }
                args[argc + remaining] = NULL;
                continue;
            }
            argc++;
        }
        if (expand_args(args, state) != 0) {
            return 1;
        }
        if (stage < stage_count - 1 && pipe(pipe_fds) != 0) {
            perror("pipe");
            return 1;
        }
        pids[stage] = fork();
        if (pids[stage] < 0) {
            perror("fork");
            return 1;
        }
        if (pids[stage] == 0) {
            if (previous_read != -1) dup2(previous_read, STDIN_FILENO);
            if (pipe_fds[1] != -1) dup2(pipe_fds[1], STDOUT_FILENO);
            if (input_file != NULL) {
                int fd = open(input_file, O_RDONLY);
                if (fd < 0) {
                    perror(input_file);
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (output_file != NULL) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror(output_file);
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            if (previous_read != -1) close(previous_read);
            if (pipe_fds[0] != -1) close(pipe_fds[0]);
            if (pipe_fds[1] != -1) close(pipe_fds[1]);
            if (args[0] == NULL) _exit(0);
            if (!handle_builtin(args, state, 1)) {
                execvp(args[0], args);
                perror(args[0]);
                _exit(127);
            }
            _exit(0);
        }
        if (previous_read != -1) close(previous_read);
        if (pipe_fds[1] != -1) close(pipe_fds[1]);
        previous_read = pipe_fds[0];
        free(input_file);
        free(output_file);
    }
    if (previous_read != -1) close(previous_read);
    if (background) {
        add_job(state, pids[stage_count - 1], command);
        for (int stage = 0; stage < stage_count - 1; stage++) waitpid(pids[stage], NULL, 0);
        return 0;
    }
    for (int stage = 0; stage < stage_count; stage++) {
        if (waitpid(pids[stage], &status, 0) < 0) {
            perror("waitpid");
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int execute_command_tokens(char **tokens, ShellState *state) {
    int assignment_status;
    int has_operator = 0;
    if (tokens[0] == NULL) {
        return 0;
    }
    assignment_status = set_variable(tokens[0], state);
    if (assignment_status != 0) {
        return assignment_status < 0 ? 1 : 0;
    }
    if (strchr(tokens[0], '=') == NULL) {
        if (expand_args(tokens, state) != 0) return 1;
        for (int i = 0; tokens[i] != NULL; i++) {
            if (strcmp(tokens[i], "<") == 0 || strcmp(tokens[i], ">") == 0 ||
                strcmp(tokens[i], "|") == 0 || strcmp(tokens[i], "&") == 0) {
                has_operator = 1;
                break;
            }
        }
        if (!has_operator && handle_builtin(tokens, state, 0)) {
            return 0;
        }
    }
    char *command = tokens[0];
    return execute_pipeline(tokens, state, command);
}
