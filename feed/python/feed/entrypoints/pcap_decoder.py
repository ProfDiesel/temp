#!/usr/bin/env python

from argparse import ArgumentParser, FileType
import dpkt

def main():
    parser = ArgumentParser()
    parser.add_argument('pcap-file', type=FileType('r', 0))
    parser.add_argument('--snaphsot-port', type=int, default=4400)
    parser.add_argument('--updates-port', type=int, default=4401)
    args = parser.parse_args(argv)

    for timestamp, buffer in dpkt.pcap.Reader(args.pcap_file):
        eth = dpkt.ethernet.Ethernet(buffer)
        ip = eth.data

        if ip.p == dpkt.ip.IP_PROTO_TCP:
            tcp = ip.data
            if tcp.dport == args.snapshot_port:
                #  snapshot
                tcp.data
        elif ip.p == dpkt.ip.IP_PROTO_UDP:
            udp = ip.data
            if udp.dport == args.updates_port:
                # up updates
                udp.data

if __name__ == '__main__':
    main()

