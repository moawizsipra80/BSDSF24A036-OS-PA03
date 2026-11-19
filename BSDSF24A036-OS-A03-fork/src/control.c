#include "shell.h"

/*
 * The if-then-else-fi control structure:
 *
 *     if <command>
 *     then
 *         <commands run if <command> exits with status 0>
 *     else                  (optional)
 *         <commands run otherwise>
 *     fi
 *
 * When the main loop reads a line starting with "if", it calls
 * run_if_block(). We then keep reading lines ourselves (prompt "> ")
 * until "fi", storing the then/else lines in two separate arrays.
 * Only after "fi" do we run anything.
 */

#define MAX_BLOCK_LINES 64

// Where we are inside the if block while reading lines
enum if_state { EXPECT_THEN, IN_THEN, IN_ELSE };

// Skips leading spaces/tabs
static char* skip_space(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Returns 1 if line (ignoring leading spaces) is exactly the word `word`
// or starts with `word` followed by a space/tab.
static int starts_with_word(char* line, const char* word) {
    line = skip_space(line);
    size_t len = strlen(word);
    return strncmp(line, word, len) == 0 &&
           (line[len] == '\0' || line[len] == ' ' || line[len] == '\t');
}

// Returns 1 if the line is exactly `word` (trailing spaces allowed)
static int is_keyword_line(char* line, const char* word) {
    if (!starts_with_word(line, word)) return 0;
    char* rest = skip_space(skip_space(line) + strlen(word));
    return *rest == '\0';
}

int is_if_line(char* line) {
    return starts_with_word(line, "if");
}

static void free_lines(char** lines, int n) {
    for (int i = 0; i < n; i++) free(lines[i]);
}

/*
 * first_line is the "if <command>" line already read by the main loop.
 * Returns the exit status of the last command run (0 if none ran).
 */
int run_if_block(char* first_line) {
    // The condition is the text after the "if" keyword
    char* condition = skip_space(skip_space(first_line) + 2);
    if (*condition == '\0') {
        fprintf(stderr, "syntax error: 'if' needs a command\n");
        return 1;
    }
    condition = strdup(condition);

    char* then_lines[MAX_BLOCK_LINES];
    char* else_lines[MAX_BLOCK_LINES];
    int n_then = 0, n_else = 0;
    int has_else = 0;
    enum if_state state = EXPECT_THEN;
    int ok = 0;   // becomes 1 when we reach "fi" without errors

    char* line;
    while ((line = readline("> ")) != NULL) {
        char* text = skip_space(line);

        if (*text == '\0') {                       // ignore blank lines
            free(line);
            continue;
        }

        if (state == EXPECT_THEN) {
            if (is_keyword_line(text, "then")) {
                state = IN_THEN;
            } else {
                fprintf(stderr, "syntax error: expected 'then'\n");
                free(line);
                break;
            }
        } else if (is_keyword_line(text, "fi")) {
            ok = 1;
            free(line);
            break;
        } else if (is_keyword_line(text, "else")) {
            if (has_else) {
                fprintf(stderr, "syntax error: more than one 'else'\n");
                free(line);
                break;
            }
            has_else = 1;
            state = IN_ELSE;
        } else if (starts_with_word(text, "if")) {
            fprintf(stderr, "syntax error: nested 'if' is not supported\n");
            free(line);
            break;
        } else {
            // A normal command: store it in the current block's array
            int count = (state == IN_THEN) ? n_then : n_else;
            if (count == MAX_BLOCK_LINES) {
                fprintf(stderr, "syntax error: too many lines in block (max %d)\n",
                        MAX_BLOCK_LINES);
                free(line);
                break;
            }
            if (state == IN_THEN) then_lines[n_then++] = strdup(text);
            else                  else_lines[n_else++] = strdup(text);
        }
        free(line);
    }

    if (line == NULL && !ok) {
        fprintf(stderr, "syntax error: unexpected end of input (missing 'fi')\n");
    }

    int status = 0;
    if (ok) {
        // 1. Run the condition with normal fork-exec-wait and get its exit code
        int cond_status = execute_line(condition);

        // 2. Exit code 0 means success -> then block; otherwise -> else block
        if (cond_status == 0) {
            for (int i = 0; i < n_then; i++) status = execute_line(then_lines[i]);
        } else {
            for (int i = 0; i < n_else; i++) status = execute_line(else_lines[i]);
        }
    }

    free(condition);
    free_lines(then_lines, n_then);
    free_lines(else_lines, n_else);
    return ok ? status : 1;
}
