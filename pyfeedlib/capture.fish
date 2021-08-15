#!/usr/bin/env fish

# sudo ifconfig lo multicast
# sudo route add -net 224.0.0.0 netmask 240.0.0.0 dev lo

#set INTERDACE lo
set INTERDACE any

stdbuf -o0 sudo tcpdump -i $INTERDACE -U -w - \
    '(((tcp) and (port 4400) and (src host 127.0.0.1))
   or ((udp) and (port 4401) and (dst host 224.0.0.1))
   or ((tcp) and (port 9999) and (dst host 127.0.0.1))
   or ((udp) and (port 9998) and (dst host 127.0.0.1)))' \
   | tee capture.pcap | pcap_decoder --unbuffered
