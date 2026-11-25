#include <string.h>
#include <signal.h>

#include "signal_manager.h"

volatile sig_atomic_t g_child_exited       = 0;
volatile sig_atomic_t g_status_requested   = 0;
volatile sig_atomic_t g_timer_fired        = 0;
volatile sig_atomic_t g_shutdown_requested = 0;

/* Handler'lar kurulmadan önceki sinyal maskesi, sigsuspend bunu kullanır */
static sigset_t wait_mask;

/*
 * Tek handler tüm sinyaller için. Async-signal-safe olması için
 * printf, malloc, fork vb. çağrılmaz; sadece bayrak set edilir.
 */
static void handle_signal(int signo)
{
    switch (signo) {
    case SIGCHLD:
        g_child_exited = 1;
        break;
    case SIGUSR1:
        g_status_requested = 1;
        break;
    case SIGALRM:
        g_timer_fired = 1;
        break;
    case SIGINT:
    case SIGTERM:
        g_shutdown_requested = signo;
        break;
    default:
        break;
    }
}

int sm_install_handlers(void)
{
    static const int sigs[] = { SIGINT, SIGTERM, SIGUSR1, SIGCHLD, SIGALRM };
    const int nsigs = (int)(sizeof(sigs) / sizeof(sigs[0]));
    struct sigaction sa;
    sigset_t block;
    int i;

    sigemptyset(&block);
    for (i = 0; i < nsigs; i++)
        sigaddset(&block, sigs[i]);

    /*
     * Sinyalleri önce blokla. Böylece bayrak kontrolü ile bekleme arasında
     * gelen bir sinyal kaybolmaz; sadece sigsuspend içinde teslim edilir.
     */
    if (sigprocmask(SIG_BLOCK, &block, &wait_mask) == -1)
        return -1;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sa.sa_mask = block; /* handler çalışırken diğerleri bekletilir */

    for (i = 0; i < nsigs; i++) {
        sa.sa_flags = SA_RESTART;
        if (sigs[i] == SIGCHLD)
            sa.sa_flags |= SA_NOCLDSTOP; /* sadece sonlanmada SIGCHLD */
        if (sigaction(sigs[i], &sa, NULL) == -1)
            return -1;
    }
    return 0;
}

/* Bir sinyal gelene kadar uyu. Maskeyi açma ve bekleme atomik olarak yapılır. */
void sm_wait_for_signal(void)
{
    sigsuspend(&wait_mask);
}

const char *sm_signal_name(int signo)
{
    switch (signo) {
    case SIGINT:  return "SIGINT";
    case SIGTERM: return "SIGTERM";
    case SIGUSR1: return "SIGUSR1";
    case SIGCHLD: return "SIGCHLD";
    case SIGALRM: return "SIGALRM";
    case SIGKILL: return "SIGKILL";
    default:      return "UNKNOWN";
    }
}
