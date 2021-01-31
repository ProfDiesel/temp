#!/usr/bin/env python3
import asyncio
import argparse
from datetime import timedelta
import logging
import logging.config
from pathlib import Path
from contextlib import asynccontextmanager

from tori import Address, Tori
from down import Message
from ppf.ppf import Fairy
from ppf.config_reader import walker, parse

INSTRUMENT_0 = 42

up_snapshot_addr: Address = ("127.0.0.1", 4000)
up_updates_addr: Address = ("239.255.0.1", 4000)
down_stream_addr: Address = ("127.0.0.1", 9998)
down_datagram_addr: Address = ("127.0.0.1", 9999)

config = f"""
ppf.executable <- 'build/ppf';
ppf.down_address <- '{down_stream_addr[0]}:{down_stream_addr[1]}';
"""


async def scenario_0(uke, tori):
    uke.subscribe(INSTRUMENT_0, threshold=3, period=10)
    tori.up.update(1000, INSTRUMENT_0, b0=10, bq0=5)
    tori.up.flush()
    tori.up.update(1001, INSTRUMENT_0, b0=15, bq0=1)
    tori.up.flush()

    def global_off_on_instrument(message: Message, as_datagram: bool):
        return message.instrument == INSTRUMENT_0 and message.content == {}.encode()

    datagram = await tori.down.wait_for_(global_off_on_instrument, timedelta(seconds=1))
    uke.send_unsubscribe(INSTRUMENT_0)


@asynccontextmanager
async def randori(config: str):
    tori = Tori()
    await tori.start(down_stream_addr, down_datagram_addr)

    uke = Fairy(config)
    await uke.setup()
    await uke.run()

    yield uke, tori

    await uke.quit()


async def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--logging-ini")
    args = parser.parse_args(argv)

    if args.logging_ini:
        logging.config.fileConfig(args.logging_ini)
    else:
        logging.basicConfig(level=logging.INFO)

    loop = asyncio.get_running_loop()

    config = walker(parse((Path(__file__).parent / "integration.conf").read_text()), "ppf")
    async with randori(config) as (uke, tori):
        await scenario_0(uke, tori)


if __name__ == "__main__":
    asyncio.run(main())
