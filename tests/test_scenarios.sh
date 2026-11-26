#!/usr/bin/env bash
#
# Watchdog test senaryoları (ödev, Bölüm 7).
# Kullanım: bash tests/test_scenarios.sh   ya da   make test
#
# Her senaryo watchdog'u kendi process grubunda (setsid) başlatır,
# watchdog.log dosyasını kontrol eder ve logu tests/output/ altına kopyalar.

set -u
cd "$(dirname "$0")/.." || exit 1

LOG=watchdog.log
OUT_DIR=tests/output
PASS=0
FAIL=0
WD_PID=""

mkdir -p "$OUT_DIR"
make -s all || { echo "build failed"; exit 1; }

# ---------------------------------------------------------------- yardımcılar

pass() { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }

# check "açıklama" "regex"  -> logda en az bir satır eşleşmeli
check() {
    if grep -Eq -- "$2" "$LOG"; then pass "$1"; else fail "$1"; fi
}

# check_count "açıklama" "regex" beklenen_sayı
check_count() {
    local n
    n=$(grep -Ec -- "$2" "$LOG")
    if [ "$n" -eq "$3" ]; then pass "$1 ($n)"; else fail "$1 (expected $3, got $n)"; fi
}

# zombi süreçler kill -0'a cevap verdiği için durum harfine de bakılır
is_alive() {
    local st
    st=$(ps -o stat= -p "$1" 2>/dev/null)
    [ -n "$st" ] && [[ $st != Z* ]]
}

# logdaki son "worker N started" satırından PID'i al (pid=123 ya da pid 123)
worker_pid() {
    grep -E "worker $1 started" "$LOG" | tail -1 | sed -E 's/.*pid[= ]([0-9]+).*/\1/'
}

# PID gerçekten watchdog'un worker'ı mı? (ödev: sinyalden önce PID doğrulanmalı)
is_our_worker() {
    local ppid args
    ppid=$(ps -o ppid= -p "$1" 2>/dev/null | tr -d ' ')
    args=$(ps -o args= -p "$1" 2>/dev/null)
    [ "$ppid" = "$WD_PID" ] && [[ $args == worker* ]]
}

start_watchdog() {
    local name=$1
    shift
    echo
    echo "== $name: ./watchdog $*"
    rm -f "$LOG"
    setsid ./watchdog "$@" >"$OUT_DIR/$name.stdout" 2>&1 &
    WD_PID=$!
    for _ in $(seq 30); do
        grep -q "watchdog started" "$LOG" 2>/dev/null && return 0
        sleep 0.1
    done
    fail "$name: watchdog did not start"
    return 1
}

# watchdog'un bitmesini en fazla $1 saniye bekle
wait_watchdog() {
    local ticks=$(($1 * 10))
    while is_alive "$WD_PID" && [ "$ticks" -gt 0 ]; do
        sleep 0.1
        ticks=$((ticks - 1))
    done
    is_alive "$WD_PID" && return 1
    wait "$WD_PID" 2>/dev/null
    return 0
}

stop_watchdog() {
    local name=$1
    if is_alive "$WD_PID"; then
        kill -TERM "$WD_PID"
    fi
    if ! wait_watchdog 12; then
        fail "$name: watchdog did not exit after SIGTERM"
        kill -KILL "$WD_PID" 2>/dev/null
    fi
    WD_PID=""
    cp "$LOG" "$OUT_DIR/$name.log"
}

cleanup() {
    [ -n "$WD_PID" ] && kill -KILL "$WD_PID" 2>/dev/null
    pkill -KILL -f '^worker [0-9]+ ' 2>/dev/null
}
trap cleanup EXIT

# ---------------------------------------------------------------- senaryolar

scenario_normal_operation() {
    start_watchdog normal_operation loop loop || return
    sleep 1
    check_count "two workers started" "worker [0-9]+ started" 2
    local w1 w2
    w1=$(worker_pid 1)
    w2=$(worker_pid 2)
    if is_our_worker "$w1" && is_our_worker "$w2"; then
        pass "both workers alive and children of watchdog (pids $w1, $w2)"
    else
        fail "workers are not running as children of watchdog"
    fi
    stop_watchdog normal_operation
}

scenario_normal_termination() {
    start_watchdog normal_termination exit:2 loop || return
    sleep 4
    check "worker 1 detected as normal exit" "worker 1 .*normally"
    check_count "no restart after normal exit" "restarting worker 1" 0
    stop_watchdog normal_termination
}

scenario_unexpected_termination() {
    start_watchdog unexpected_termination loop loop || return
    sleep 1
    local w old
    w=$(worker_pid 1)
    if ! is_our_worker "$w"; then
        fail "pid $w is not a worker of watchdog, SIGKILL not sent"
        stop_watchdog unexpected_termination
        return
    fi
    old=$w
    kill -KILL "$w"
    sleep 1
    check "SIGKILL detected" "worker 1 .*signal 9"
    check "restart triggered" "restarting worker 1 \(attempt 1/3\)"
    w=$(worker_pid 1)
    if [ "$w" != "$old" ] && is_our_worker "$w"; then
        pass "worker 1 running again with new pid $w"
    else
        fail "worker 1 was not restarted"
    fi
    stop_watchdog unexpected_termination
}

scenario_restart_limit() {
    start_watchdog restart_limit crash:1 loop || return
    sleep 7
    check_count "exactly 3 restart attempts" "restarting worker 1" 3
    check "restart limit reached" "worker 1 .*restart limit"
    sleep 2
    check_count "no restart after the limit" "restarting worker 1" 3
    if is_our_worker "$(worker_pid 2)"; then
        pass "other worker not affected"
    else
        fail "other worker stopped unexpectedly"
    fi
    stop_watchdog restart_limit
}

scenario_status_report() {
    start_watchdog status_report loop exit:1 || return
    sleep 2
    kill -USR1 "$WD_PID"
    sleep 1
    check "status report written" "status report \(SIGUSR1\)"
    check "running worker listed with pid" "worker 1 \| pid [0-9]+ \| .*state RUNNING"
    check "finished worker listed as EXITED" "worker 2 \| last pid [0-9]+ \| .*state EXITED"
    if is_alive "$WD_PID"; then pass "watchdog keeps running after SIGUSR1"; else fail "watchdog exited on SIGUSR1"; fi
    stop_watchdog status_report
}

scenario_shutdown_sigterm() {
    start_watchdog shutdown_sigterm loop stubborn || return
    sleep 1
    local w1 w2
    w1=$(worker_pid 1)
    w2=$(worker_pid 2)
    kill -TERM "$WD_PID"
    if wait_watchdog 12; then pass "watchdog exited after SIGTERM"; else fail "watchdog still running"; fi
    WD_PID=""
    check "SIGTERM logged" "SIGTERM received"
    check "SIGTERM sent to workers" "sending SIGTERM to 2 worker"
    check "stubborn worker killed with SIGKILL" "sending SIGKILL"
    check "shutdown completed" "all workers stopped"
    if ! is_alive "$w1" && ! is_alive "$w2"; then pass "no worker left behind"; else fail "worker still alive"; fi
    cp "$LOG" "$OUT_DIR/shutdown_sigterm.log"
}

scenario_shutdown_ctrl_c() {
    start_watchdog shutdown_ctrl_c loop loop || return
    sleep 1
    local w1 w2
    w1=$(worker_pid 1)
    w2=$(worker_pid 2)
    # Ctrl+C gibi: SIGINT tüm process grubuna gider (watchdog + worker'lar)
    kill -INT -- "-$WD_PID"
    if wait_watchdog 10; then pass "watchdog exited after Ctrl+C"; else fail "watchdog still running"; fi
    WD_PID=""
    check "SIGINT logged" "SIGINT received"
    check "workers stopped by watchdog" "sending SIGTERM to 2 worker"
    check "shutdown completed" "all workers stopped"
    if ! is_alive "$w1" && ! is_alive "$w2"; then pass "no worker left behind"; else fail "worker still alive"; fi
    cp "$LOG" "$OUT_DIR/shutdown_ctrl_c.log"
}

scenario_log_verification() {
    echo
    echo "== log_verification: all scenario logs"
    local all="$OUT_DIR/all_scenarios.log" bad
    rm -f "$all"
    cat "$OUT_DIR"/*.log 2>/dev/null | grep -v '^$' >"$all.tmp"
    mv "$all.tmp" "$all"
    bad=$(grep -Evc '^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] ' "$all")
    if [ "$bad" -eq 0 ]; then pass "every log line has a timestamp"; else fail "$bad line(s) without timestamp"; fi
    LOG=$all
    check "process creation logged" "worker [0-9]+ started"
    check "termination logged" "worker [0-9]+ .*(normally|signal|code)"
    check "restart logged" "restarting worker"
    check "shutdown logged" "watchdog stopped"
    LOG=watchdog.log
}

scenario_normal_operation
scenario_normal_termination
scenario_unexpected_termination
scenario_restart_limit
scenario_status_report
scenario_shutdown_sigterm
scenario_shutdown_ctrl_c
scenario_log_verification

echo
echo "Result: $PASS passed, $FAIL failed (logs in $OUT_DIR/)"
[ "$FAIL" -eq 0 ]
