#include "shell.h"

/*
 * Background job table.
 *
 * A job is one background command line, e.g. "sleep 10 &" or
 * "sleep 5 | cat &". A pipeline has several processes, so a job keeps
 * all of its PIDs and is finished only when every one has been reaped.
 * A table slot is free when cmd == NULL.
 */
typedef struct {
    int   id;                 // job number shown as [id]
    pid_t pids[MAX_CMDS];     // processes in this job (0 = already reaped)
    int   running;            // how many of them are still running
    char* cmd;                // command text for the jobs listing
} job_t;

static job_t jobs[MAX_JOBS];
static int next_id = 1;

// Joins the tokens back into one string, e.g. {"sleep","10"} -> "sleep 10 &"
static char* join_args(char** argv) {
    size_t size = 3;   // room for " &" and '\0'
    for (int i = 0; argv[i] != NULL; i++)
        size += strlen(argv[i]) + 1;

    char* text = malloc(size);
    text[0] = '\0';
    for (int i = 0; argv[i] != NULL; i++) {
        if (i > 0) strcat(text, " ");
        strcat(text, argv[i]);
    }
    strcat(text, " &");
    return text;
}

static int jobs_empty(void) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].cmd != NULL) return 0;
    return 1;
}

// Records a new background job and prints "[id] pid" like bash
void add_job(pid_t pids[], int npids, char** argv) {
    if (jobs_empty())
        next_id = 1;   // restart numbering when no jobs are left

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].cmd == NULL) {
            jobs[i].id = next_id++;
            for (int p = 0; p < npids; p++)
                jobs[i].pids[p] = pids[p];
            for (int p = npids; p < MAX_CMDS; p++)
                jobs[i].pids[p] = 0;
            jobs[i].running = npids;
            jobs[i].cmd = join_args(argv);
            printf("[%d] %d\n", jobs[i].id, pids[npids - 1]);
            return;
        }
    }
    fprintf(stderr, "jobs: job table full, job will not be listed\n");
}

/*
 * Reaps every child that has finished, WITHOUT blocking.
 * A finished child whose status nobody has collected is a "zombie": it
 * still uses a process-table entry. waitpid(-1, ..., WNOHANG) collects
 * any one finished child and returns its PID, returns 0 if children
 * exist but none has finished yet, or -1 if there are no children.
 * Called in the main loop before every prompt, and by "jobs".
 */
void reap_jobs(void) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].cmd == NULL) continue;

            for (int p = 0; p < MAX_CMDS; p++) {
                if (jobs[i].pids[p] == pid) {
                    jobs[i].pids[p] = 0;
                    jobs[i].running--;
                    if (jobs[i].running == 0) {   // whole job finished
                        printf("[%d]+ Done\t%s\n", jobs[i].id, jobs[i].cmd);
                        free(jobs[i].cmd);
                        jobs[i].cmd = NULL;       // free the slot
                    }
                }
            }
        }
    }
}

// The "jobs" built-in: list jobs still running, with their PIDs
void print_jobs(void) {
    reap_jobs();   // drop anything that finished, so the list is accurate

    if (jobs_empty()) {
        printf("No background jobs.\n");
        return;
    }
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].cmd == NULL) continue;
        printf("[%d]  Running\t%s\tPID:", jobs[i].id, jobs[i].cmd);
        for (int p = 0; p < MAX_CMDS; p++)
            if (jobs[i].pids[p] != 0)
                printf(" %d", jobs[i].pids[p]);
        printf("\n");
    }
}
