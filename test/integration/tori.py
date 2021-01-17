#!/usr/bin/env python3
import asyncio
from asyncio.subprocess import PIPE

import argparse
import logging
import logging.config

from base64 import b64encode
import socket
import sys
from collections import defaultdict
import logging
import os

import up

INSTRUMENT = 42

up_snapshot_addr: Address = ('127.0.0.1', 4000)
up_updates_addr: Address = ('239.255.0.1', 4000)
down_stream_addr: Address = ('127.0.0.1', 9998)
down_datagram_addr: Address = ('127.0.0.1', 9999)

config = f'''
config.feed <- 'feed';
config.send <- 'send';
config.subscription <- 'subscription';
config.command_out_fd <- 1;
feed.update <- '239.255.0.1:4000';
feed.snapshot <- '127.0.0.1:4000';
feed.spin_duration <- 1;
feed.spin_count <- 100;
send.datagram <- '127.0.0.1:5000';
send.cooldown <- 2000000;
send.disposable_payload <- 'true';
subscription.instrument <- {INSTRUMENT};
subscription.instant_threshold <- 0.5;
subscription.message <- '{{message_payload}}';
subscription.datagram <- '{{datagram_payload}}';
--
send.fd <- {{down_fd}};
'''

class Tori:
    def __init__(self):
        self.__down = Down()
        self.__up = Up()
        await down.open(down_stream_addr, down_datagram_addr)
        self.__up.start()


class Uke:
    def __init__(self):
        self.__fairy = Fairy()

    async def setup(self, loop, config):
        self.process = await asyncio.create_subprocess_shell('ppf/ppf', stdin=PIPE, stdout=PIPE, stderr=PIPE, close_fds=False)


async def scenario_0(up):
    up.update(1000, INSTRUMENT_0, b0 = 10, bq0 = 5)
    up.flush()
    up.update(1001, INSTRUMENT_0, b0 = 11, bq0 = 1)
    up.flush()
    datagram = await down.wait_for_datagram()

async def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--logging-ini')
    args = parser.parse_args(argv)

    if args.logging_ini:
        logging.config.fileConfig(args.logging_ini)
    else:
        logging.basicConfig(level=logging.INFO)

    loop = asyncio.get_running_loop()
    up_, down_ = up(), down()

    await up_.open(loop)
    await down_.open(loop)

    message_payload = b'message_payload'
    datagram_payload = b'datagram_payload'

    uke_ = uke()
    await uke_.setup(loop, config.format(down_fd=down_.stream_fd, message_payload=b64encode(message_payload).hex(), datagram_payload=b64encode(datagram_payload).hex()))

    await asyncio.sleep(3)

    await uke_.quit()


if __name__ == '__main__':
    asyncio.run(main())
