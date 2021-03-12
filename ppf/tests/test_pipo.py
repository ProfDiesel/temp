#!/usr/bin/env python3
import asyncio
import argparse
from datetime import timedelta
import logging
import logging.config
from pathlib import Path
from contextlib import asynccontextmanager

from common_types import Address

from tori import Tori
from down import Message
from ppf.ppf import Fairy
from ppf.config_reader import Walker, parse, make_walker

import pytest
import logging
from rich.logging import RichHandler


INSTRUMENT_0 = 42

up_snapshot_addr: Address = ('127.0.0.1', 4000)
up_updates_addr: Address = ('239.255.0.1', 4001)
down_stream_addr: Address = ('127.0.0.1', 9998)
down_datagram_addr: Address = ('127.0.0.1', 9999)
executable: Path = Path(__file__).parent / '../../build/debug/ppf'

async def scenario_0(uke: Fairy, tori: Tori):
    tori.up.update(1000, INSTRUMENT_0, b0=10, bq0=5)
    tori.up.flush()
    tori.up.update(1001, INSTRUMENT_0, b0=15, bq0=1)
    tori.up.flush()
    await asyncio.sleep(1)

    '''
    def global_off_on_instrument(message: Message, as_datagram: bool):
        return message.instrument == INSTRUMENT_0 and message.content == {}.encode()

    datagram = await tori.down.wait_for_(global_off_on_instrument, timedelta(seconds=1))
    '''


@asynccontextmanager
async def randori(config: str):
    config_walker = Walker(parse((Path(__file__).parent / config).read_text().format(**locals(), **globals())), 'ppf')

    tori = await Tori(up_snapshot_addr, up_updates_addr, down_stream_addr, down_datagram_addr)

    uke = Fairy(config_walker)
    await uke.setup()
    await uke.run()

    yield uke, tori

    await uke.quit()


async def test_static():
    FORMAT = "%(message)s"
    logging.basicConfig(level=logging.DEBUG, format=FORMAT, datefmt="[%X]", handlers=[RichHandler(rich_tracebacks=True)])

    async with randori('integration.conf') as (uke, tori):
        await scenario_0(uke, tori)


async def test_dynamic():
    async with randori('integration_dynamic.conf') as (uke, tori):
        await uke.send_subscribe(INSTRUMENT_0, threshold=3, period=10)
        await scenario_0(uke, tori)
        await uke.send_unsubscribe(INSTRUMENT_0)


if __name__ == '__main__':
    asyncio.run(test_static())
