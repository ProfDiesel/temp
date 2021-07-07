#!/usr/bin/env python

from argparse import ArgumentParser, FileType
import dpkt

def main():
    parser = ArgumentParser()
    parser.add_argument('pcap-file', type=FileType('r', 0))
    args = parser.parse_args(argv)

    for timestamp, buffer in dpkt.pcap.Reader(args.pcap_file):
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
                udp.data
            elif udp.dport == 9999:
                # down datagram
                udp.data

if __name__ == '__main__':
    main()
