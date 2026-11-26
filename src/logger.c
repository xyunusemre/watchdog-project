#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

#define LOG_LINE_MAX 512

static int log_fd = -1;

/* write() isteğin tamamını tek seferde yazmayabilir, kalan kısmı tekrar dene */
static void write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        buf += n;
        len -= (size_t)n;
    }
}

static void format_time(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL ||
        strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_now) == 0)
        snprintf(buf, size, "%ld", (long)now);
}

/* Satırı hem log dosyasına hem de terminale yazar */
static void emit_line(const char *line, size_t len)
{
    if (log_fd >= 0)
        write_all(log_fd, line, len);
    write_all(STDOUT_FILENO, line, len);
}

int log_open(const char *path)
{
    if (log_fd >= 0)
        return 0;

    /* O_CLOEXEC: worker'lar exec sonrası log dosyasının fd'sini miras almasın */
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    return (log_fd < 0) ? -1 : 0;
}

void log_close(void)
{
    if (log_fd < 0)
        return;
    if (close(log_fd) < 0)
        perror("close log file");
    log_fd = -1;
}

void log_event(const char *fmt, ...)
{
    char line[LOG_LINE_MAX];
    char ts[32];
    va_list ap;
    int prefix;
    int msg;
    size_t len;

    format_time(ts, sizeof(ts));
    prefix = snprintf(line, sizeof(line), "[%s] ", ts);
    if (prefix < 0 || (size_t)prefix >= sizeof(line))
        return;

    va_start(ap, fmt);
    msg = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix, fmt, ap);
    va_end(ap);
    if (msg < 0)
        return;

    /* mesaj sığmadıysa kesilmiş haliyle yaz, satır sonunu her zaman ekle */
    len = strlen(line);
    if (len > sizeof(line) - 2)
        len = sizeof(line) - 2;
    line[len++] = '\n';
    emit_line(line, len);
}

void log_status_report(const child_t *children, int count)
{
    time_t now = time(NULL);
    int running = 0;
    int i;

    for (i = 0; i < count; i++) {
        if (children[i].state == CHILD_RUNNING)
            running++;
    }

    log_event("status report (SIGUSR1): %d worker(s), %d running", count, running);
    for (i = 0; i < count; i++) {
        const child_t *c = &children[i];

        if (c->state == CHILD_RUNNING)
            log_event("  worker %d | pid %d | mode %s | state %s | restarts %d/%d | uptime %lds",
                      c->id, (int)c->pid, c->mode, pm_state_name(c->state),
                      c->restarts, MAX_RESTARTS, (long)(now - c->started_at));
        else
            log_event("  worker %d | last pid %d | mode %s | state %s | restarts %d/%d",
                      c->id, (int)c->pid, c->mode, pm_state_name(c->state),
                      c->restarts, MAX_RESTARTS);
    }
    log_event("end of status report");
}
