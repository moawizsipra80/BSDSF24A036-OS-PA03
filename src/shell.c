#include "shell.h"
#include <dirent.h>

char *read_cmd(const char *prompt) {
    static int history_initialized;
    if (!history_initialized) {
        using_history();
        stifle_history(HISTORY_SIZE);
        history_initialized = 1;
    }
    rl_attempted_completion_function = shell_completion;
    return readline(prompt);
}

static char **command_names;
static size_t command_name_count;
static size_t command_name_index;

static char *command_generator(const char *text, int state) {
    if (state == 0) {
        char *path = getenv("PATH");
        command_name_count = 0;
        command_name_index = 0;
        if (path != NULL) {
            char *path_copy = strdup(path);
            char *part = strtok(path_copy, ":");
            while (part != NULL) {
                DIR *directory = opendir(part);
                if (directory != NULL) {
                    struct dirent *entry;
                    while ((entry = readdir(directory)) != NULL) {
                        if (strncmp(entry->d_name, text, strlen(text)) == 0) {
                            char **grown = realloc(command_names, (command_name_count + 1) * sizeof(char *));
                            if (grown != NULL) {
                                command_names = grown;
                                command_names[command_name_count++] = strdup(entry->d_name);
                            }
                        }
                    }
                    closedir(directory);
                }
                part = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }
    if (command_name_index < command_name_count) {
        return strdup(command_names[command_name_index++]);
    }
    for (size_t i = 0; i < command_name_count; i++) free(command_names[i]);
    free(command_names);
    command_names = NULL;
    command_name_count = 0;
    return NULL;
}

char **shell_completion(const char *text, int start, int end) {
    (void)end;
    rl_completion_append_character = ' ';
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    return rl_completion_matches(text, rl_filename_completion_function);
}

static int token_is_operator(char c) {
    return c == '<' || c == '>' || c == '|' || c == '&';
}

char **tokenize(const char *cmdline) {
    char **tokens = calloc(MAXARGS + 1, sizeof(char *));
    int count = 0;
    int i = 0;

    if (tokens == NULL) {
        perror("calloc");
        return NULL;
    }

    while (cmdline != NULL && cmdline[i] != '\0') {
        char buffer[MAX_LEN];
        int length = 0;
        char quote = '\0';

        while (cmdline[i] == ' ' || cmdline[i] == '\t') {
            i++;
        }
        if (cmdline[i] == '\0') {
            break;
        }
        if (count == MAXARGS) {
            fprintf(stderr, "Too many arguments\n");
            free_tokens(tokens);
            return NULL;
        }

        if (token_is_operator(cmdline[i])) {
            buffer[length++] = cmdline[i++];
        } else {
            while (cmdline[i] != '\0') {
                char current = cmdline[i];
                if (quote != '\0') {
                    if (current == quote) {
                        quote = '\0';
                    } else if (length < MAX_LEN - 1) {
                        buffer[length++] = current;
                    }
                    i++;
                    continue;
                }
                if (current == '\'' || current == '"') {
                    quote = current;
                    i++;
                    continue;
                }
                if (current == ' ' || current == '\t' || token_is_operator(current)) {
                    break;
                }
                if (length < MAX_LEN - 1) {
                    buffer[length++] = current;
                }
                i++;
            }
        }
        buffer[length] = '\0';
        tokens[count] = strdup(buffer);
        if (tokens[count] == NULL) {
            perror("strdup");
            free_tokens(tokens);
            return NULL;
        }
        count++;
    }
    return tokens;
}

void free_tokens(char **tokens) {
    if (tokens == NULL) {
        return;
    }
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

static char *lookup_variable(const char *name, ShellState *state) {
    for (Variable *variable = state->variables; variable != NULL; variable = variable->next) {
        if (strcmp(variable->name, name) == 0) {
            return variable->value;
        }
    }
    return getenv(name);
}

char *expand_variables(char *value, ShellState *state) {
    if (value == NULL || value[0] != '$') {
        return value;
    }
    char *name = value + 1;
    char *replacement = lookup_variable(name, state);
    if (replacement == NULL) {
        return strdup("");
    }
    return strdup(replacement);
}

static int split_commands(const char *line, char ***commands) {
    int count = 0;
    int capacity = 8;
    int start = 0;
    char quote = '\0';
    char **result = calloc((size_t)capacity, sizeof(char *));

    if (result == NULL) {
        return -1;
    }
    for (int i = 0;; i++) {
        char current = line[i];
        if (quote != '\0') {
            if (current == quote) {
                quote = '\0';
            }
        } else if (current == '\'' || current == '"') {
            quote = current;
        } else if (current == ';' || current == '\0') {
            int length = i - start;
            while (length > 0 && (line[start + length - 1] == ' ' || line[start + length - 1] == '\t')) {
                length--;
            }
            while (length > 0 && (line[start] == ' ' || line[start] == '\t')) {
                start++;
                length--;
            }
            if (length > 0) {
                if (count + 1 == capacity) {
                    capacity *= 2;
                    char **grown = realloc(result, (size_t)capacity * sizeof(char *));
                    if (grown == NULL) {
                        for (int j = 0; j < count; j++) free(result[j]);
                        free(result);
                        return -1;
                    }
                    result = grown;
                }
                result[count] = strndup(line + start, (size_t)length);
                if (result[count] == NULL) {
                    for (int j = 0; j < count; j++) free(result[j]);
                    free(result);
                    return -1;
                }
                count++;
            }
            if (current == '\0') {
                break;
            }
            start = i + 1;
        }
    }
    *commands = result;
    return count;
}

void add_history_entry(ShellState *state, const char *line) {
    if (line == NULL || line[0] == '\0') {
        return;
    }
    if (state->history_count < HISTORY_SIZE) {
        int index = (state->history_start + state->history_count) % HISTORY_SIZE;
        state->history[index] = strdup(line);
        state->history_count++;
    } else {
        free(state->history[state->history_start]);
        state->history[state->history_start] = strdup(line);
        state->history_start = (state->history_start + 1) % HISTORY_SIZE;
    }
    add_history(line);
}

static char *history_lookup(ShellState *state, int number) {
    if (number < 1 || number > state->history_count) {
        return NULL;
    }
    int index = (state->history_start + number - 1) % HISTORY_SIZE;
    return state->history[index];
}

int resolve_history(char **line, ShellState *state) {
    if (*line == NULL || (*line)[0] != '!') {
        return 0;
    }
    char *end;
    long number = strtol(*line + 1, &end, 10);
    if (*end != '\0' || number < 1) {
        fprintf(stderr, "Invalid history reference\n");
        return -1;
    }
    char *saved = history_lookup(state, (int)number);
    if (saved == NULL) {
        fprintf(stderr, "History entry not found\n");
        return -1;
    }
    char *replacement = strdup(saved);
    if (replacement == NULL) {
        perror("strdup");
        return -1;
    }
    printf("%s\n", replacement);
    free(*line);
    *line = replacement;
    return 1;
}

int execute_if_block(ShellState *state, char *first_line) {
    char **then_lines = calloc(MAXARGS, sizeof(char *));
    char **else_lines = calloc(MAXARGS, sizeof(char *));
    int then_count = 0;
    int else_count = 0;
    int in_else = 0;
    int condition_status;
    int selected_status;
    char *line;

    if (then_lines == NULL || else_lines == NULL) {
        perror("calloc");
        free(then_lines);
        free(else_lines);
        return 1;
    }
    while ((line = read_cmd("...> ")) != NULL) {
        if (strcmp(line, "then") == 0) {
            free(line);
            continue;
        }
        if (strcmp(line, "else") == 0) {
            in_else = 1;
            free(line);
            continue;
        }
        if (strcmp(line, "fi") == 0) {
            free(line);
            break;
        }
        char **target = in_else ? else_lines : then_lines;
        int *target_count = in_else ? &else_count : &then_count;
        if (*target_count < MAXARGS - 1) {
            target[(*target_count)++] = line;
        } else {
            free(line);
            fprintf(stderr, "Too many lines in if block\n");
        }
    }
    condition_status = execute_line(first_line, state);
    char **selected = condition_status == 0 ? then_lines : else_lines;
    int selected_count = condition_status == 0 ? then_count : else_count;
    selected_status = condition_status;
    for (int i = 0; i < selected_count && !state->should_exit; i++) {
        selected_status = execute_line(selected[i], state);
    }
    condition_status = selected_status;
    for (int i = 0; i < then_count; i++) free(then_lines[i]);
    for (int i = 0; i < else_count; i++) free(else_lines[i]);
    free(then_lines);
    free(else_lines);
    return condition_status;
}

int execute_line(char *line, ShellState *state) {
    char **commands = NULL;
    int count;
    int status = 0;

    if (line == NULL) {
        return 0;
    }
    count = split_commands(line, &commands);
    if (count < 0) {
        perror("split");
        return 1;
    }
    for (int i = 0; i < count && !state->should_exit; i++) {
        char *command = commands[i];
        char **tokens = tokenize(command);
        if (tokens != NULL && tokens[0] != NULL) {
            status = execute_command_tokens(tokens, state);
            free_tokens(tokens);
        }
    }
    for (int i = 0; i < count; i++) free(commands[i]);
    free(commands);
    return status;
}

void free_state(ShellState *state) {
    for (int i = 0; i < state->history_count; i++) {
        int index = (state->history_start + i) % HISTORY_SIZE;
        free(state->history[index]);
    }
    for (Variable *variable = state->variables; variable != NULL;) {
        Variable *next = variable->next;
        free(variable->name);
        free(variable->value);
        free(variable);
        variable = next;
    }
    for (int i = 0; i < state->job_count; i++) {
        free(state->jobs[i].command);
    }
}
