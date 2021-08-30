#!/usr/bin/env python

from argparse import ArgumentParser, FileType

import dpkt
from dpkt.ethernet import Ethernet
from dpkt.ip import IP, IP_PROTO_UDP, IP_PROTO_TCP
from dpkt.udp import UDP
from dpkt.tcp import TCP

from ..feed import Decoder, Field, Instrument, State


def main(argv=None):
    parser = ArgumentParser()
    parser.add_argument('pcap-file', nargs='?', type=FileType('rb'), default='-')
    parser.add_argument('--unbuffered', action='store_true')
    parser.add_argument('--snapshot-port', type=int, default=4400)
    parser.add_argument('--updates-port', type=int, default=4401)
    args = parser.parse_args(argv)

    input_ = getattr(args, 'pcap-file')
    if args.unbuffered:
        input_ = getattr(input_, 'buffer', input_)

    class PrintDecoder(Decoder):
        def on_message(self, state: State):
            print(state)

        def decoder(self, data):
            super().decode(data)

    decoder = PrintDecoder()

    for timestamp, buffer in dpkt.pcap.Reader(input_):
        eth = Ethernet(buffer[2:]) # TODO understand why ...

        ip = eth.data
        if not isinstance(ip, IP):
            continue

        if ip.p == IP_PROTO_TCP:
            tcp = ip.data
            if tcp.sport == args.snapshot_port:
                #  snapshot
                decoder.decode(tcp.data)
        elif ip.p == IP_PROTO_UDP:
            udp = ip.data
            if udp.dport == args.updates_port:
                # up updates
                decoder.decode(udp.data)

if __name__ == '__main__':
    main()
