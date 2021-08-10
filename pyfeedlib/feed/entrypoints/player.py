#!/usr/bin/env python

import asyncio
from argparse import ArgumentParser, FileType

from ..feed import Address, Server


def address(as_str) -> Address:
    host, port = as_str.split(':', 1)
    return (host, int(port))

async def async_main(argv=None):
    parser = ArgumentParser()
    parser.add_argument('scenario-file', type=FileType('rb'))
    parser.add_argument('--snapshot-address', type=address, default=Address('0.0.0.0', 4400))
    parser.add_argument('--update-address', type=address, default=Address('0.0.0.0', 4401))
    args = parser.parse_args(argv)
    scenario_file = getattr(args, 'scenario-file')

    server = Server(args.snapshot_address, args.update_address)
    await server.connect()
    await server.replay(scenario_file.read())

def main(argv=None):
    asyncio.run(async_main())

if __name__ == '__main__':
    main()
