#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>
#include <time.h>

#define MAX_CHILDREN 8
#define MAX_RESTARTS 3
#define MODE_LEN     16

typedef enum {
    CHILD_RUNNING = 0,
    CHILD_EXITED,
    CHILD_FAILED,
    CHILD_STOPPED
} child_state_t;

typedef struct {
    int           id;
    char          mode[MODE_LEN];
    int           seconds;
    pid_t         pid;
    child_state_t state;
    int           restarts;
    time_t        started_at;
} child_t;

/* Başlatma ve süreç yaşam döngüsü */
int  pm_init(const char *worker_path, int count, char *specs[]);
int  pm_start_all(void);
void pm_reap_children(void);

/* SIGALRM tabanlı periyodik kontrol */
int  pm_start_timer(int interval_sec);
void pm_stop_timer(void);
void pm_periodic_check(void);

/* Kontrollü kapanış (graceful shutdown) */
void pm_begin_shutdown(void);
void pm_signal_running(int signo);
void pm_wait_running(void);

/* Sorgulama fonksiyonları */
int            pm_running_count(void);
const child_t *pm_children(int *count);
const char    *pm_state_name(child_state_t state);

#endif /* PROCESS_MANAGER_H */