#include "shell.h"

/*
 * Command history stored as a circular buffer.
 *
 * history_count is the total number of commands ever added. Command
 * number n (1-based, like bash) is stored at index (n - 1) % HISTORY_SIZE.
 * Once more than HISTORY_SIZE commands are added, each new command
 * overwrites the oldest one, so only the last HISTORY_SIZE are kept.
 */
static char* history[HISTORY_SIZE];
static int history_count = 0;

// Number of the oldest command still in the buffer
static int oldest_number(void) {
    if (history_count <= HISTORY_SIZE)
        return 1;
    return history_count - HISTORY_SIZE + 1;
}

// Returns 1 if the line contains only spaces/tabs (or is empty)
static int is_blank(const char* line) {
    for (; *line != '\0'; line++) {
        if (*line != ' ' && *line != '\t')
            return 0;
    }
    return 1;
}

void add_to_history(const char* cmdline) {
    if (cmdline == NULL || is_blank(cmdline))
        return;

    int index = history_count % HISTORY_SIZE;
    free(history[index]);              // free the oldest entry if overwriting (free(NULL) is safe)
    history[index] = strdup(cmdline);  // keep our own copy; main() frees cmdline
    history_count++;
}

void show_history(void) {
    for (int n = oldest_number(); n <= history_count; n++) {
        printf("%5d  %s\n", n, history[(n - 1) % HISTORY_SIZE]);
    }
}

/*
 * Handles the !n syntax. cmdline must start with '!'.
 * Returns a newly malloc'd copy of command number n, or NULL (after
 * printing an error) if n is not a valid number or is out of range.
 */
char* expand_history(const char* cmdline) {
    char* end;
    long n = strtol(cmdline + 1, &end, 10);

    // Reject "!", "!abc", "!3x", etc. (allow trailing spaces)
    if (end == cmdline + 1 || !is_blank(end)) {
        fprintf(stderr, "%s: invalid history reference (use !n)\n", cmdline);
        return NULL;
    }

    if (n < oldest_number() || n > history_count) {
        fprintf(stderr, "!%ld: event not found\n", n);
        return NULL;
    }

    return strdup(history[(n - 1) % HISTORY_SIZE]);
}
