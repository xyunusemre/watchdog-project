/*
 * Watchdog tarafından fork + exec ile çalıştırılan test programı.
 *
 * Kullanım: worker <id> <mode> [sec]
 *   loop      : SIGTERM gelene kadar çalışır
 *   exit sec  : sec saniye sonra exit(0) ile normal biter
 *   crash sec : sec saniye sonra exit(1) ile hata vererek biter
 *   stubborn  : SIGTERM'i yok sayar (SIGKILL testi için)
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void on_sigterm(int signo)
{
    (void)signo;
    stop_requested = 1;
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    int id;
    int seconds;
    int elapsed = 0;
    const char *mode;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <id> <loop|exit|crash|stubborn> [sec]\n", argv[0]);
        return 2;
    }
    id = atoi(argv[1]);
    mode = argv[2];
    seconds = (argc > 3) ? atoi(argv[3]) : 0;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    /*
     * Ctrl+C terminaldeki tüm process grubuna SIGINT gönderir. Worker'lar
     * bunu yok sayar; kapatma işini watchdog SIGTERM göndererek yapar.
     */
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);

    if (strcmp(mode, "stubborn") == 0) {
        sa.sa_handler = SIG_IGN;
    } else {
        sa.sa_handler = on_sigterm;
    }
    sigaction(SIGTERM, &sa, NULL);

    printf("[worker %d] pid %d started, mode %s\n", id, (int)getpid(), mode);
    fflush(stdout);

    while (!stop_requested) {
        sleep(1);
        elapsed++;

        if (strcmp(mode, "exit") == 0 && elapsed >= seconds) {
            printf("[worker %d] work done, exiting normally\n", id);
            return 0;
        }
        if (strcmp(mode, "crash") == 0 && elapsed >= seconds) {
            fprintf(stderr, "[worker %d] simulated error, exiting with code 1\n", id);
            return 1;
        }
    }

    printf("[worker %d] SIGTERM received, cleaning up\n", id);
    return 0;
}
