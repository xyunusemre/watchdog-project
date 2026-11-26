# Watchdog – POSIX Signal-Based Process Monitoring

System Programming Term Project 1 (2026–2027 Fall), Sakarya University.

`watchdog` is a supervisor written in C. It starts worker processes with `fork()` + `execv()`, detects their termination with `SIGCHLD` + `waitpid()`, restarts workers that fail (at most 3 times), writes a status report on `SIGUSR1`, and shuts down gracefully on `SIGINT` / `SIGTERM`. Every event is written to `watchdog.log` with a timestamp.

## Team and task allocation

| Student | Module | Files |
|---|---|---|
| Yunus Emre Kahraman (`xyunusemre`) | 1 – Signal management and main program | `src/main.c`, `src/signal_manager.c`, `include/signal_manager.h` |
| Efe Akıncı (`EfeAkincii`) | 2 – Child process management | `src/process_manager.c`, `include/process_manager.h` |
| Taha Dervişoğlu (`tahadervisoglu`) | 3 – Logging and test infrastructure | `src/logger.c`, `include/logger.h`, `Makefile`, `tests/test_scenarios.sh`, `README.md` |

`workers/worker.c` is the test worker program used by all modules.

## Requirements

- Linux (or WSL), `gcc`, `make`, `bash`
- Only POSIX system calls are used.

## Build

```bash
make          # builds ./watchdog and ./worker
make clean    # removes build files, binaries and the log
```

## Usage

```bash
./watchdog [spec ...]
```

Each `spec` starts one worker (at least 2, at most 8). Without arguments: `loop loop exit:20`.

| Spec | Worker behaviour |
|---|---|
| `loop` | runs until it receives `SIGTERM` |
| `exit:<sec>` | exits normally (`exit(0)`) after `<sec>` seconds |
| `crash:<sec>` | exits with code 1 after `<sec>` seconds (unexpected termination) |
| `stubborn` | ignores `SIGTERM`, used to test the `SIGKILL` fallback |

Signals, from another terminal:

```bash
kill -SIGUSR1 <WATCHDOG_PID>   # write the status of every worker to the log
kill -SIGTERM <WATCHDOG_PID>   # graceful shutdown (Ctrl+C does the same)
kill -SIGKILL <WORKER_PID>     # test only: unexpected termination of a worker
```

## Behaviour

- **Normal exit** (`exit code 0`): the worker is marked `EXITED` and is not restarted.
- **Unexpected exit** (non-zero code or killed by a signal): the worker is restarted, at most 3 times. After that it is marked `FAILED`.
- **Periodic check**: every 10 seconds `SIGALRM` (`setitimer`) triggers a check of the running workers.
- **Graceful shutdown**: on `SIGINT`/`SIGTERM` no new worker is started, `SIGTERM` is sent to running workers, and workers still alive after 5 seconds get `SIGKILL`. All children are reaped, so no zombies are left.
- **Signal handlers** only set `volatile sig_atomic_t` flags. All real work (logging, `waitpid`, restarts) happens in the main loop.

## Log format

Each line starts with a timestamp: `[YYYY-MM-DD HH:MM:SS] message`. The log is appended to `watchdog.log` and also printed to the terminal. The log file is opened with `O_CLOEXEC`, so workers do not inherit its file descriptor.

Status report (`SIGUSR1`):

```
[2026-11-26 21:28:09] status report (SIGUSR1): 4 worker(s), 2 running
[2026-11-26 21:28:09]   worker 1 | last pid 1726 | mode crash | state FAILED | restarts 3/3
[2026-11-26 21:28:09]   worker 2 | last pid 1710 | mode exit | state EXITED | restarts 0/3
[2026-11-26 21:28:09]   worker 3 | pid 1711 | mode loop | state RUNNING | restarts 0/3 | uptime 6s
[2026-11-26 21:28:09]   worker 4 | pid 1712 | mode stubborn | state RUNNING | restarts 0/3 | uptime 6s
[2026-11-26 21:28:09] end of status report
```

A full sample log is in [`docs/sample_log.txt`](docs/sample_log.txt).

## Tests

```bash
make test
```

`tests/test_scenarios.sh` runs every scenario from Section 7 of the assignment and checks `watchdog.log`:

| Scenario | What is checked |
|---|---|
| Normal operation | 2 workers start and run as children of the watchdog |
| Normal termination | `exit:2` worker is detected as a normal exit and is not restarted |
| Unexpected termination | the worker PID is verified, then `SIGKILL` is sent; the worker is restarted with a new PID |
| Restart limit | `crash:1` worker is restarted exactly 3 times, then never again |
| Status reporting | after `SIGUSR1`, every worker's PID and state is in the log |
| Graceful shutdown (SIGTERM) | `SIGTERM` is sent to workers, the stubborn worker gets `SIGKILL`, no worker is left |
| Graceful shutdown (Ctrl+C) | `SIGINT` is sent to the whole process group, like Ctrl+C in a terminal |
| Log verification | every line has a timestamp; start, termination, restart and shutdown events exist |

The log of each scenario is saved in `tests/output/`. The latest results are in [`docs/test_results.txt`](docs/test_results.txt).

## Project structure

```
watchdog-project/
├── Makefile
├── README.md
├── include/        signal_manager.h, process_manager.h, logger.h
├── src/            main.c, signal_manager.c, process_manager.c, logger.c
├── workers/        worker.c
├── tests/          test_scenarios.sh
└── docs/           sample_log.txt, test_results.txt
```
