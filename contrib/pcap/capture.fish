#!/usr/bin/env fish
set INTERDACE lo

sudo tcpdump -i $INTERDACE -w - \
    '(((tcp) and (port 4000) and (src host 127.0.0.1))
   or ((udp) and (port 4001) and (dst host 239.255.0.1))
   or ((tcp) and (port 9999) and (dst host 127.0.0.1))
   or ((udp) and (port 9998) and (dst host 127.0.0.1)))' \
   | tee capture.pcap | parse_pcap.py -

