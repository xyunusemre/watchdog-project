#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "signal_manager.h"
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

/*
 * Kapanış sırası:
 * 1) yeni process başlatmayı durdur
 * 2) çalışan worker'lara SIGTERM gönder
 * 3) hepsi bitene kadar bekle ve topla
 */
static void graceful_shutdown(void)
{
    struct timespec tick = { 0, 100 * 1000 * 1000 }; /* 100 ms */

    pm_begin_shutdown();
    pm_reap_children(); /* o ana kadar ölmüş olanları topla */

    if (pm_running_count() == 0)
        return;

    log_event("sending SIGTERM to %d worker(s)", pm_running_count());
    pm_signal_running(SIGTERM);

    /* şimdilik süresiz bekle, zaman aşımı ve SIGKILL sonraki adımda */
    while (pm_running_count() > 0) {
        nanosleep(&tick, NULL);
        pm_reap_children();
    }
    log_event("all workers stopped");
}

static void run_main_loop(void)
{
    while (!g_shutdown_requested) {
        sm_wait_for_signal();

        /* kapanış her şeyden önce gelir, yoksa ölen worker yeniden başlatılabilir */
        if (g_shutdown_requested)
            break;

        if (g_child_exited) {
            g_child_exited = 0;
            pm_reap_children();
        }
        if (pm_running_count() == 0) {
            log_event("no running workers left");
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    char **specs = default_specs;
    int count = (int)(sizeof(default_specs) / sizeof(default_specs[0]));
    int exit_code = EXIT_SUCCESS;

    if (argc > 1) {
        specs = argv + 1;
        count = argc - 1;
    }

    if (log_open(LOG_FILE) < 0) {
        perror("log_open");
        return EXIT_FAILURE;
    }
    if (sm_install_handlers() < 0) {
        perror("sm_install_handlers");
        log_close();
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
        exit_code = EXIT_FAILURE;
    } else {
        run_main_loop();
    }

    if (g_shutdown_requested)
        log_event("%s received, starting graceful shutdown",
                  sm_signal_name(g_shutdown_requested));

    graceful_shutdown();
    log_event("watchdog stopped");
    log_close();
    return exit_code;
}
