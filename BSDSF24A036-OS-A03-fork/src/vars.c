#include "shell.h"
#include <ctype.h>

/*
 * Shell variables, stored as a linked list of name/value pairs.
 *
 *   MSG="hello"     -> assignment (a built-in action, no fork)
 *   echo $MSG       -> $MSG is replaced by "hello" before the command runs
 *   set             -> prints every variable
 */
typedef struct var {
    char* name;
    char* value;
    struct var* next;
} var_t;

static var_t* var_list = NULL;   // head of the list

// Finds a variable by name, or returns NULL
static var_t* find_var(const char* name) {
    for (var_t* v = var_list; v != NULL; v = v->next) {
        if (strcmp(v->name, name) == 0)
            return v;
    }
    return NULL;
}

// Creates the variable, or replaces the value if it already exists
void set_var(const char* name, const char* value) {
    var_t* v = find_var(name);
    if (v != NULL) {
        free(v->value);                // free the old value before replacing
        v->value = strdup(value);
        return;
    }
    v = malloc(sizeof(var_t));
    v->name = strdup(name);
    v->value = strdup(value);
    v->next = var_list;                // insert at the head of the list
    var_list = v;
}

// Value of a shell variable; falls back to the environment (e.g. $HOME)
const char* get_var(const char* name) {
    var_t* v = find_var(name);
    if (v != NULL)
        return v->value;
    return getenv(name);               // NULL if not defined anywhere
}

// The "set" built-in
void print_vars(void) {
    for (var_t* v = var_list; v != NULL; v = v->next) {
        printf("%s=%s\n", v->name, v->value);
    }
}

// Variable names: a letter or '_' followed by letters, digits or '_'
static int is_name_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_name_char(char c)  { return isalnum((unsigned char)c) || c == '_'; }

/*
 * Returns 1 if the token looks like NAME=value with a valid NAME.
 * The tokenizer already removed the quotes, so MSG="hi there" arrives
 * here as the single token  MSG=hi there
 */
int is_assignment(const char* token) {
    if (!is_name_start(token[0]))
        return 0;
    int i = 1;
    while (is_name_char(token[i]))
        i++;
    return token[i] == '=';
}

// Splits NAME=value at the first '=' and stores it
void do_assignment(const char* token) {
    const char* eq = strchr(token, '=');
    char* name = strndup(token, eq - token);
    set_var(name, eq + 1);
    free(name);
}

/*
 * Returns a new malloc'd copy of `token` with every $NAME replaced by
 * its value. Undefined variables become an empty string. A '$' that is
 * not followed by a valid name (e.g. "$" or "5$") is kept as it is.
 */
static char* expand_token(const char* token) {
    size_t cap = strlen(token) + 1;
    char* out = malloc(cap);
    size_t len = 0;

    for (const char* p = token; *p != '\0'; ) {
        const char* value = NULL;
        size_t vlen = 1;
        char single[2] = { *p, '\0' };

        if (*p == '$' && is_name_start(p[1])) {
            const char* start = p + 1;
            const char* end = start;
            while (is_name_char(*end)) end++;

            char* name = strndup(start, end - start);
            value = get_var(name);
            free(name);
            if (value == NULL) value = "";   // undefined -> empty
            vlen = strlen(value);
            p = end;
        } else {
            value = single;                  // ordinary character
            p++;
        }

        // Grow the buffer if needed (+1 for the '\0')
        if (len + vlen + 1 > cap) {
            cap = (len + vlen + 1) * 2;
            out = realloc(out, cap);
        }
        memcpy(out + len, value, vlen);
        len += vlen;
    }
    out[len] = '\0';
    return out;
}

/*
 * The expansion step, run after tokenize() and before executing.
 * Every token containing '$' is replaced by a NEW malloc'd expanded
 * string and the old token is freed, so the caller can still free
 * every arglist[i] as usual.
 */
void expand_variables(char** arglist) {
    for (int i = 0; arglist[i] != NULL; i++) {
        if (strchr(arglist[i], '$') != NULL) {
            char* expanded = expand_token(arglist[i]);
            free(arglist[i]);
            arglist[i] = expanded;
        }
    }
}
