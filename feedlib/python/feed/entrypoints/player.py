#!/usr/bin/env python

import asyncio
from argparse import ArgumentParser, FileType

from ..feed import Address, Server


def address(as_str):
    host, port = as_str.split(':', 1)
    return Address(host, int(port))

async def main(argv=None):
    parser = ArgumentParser()
    parser.add_argument('scenario-file', type=FileType)
    parser.add_argument('--snaphsot-address', type=address, default=Address('0.0.0.0', 4400))
    parser.add_argument('--update-address', type=address, default=Address('0.0.0.0', 4401))
    args = parser.parse_args(argv)

    server = Server(args.snapshot_address, args.update_address)
    await server.replay(args.scenario_file.read())

if __name__ == '__main__':
    asyncio.run(main())
