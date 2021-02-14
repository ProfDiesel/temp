#!/usr/bin/env bash
LD_PRELOAD=mimalloc.so:$LD_PRELOAD numactl --membind 0 --physcpybind 2,3 -- $*
PID=$!
sleep 5
migratepages $PID all 0
taskset -p 2 $(find /proc/$PID/task -mindepth 1 -maxdepth 1 -exec grep -q 'main' {}/comm \; -printf %P)
taskset -p 3 $(find /proc/$PID/task -mindepth 1 -maxdepth 1 -exec grep -q 'log' {}/comm \; -printf %P)
chrt -f --pid 99 $(find /proc/$PID/task -mindepth 1 -maxdepth 1 -exec grep -q 'main' {}/comm \; -printf %P)
