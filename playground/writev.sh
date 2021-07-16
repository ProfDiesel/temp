#!/usr/bin/env bash
VARIANT=debug

export LD_PRELOAD=/home/jonathan/dev/pipo/temp/build/scripts/smartbulb/$VARIANT/libsmartbulb.so:$LD_PRELOAD 
export SMARTBULB_DEBUG=1
export SMARTBULB_LOG_FD=2
export SMARTBULB_ADDRESS_RE='.*'
export SMARTBULB_STACK_RE='.*'

#socat -u TCP-LISTEN:4444,reuseaddr OPEN:out.txt,creat &

gdb --args /home/jonathan/dev/pipo/temp/build/playground/$VARIANT/writev
