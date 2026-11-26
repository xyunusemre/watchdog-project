#ifndef LOGGER_H
#define LOGGER_H

#include "process_manager.h"

#if defined(__GNUC__)
#define LOG_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define LOG_PRINTF(fmt_idx, arg_idx)
#endif

/* Log dosyasını açar (yoksa oluşturur, varsa sonuna ekler). Hata durumunda -1. */
int  log_open(const char *path);
void log_close(void);

/*
 * Zaman damgalı tek satırlık olay kaydı: "[YYYY-MM-DD HH:MM:SS] mesaj"
 * Signal handler içinden çağrılmamalı, sadece ana döngüden çağrılır.
 */
void log_event(const char *fmt, ...) LOG_PRINTF(1, 2);

/* SIGUSR1 geldiğinde her worker'ın PID ve durumunu log dosyasına yazar */
void log_status_report(const child_t *children, int count);

#endif
