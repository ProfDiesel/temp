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

INSTRUMENT = 42

up_snapshot_addr = ('127.0.0.1', 4000)
up_updates_addr = ('239.255.0.1', 4000)
down_stream_addr = ('127.0.0.1', 9998)
down_datagram_addr = ('127.0.0.1', 9999)

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

quit_command = '''
entrypoint.type <- 'quit';

'''

class up:
    native.feed_server
    native.instrument_state

    (INST, SEQ, B0, BQ0, O0, OQ0) = ('inst', 'seq', 'b0', 'bq0', 'o0', 'oq0')

    async def open(self, loop):
        self.snapshot_server = await asyncio.start_server(self.handle_snapshot_request, '127.0.0.1', 4000)
        self.updates_transport, _ = await loop.create_datagram_endpoint(asyncio.DatagramProtocol, local_addr=('239.255.0.1', 4000))
        self.states = defaultdict(lambda: {self.SEQ: 1})

    async def run(self):
        await self.snapshot_server.serve_forever()

    async def close(self):
        self.updates_transport.close()

    async def publish_update(self, timestamp, instrument, update):
        state = self.states[instrument]
        state.update(update, seq=state[self.SEQ] + 1)
        self.updates_transport.send_to(';'.join('{field}: {value}' for field, value in ((self.INST, instrument), *state.items())) + '\n')

    async def handle_snapshot_request(self, reader, writer):
        data = await reader.read()
        writer.write(data)
        await writer.drain()
        writer.close()


class down_stream_protocol(asyncio.StreamReaderProtocol):
    def __init__(self):
        self.reader = asyncio.StreamReader()
        super().__init__(reader)

    def connection_made(self, transport):
        super().connection_made(transport)
        self.transport = transport
        self.writer = asyncio.StreamWriter(transport, self, self.reader)

    def connection_lost(self, exc):
        super().connection_lost(exc)
        self.transport = transport

    def eof_received(self):
        super().eof_recieved()
        pass


class down_datagram_protocol(asyncio.DatagramProtocol):
    def datagram_received(self, datagram, addr):
        self.transport.sendto(datagram, addr)

    def error_received(self, exc):
        pass


client_connection = namedtuple('client_connection', ('name', 'stream_reader', 'stream_writer', 'datagram_address')

class down:
    def __init__(self):
        self.connections = []

    async def open(self, loop):
        self.stream_socket = socket.create_server(('127.0.0.1', 9998))
        self.stream_server = await loop.create_server(down_stream_protocol, sock=self.stream_socket)
        self.datagram_transport, self.datagram_protocol = await loop.create_datagram_endpoint(down_datagram_protocol, local_addr=('127.0.0.1', 9999))

    @property
    def stream_fd(self):
        return self.stream_socket.fileno()

    async def wait_for_datagram(self):
        pass

    async def wait_for_datagram(self):
        pass


class Uke:
    def __init__(self):
        self.__fairy = Fairy()

    async def setup(self, loop, config):
        self.process = await asyncio.create_subprocess_shell('build/debug/ppf', stdin=PIPE, stdout=PIPE, stderr=PIPE, close_fds=False)
        loop.create_task(self.stderr_reader(loop))
        logging.info(config)
        self.process.stdin.write(config.encode())
        await self.process.stdin.drain()

    async def quit(self):
        self.process.stdin.write(quit_command.encode())
        await self.process.stdin.drain()
        return_code = await self.process.wait()
        logging.info(return_code)

    async def stderr_reader(self, loop):
        async for line in self.process.stderr:
            logging.info(line)


async def scenario_0(up):
    await up.publish_update(1000, INSTRUMENT, {up.B0:10, up.BQ0:1})
    await up.publish_update(1000, INSTRUMENT, {up.B0:20})
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
