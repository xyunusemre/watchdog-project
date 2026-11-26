#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "process_manager.h"
#include "logger.h"

static child_t     g_workers[MAX_CHILDREN];
static int         g_worker_count = 0;
static const char *g_binary_path  = NULL;
static int         g_shutting_down = 0;

static int parse_worker_spec(const char *spec, child_t *entry)
{
    char mode_tmp[MODE_LEN];
    int sec_val = 0;
    char trailing;

    if (spec == NULL || spec[0] == '\0')
        return -1;

    if (strchr(spec, ':') != NULL) {
        if (sscanf(spec, "%15[^:]:%d%c", mode_tmp, &sec_val, &trailing) != 2)
            return -1;
        if (sec_val <= 0 || sec_val > 3600)
            return -1;
        if (strcmp(mode_tmp, "exit") != 0 && strcmp(mode_tmp, "crash") != 0)
            return -1;
    } else {
        if (strlen(spec) >= MODE_LEN)
            return -1;
        snprintf(mode_tmp, sizeof(mode_tmp), "%s", spec);
        if (strcmp(mode_tmp, "loop") != 0 && strcmp(mode_tmp, "stubborn") != 0)
            return -1;
    }

    snprintf(entry->mode, sizeof(entry->mode), "%s", mode_tmp);
    entry->seconds = sec_val;
    return 0;
}

int pm_init(const char *worker_path, int count, char *specs[])
{
    if (worker_path == NULL || count < 2 || count > MAX_CHILDREN) {
        fprintf(stderr, "Worker count must be between 2 and %d\n", MAX_CHILDREN);
        return -1;
    }

    g_binary_path   = worker_path;
    g_worker_count  = count;
    g_shutting_down = 0;
    memset(g_workers, 0, sizeof(g_workers));

    for (int i = 0; i < count; i++) {
        g_workers[i].id    = i + 1;
        g_workers[i].pid   = -1;
        g_workers[i].state = CHILD_STOPPED;

        if (parse_worker_spec(specs[i], &g_workers[i]) < 0) {
            fprintf(stderr, "Invalid worker specification: %s\n", specs[i]);
            return -1;
        }
    }
    return 0;
}

static int spawn_worker(child_t *entry)
{
    pid_t pid = fork();

    if (pid < 0) {
        log_event("fork failed for worker %d: %s", entry->id, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        sigset_t empty_mask;
        char id_buf[16];
        char sec_buf[16];
        char *exec_args[5];

        /* Parent tarafından bloklanan sinyallerin child'a miras kalmasını engelle */
        sigemptyset(&empty_mask);
        sigprocmask(SIG_SETMASK, &empty_mask, NULL);

        snprintf(id_buf, sizeof(id_buf), "%d", entry->id);
        snprintf(sec_buf, sizeof(sec_buf), "%d", entry->seconds);

        exec_args[0] = "worker";
        exec_args[1] = id_buf;
        exec_args[2] = entry->mode;
        exec_args[3] = sec_buf;
        exec_args[4] = NULL;

        execv(g_binary_path, exec_args);
        perror("execv");
        _exit(127);
    }

    entry->pid        = pid;
    entry->state      = CHILD_RUNNING;
    entry->started_at = time(NULL);

    log_event("worker %d started (pid=%d, mode=%s)",
              entry->id, (int)pid, entry->mode);
    return 0;
}

int pm_start_all(void)
{
    for (int i = 0; i < g_worker_count; i++) {
        if (spawn_worker(&g_workers[i]) < 0)
            return -1;
    }
    return 0;
}