from argparse import ArgumentParser
import asyncio

from .tori import Tori

@walker_type("tori")
class ToriConfig:
    up_snapshot_addr: Address
    up_updates_addr: Address
    down_stream_addr: Address
    down_datagram_addr: Address

async def play(scenario: Path, up: Up):
    pass


async def main():
    parser = ArgumentParser()
    parser.add_argument('config-file', type=Path, default=Path.cwd(), help='config file')
    parser.add_argument('scenario-file', type=Path, default=Path.cwd(), help='scenario file')
    args = parser.parse_args(argv)

    config: ToriConfig = unmarshall_walker((Path(__file__).parent / args.config_file).read_text().format(**locals(), **globals()), 'tori', ToriConfig)
    tori = await Tori.create(config.up_snapshot_addr, config.up_updates_addr, config.down_stream_addr, config.down_datagram_addr)
    await asyncio.sleep(3)
    play(args.scenario_file, tori.up)


if __name__ == '__main__':
    asyncio.run(main())

