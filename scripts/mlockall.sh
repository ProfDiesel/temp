#!/usr/bin/env bash
ulimit -l 1073741824
LD_PRELOAD+=$(dirname $BASH_SOURCE)/libmlockall.so exec $*
