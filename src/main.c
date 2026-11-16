#include "shell.h"

static int is_if_line(const char *line) {
    return strncmp(line, "if ", 3) == 0 || strcmp(line, "if") == 0;
}

int main(void) {
    ShellState state = {0};
    char *line;

    while (!state.should_exit) {
        reap_jobs(&state);
        line = read_cmd(PROMPT);
        if (line == NULL) {
            putchar('\n');
            break;
        }
        if (line[0] == '\0') {
            free(line);
            continue;
        }
        if (is_if_line(line)) {
            char *condition = line + 2;
            while (*condition == ' ' || *condition == '\t') condition++;
            char *saved = strdup(condition);
            free(line);
            if (saved == NULL) {
                perror("strdup");
                break;
            }
            execute_if_block(&state, saved);
            free(saved);
            continue;
        }
        int history_result = resolve_history(&line, &state);
        if (history_result < 0) {
            free(line);
            continue;
        }
        add_history_entry(&state, line);
        execute_line(line, &state);
        free(line);
    }
    free_state(&state);
    printf("Shell exited.\n");
    return 0;
}
