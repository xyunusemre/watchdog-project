#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

#include <signal.h>

/* Handler'lar sadece bu bayrakları set eder, asıl iş main döngüsünde yapılır */
extern volatile sig_atomic_t g_child_exited;       /* SIGCHLD */
extern volatile sig_atomic_t g_status_requested;   /* SIGUSR1 */
extern volatile sig_atomic_t g_timer_fired;        /* SIGALRM */
extern volatile sig_atomic_t g_shutdown_requested; /* SIGINT/SIGTERM, gelen sinyalin numarası */

int         sm_install_handlers(void);
void        sm_wait_for_signal(void);
const char *sm_signal_name(int signo);

#endif
