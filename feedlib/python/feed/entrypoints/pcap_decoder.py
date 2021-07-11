#!/usr/bin/env python

from argparse import ArgumentParser, FileType

import dpkt

from ..feed import Decoder, Field


def main():
    parser = ArgumentParser()
    parser.add_argument('pcap-file', type=FileType('r', 0))
    parser.add_argument('--snaphsot-port', type=int, default=4400)
    parser.add_argument('--updates-port', type=int, default=4401)
    args = parser.parse_args(argv)

    def on_update_float(field: Field, value: float):
        pass

    def on_update_uint(field: Field, value: int):
        pass

    decoder = Decoder(on_update_float=on_update_float, on_update_uint=on_update_uint)

    for timestamp, buffer in dpkt.pcap.Reader(args.pcap_file):
        eth = dpkt.ethernet.Ethernet(buffer)
        ip = eth.data

        if ip.p == dpkt.ip.IP_PROTO_TCP:
            tcp = ip.data
            if tcp.dport == args.snapshot_port:
                #  snapshot
                decoder.decode(tcp.data)
        elif ip.p == dpkt.ip.IP_PROTO_UDP:
            udp = ip.data
            if udp.dport == args.updates_port:
                # up updates
                decoder.decode(udp.data)

if __name__ == '__main__':
    main()
