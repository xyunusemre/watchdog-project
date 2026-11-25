#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "process_manager.h"
#include "logger.h"

#define LOG_FILE    "watchdog.log"
#define WORKER_PATH "./worker"

/* Argüman verilmezse başlatılacak worker'lar */
static char *default_specs[] = { "loop", "loop", "exit:20" };

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [spec ...]\n"
            "  spec: loop | exit:<sec> | crash:<sec> | stubborn\n"
            "  at least 2, at most %d workers (default: loop loop exit:20)\n",
            prog, MAX_CHILDREN);
}

int main(int argc, char *argv[])
{
    char **specs = default_specs;
    int count = (int)(sizeof(default_specs) / sizeof(default_specs[0]));
    int status;
    pid_t pid;

    if (argc > 1) {
        specs = argv + 1;
        count = argc - 1;
    }

    if (log_open(LOG_FILE) < 0) {
        perror("log_open");
        return EXIT_FAILURE;
    }
    if (pm_init(WORKER_PATH, count, specs) < 0) {
        print_usage(argv[0]);
        log_close();
        return EXIT_FAILURE;
    }

    log_event("watchdog started (pid %d), %d workers, restart limit %d",
              (int)getpid(), count, MAX_RESTARTS);

    if (pm_start_all() < 0) {
        log_event("could not start all workers");
        log_close();
        return EXIT_FAILURE;
    }

    /* Geçici: çocuklar bitene kadar bekle. Sonraki adımlarda SIGCHLD ile değişecek. */
    while ((pid = wait(&status)) > 0)
        log_event("child pid %d finished", (int)pid);

    log_event("watchdog stopped");
    log_close();
    return EXIT_SUCCESS;
}
