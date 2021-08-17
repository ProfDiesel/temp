#!/usr/bin/env python

from argparse import ArgumentParser, FileType

import dpkt
from dpkt.ethernet import Ethernet
from dpkt.ip import IP, IP_PROTO_UDP, IP_PROTO_TCP
from dpkt.udp import UDP
from dpkt.tcp import TCP

from ..feed import Decoder, Field, Instrument


def main(argv=None):
    parser = ArgumentParser()
    parser.add_argument('pcap-file', nargs='?', type=FileType('rb'), default='-')
    parser.add_argument('--unbuffered', action='store_true')
    parser.add_argument('--snaphsot-port', type=int, default=4400)
    parser.add_argument('--updates-port', type=int, default=4401)
    args = parser.parse_args(argv)

    input_ = getattr(args, 'pcap-file')
    if args.unbuffered:
        input_ = getattr(input_, 'buffer', input_)

    class PrintDecoder(Decoder):
        def on_message(self, instrument_id: Instrument):
            print(instrument_id)

        def on_update_float(self, field, value):
            print(field, value)

        def on_update_uint(self, field, value):
            print(field, value)

    decoder = PrintDecoder()

    for timestamp, buffer in dpkt.pcap.Reader(input_):
        eth = Ethernet(buffer[2:]) # TODO understand why ...

        if not isinstance(eth.data, dpkt.ip.IP):
            continue

        ip = eth.data
        if ip.p == IP_PROTO_TCP:
            tcp = ip.data
            if tcp.dport == args.snapshot_port:
                #  snapshot
                decoder.decode(tcp.data)
        elif ip.p == IP_PROTO_UDP:
            udp = ip.data
            if udp.dport == args.updates_port:
                # up updates
                decoder.decode(udp.data)

if __name__ == '__main__':
    main()
