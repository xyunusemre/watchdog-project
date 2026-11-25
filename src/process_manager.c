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

static child_t     children[MAX_CHILDREN];
static int         child_count   = 0;
static const char *worker_path   = NULL;

/* "exit:10" gibi bir tanımı mode ve saniye olarak ayırır */
static int parse_spec(const char *spec, child_t *c)
{
    const char *colon = strchr(spec, ':');
    size_t len = colon ? (size_t)(colon - spec) : strlen(spec);
    char *end;
    long sec = 0;

    if (len == 0 || len >= MODE_LEN)
        return -1;
    memcpy(c->mode, spec, len);
    c->mode[len] = '\0';

    if (colon) {
        errno = 0;
        sec = strtol(colon + 1, &end, 10);
        if (errno != 0 || *end != '\0' || sec <= 0 || sec > 3600)
            return -1;
    }
    c->seconds = (int)sec;

    if (strcmp(c->mode, "loop") == 0 || strcmp(c->mode, "stubborn") == 0)
        return colon ? -1 : 0;
    if (strcmp(c->mode, "exit") == 0 || strcmp(c->mode, "crash") == 0)
        return colon ? 0 : -1;
    return -1;
}

int pm_init(const char *path, int count, char *specs[])
{
    int i;

    if (count < 2 || count > MAX_CHILDREN) {
        fprintf(stderr, "worker count must be between 2 and %d\n", MAX_CHILDREN);
        return -1;
    }

    worker_path = path;
    child_count = count;
    memset(children, 0, sizeof(children));

    for (i = 0; i < count; i++) {
        children[i].id = i + 1;
        children[i].state = CHILD_STOPPED;
        if (parse_spec(specs[i], &children[i]) < 0) {
            fprintf(stderr, "invalid worker spec: '%s'\n", specs[i]);
            return -1;
        }
    }
    return 0;
}

static int spawn_child(child_t *c)
{
    pid_t pid = fork();

    if (pid < 0) {
        log_event("fork failed for worker %d: %s", c->id, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        char id_arg[16];
        char sec_arg[16];
        char *args[5];

        snprintf(id_arg, sizeof(id_arg), "%d", c->id);
        snprintf(sec_arg, sizeof(sec_arg), "%d", c->seconds);
        args[0] = "worker";
        args[1] = id_arg;
        args[2] = c->mode;
        args[3] = sec_arg;
        args[4] = NULL;

        execv(worker_path, args);
        perror("execv");
        _exit(127); /* exec başarısız: watchdog bunu beklenmedik bitiş sayar */
    }

    c->pid = pid;
    c->state = CHILD_RUNNING;
    c->started_at = time(NULL);
    log_event("worker %d started (pid %d, mode %s)", c->id, (int)pid, c->mode);
    return 0;
}

int pm_start_all(void)
{
    int i;

    for (i = 0; i < child_count; i++) {
        if (spawn_child(&children[i]) < 0)
            return -1;
    }
    return 0;
}

int pm_running_count(void)
{
    int i;
    int n = 0;

    for (i = 0; i < child_count; i++) {
        if (children[i].state == CHILD_RUNNING)
            n++;
    }
    return n;
}

const child_t *pm_children(int *count)
{
    *count = child_count;
    return children;
}

const char *pm_state_name(child_state_t state)
{
    switch (state) {
    case CHILD_RUNNING: return "RUNNING";
    case CHILD_EXITED:  return "EXITED";
    case CHILD_FAILED:  return "FAILED";
    case CHILD_STOPPED: return "STOPPED";
    default:            return "UNKNOWN";
    }
}
