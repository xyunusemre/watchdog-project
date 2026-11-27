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

static child_t *find_worker_by_pid(pid_t pid)
{
    for (int i = 0; i < g_worker_count; i++) {
        if (g_workers[i].state == CHILD_RUNNING && g_workers[i].pid == pid)
            return &g_workers[i];
    }
    return NULL;
}

static void process_child_exit(child_t *entry, int status)
{
    if (g_shutting_down) {
        entry->state = CHILD_STOPPED;
        log_event("worker %d (pid=%d) stopped during shutdown",
                  entry->id, (int)entry->pid);
        return;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        entry->state = CHILD_EXITED;
        log_event("worker %d (pid=%d) exited normally",
                  entry->id, (int)entry->pid);
        return;
    }

    if (WIFEXITED(status)) {
        log_event("worker %d (pid=%d) exited with error code %d",
                  entry->id, (int)entry->pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        log_event("worker %d (pid=%d) terminated by signal %d (%s)",
                  entry->id, (int)entry->pid, sig, strsignal(sig));
    } else {
        log_event("worker %d (pid=%d) terminated abnormally (status=0x%x)",
                  entry->id, (int)entry->pid, (unsigned)status);
    }

    if (entry->restarts >= MAX_RESTARTS) {
        entry->state = CHILD_FAILED;
        log_event("worker %d reached max restart limit (%d), giving up",
                  entry->id, MAX_RESTARTS);
        return;
    }

    entry->restarts++;
    log_event("restarting worker %d (attempt %d/%d)",
              entry->id, entry->restarts, MAX_RESTARTS);

    if (spawn_worker(entry) < 0)
        entry->state = CHILD_FAILED;
}

void pm_reap_children(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        child_t *entry = find_worker_by_pid(pid);
        if (entry != NULL) {
            process_child_exit(entry, status);
        } else {
            log_event("reaped unknown child process (pid=%d)", (int)pid);
        }
    }

    if (pid == -1 && errno != ECHILD)
        log_event("waitpid error: %s", strerror(errno));
}

int pm_start_timer(int interval_sec)
{
    struct itimerval itv;

    memset(&itv, 0, sizeof(itv));
    itv.it_value.tv_sec    = interval_sec;
    itv.it_interval.tv_sec = interval_sec;

    if (setitimer(ITIMER_REAL, &itv, NULL) < 0) {
        log_event("setitimer failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

void pm_stop_timer(void)
{
    struct itimerval itv;
    memset(&itv, 0, sizeof(itv));
    setitimer(ITIMER_REAL, &itv, NULL);
}

void pm_periodic_check(void)
{
    int running = 0;

    pm_reap_children();

    for (int i = 0; i < g_worker_count; i++) {
        child_t *entry = &g_workers[i];
        if (entry->state != CHILD_RUNNING)
            continue;

        if (kill(entry->pid, 0) == 0) {
            running++;
        } else if (errno == ESRCH) {
            log_event("periodic check: worker %d (pid=%d) not alive",
                      entry->id, (int)entry->pid);
        }
    }

    log_event("periodic check: %d/%d workers running", running, g_worker_count);
}

void pm_begin_shutdown(void)
{
    g_shutting_down = 1;
}

void pm_signal_running(int signo)
{
    for (int i = 0; i < g_worker_count; i++) {
        child_t *entry = &g_workers[i];
        if (entry->state != CHILD_RUNNING)
            continue;

        if (kill(entry->pid, signo) < 0 && errno != ESRCH) {
            log_event("kill(pid=%d, sig=%d) failed: %s",
                      (int)entry->pid, signo, strerror(errno));
        }
    }
}

void pm_wait_running(void)
{
    for (int i = 0; i < g_worker_count; i++) {
        child_t *entry = &g_workers[i];
        int status;
        pid_t res;

        if (entry->state != CHILD_RUNNING)
            continue;

        do {
            res = waitpid(entry->pid, &status, 0);
        } while (res == -1 && errno == EINTR);

        if (res == entry->pid)
            process_child_exit(entry, status);
        else
            entry->state = CHILD_STOPPED;
    }
}

int pm_running_count(void)
{
    int active = 0;
    for (int i = 0; i < g_worker_count; i++) {
        if (g_workers[i].state == CHILD_RUNNING)
            active++;
    }
    return active;
}

const child_t *pm_children(int *count)
{
    if (count != NULL)
        *count = g_worker_count;
    return g_workers;
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