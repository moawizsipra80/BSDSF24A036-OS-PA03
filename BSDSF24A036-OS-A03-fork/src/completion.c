#include "shell.h"
#include <dirent.h>

/*
 * Tab completion with GNU Readline.
 *
 * - First word on the line  -> complete command names
 *                              (our built-ins + executables found in $PATH)
 * - Any later word          -> complete file names (readline's default)
 */

static const char* builtins[] = { "cd", "exit", "help", "jobs", "history", "set", NULL };

/*
 * Generator function called repeatedly by readline.
 * state == 0 on the first call for a new word: reset and start scanning.
 * Each call returns ONE malloc'd match, or NULL when there are no more.
 * (readline frees the strings we return.)
 */
static char* command_generator(const char* text, int state) {
    static int builtin_index;
    static char* path_copy;       // our copy of $PATH, cut up by strtok_r
    static char* saveptr;
    static char* current_dir;
    static DIR* dir;
    static size_t len;

    if (state == 0) {
        builtin_index = 0;
        len = strlen(text);
        free(path_copy);
        char* path = getenv("PATH");
        path_copy = path ? strdup(path) : NULL;
        current_dir = path_copy ? strtok_r(path_copy, ":", &saveptr) : NULL;
        if (dir) closedir(dir);
        dir = NULL;
    }

    // 1. Built-in commands
    while (builtins[builtin_index] != NULL) {
        const char* name = builtins[builtin_index++];
        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }

    // 2. Executables in each directory of $PATH
    while (current_dir != NULL) {
        if (dir == NULL)
            dir = opendir(current_dir);

        struct dirent* entry;
        while (dir != NULL && (entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, text, len) != 0)
                continue;

            // Only offer files we are allowed to execute
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", current_dir, entry->d_name);
            if (access(full, X_OK) == 0)
                return strdup(entry->d_name);
        }

        if (dir) closedir(dir);
        dir = NULL;
        current_dir = strtok_r(NULL, ":", &saveptr);
    }

    return NULL; // no more matches
}

/*
 * Called by readline when Tab is pressed.
 * start is the position of the word being completed in rl_line_buffer.
 */
static char** shell_completion(const char* text, int start, int end) {
    (void)end;

    // Only the first word is a command (skip leading spaces before it)
    int i = 0;
    while (i < start && (rl_line_buffer[i] == ' ' || rl_line_buffer[i] == '\t'))
        i++;

    if (i == start)
        return rl_completion_matches(text, command_generator);

    // Returning NULL lets readline fall back to its default filename completion
    return NULL;
}

void init_readline(void) {
    rl_attempted_completion_function = shell_completion;
}
