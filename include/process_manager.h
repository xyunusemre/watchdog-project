#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>
#include <time.h>

#define MAX_CHILDREN 8
#define MAX_RESTARTS 3
#define MODE_LEN     16

/* Bir worker'ın watchdog gözünden durumu */
typedef enum {
    CHILD_RUNNING,  /* çalışıyor */
    CHILD_EXITED,   /* kendi isteğiyle normal bitti (exit 0) */
    CHILD_FAILED,   /* restart limiti aşıldı, bir daha başlatılmayacak */
    CHILD_STOPPED   /* kapanış sırasında durduruldu */
} child_state_t;

typedef struct {
    int           id;
    char          mode[MODE_LEN]; /* loop, exit, crash, stubborn */
    int           seconds;        /* exit/crash modları için süre */
    pid_t         pid;            /* çalışmıyorsa son pid */
    child_state_t state;
    int           restarts;
    time_t        started_at;
} child_t;

/* specs örnekleri: "loop", "exit:10", "crash:3", "stubborn" */
int  pm_init(const char *worker_path, int count, char *specs[]);
int  pm_start_all(void);

/* SIGCHLD geldikten sonra ana döngüden çağrılır (handler içinden değil) */
void pm_reap_children(void);

/* SIGALRM ile tetiklenen periyodik kontrol */
int  pm_start_timer(int interval_sec);
void pm_stop_timer(void);
void pm_periodic_check(void);

/* Kapanış */
void pm_begin_shutdown(void);
void pm_signal_running(int signo);

int            pm_running_count(void);
const child_t *pm_children(int *count);
const char    *pm_state_name(child_state_t state);

#endif
