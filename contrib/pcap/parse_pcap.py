#!/usr/bin/env python

from argparse import ArgumentParser, FileType
import sys

import dpkt


def main(argv=None):
    parser = ArgumentParser()
    parser.add_argument('pcap-file', nargs='?', type=FileType(), default='-')
    args = parser.parse_args(argv)

    input_ = getattr(args, 'pcap-file')
    input_ = getattr(input_, 'buffer', input_)

    for timestamp, buffer in dpkt.pcap.Reader(input_):
        eth = dpkt.ethernet.Ethernet(buffer)
        ip = eth.data

        if ip.p == dpkt.ip.IP_PROTO_TCP:
            tcp = ip.data
            if tcp.dport == 4400:
                # up snapshot
                tcp.data
            elif tcp.dport == 9998:
                # down stream
                tcp.data

        elif ip.p == dpkt.ip.IP_PROTO_UDP:
            udp = ip.data
            if udp.dport == 4401:
                # up updates
                print(udp.data)
            elif udp.dport == 9999:
                # down datagram
                print(udp.data)

if __name__ == '__main__':
    main()
