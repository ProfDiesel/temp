from .config_reader import Config, parse, write as write_config, Walker
from argparse import ArgumentParser
import logging
from pathlib import Path
import asyncio
from asyncio.subprocess import PIPE
import socket
from base64 import b64encode
import json
from dataclasses import dataclass
from typing import List


@dataclass
class Instrument:
    __id: int
    __serial: int = 0


class Fairy:
    def __init__(self, config: Walker):
        self.__config = config
        self.__instruments: List[Instrument] = []

    @staticmethod
    def _get_payload(name: str, instrument: Instrument):
        def encode(message):
            return b64encode(json.dumps(message).encode())

        message, datagram = {'instrument': instrument.id, 'content': f'hit #{instrument.serial}'}, {'instrument': instrument.id, 'content': f'datagram hit #{instrument.serial}'}
        return f'''\
{name}.instrument <- {instrument.id};
{name}.message <- {encode(message)};
{name}.datagram <- {encode(datagram)};
'''

    async def setup(self, *, loop=None):
        self.process = await asyncio.create_subprocess_shell(
            str(self.__config.executable),
            stdin=PIPE,
            stdout=PIPE,
            stderr=PIPE,
            close_fds=False,
        )
        loop = loop or asyncio.get_event_loop()
        loop.create_task(self.__command_reader())
        loop.create_task(self.__stderr_reader())

        down_socket = socket.create_connection(str(self.__config.down_address).split(':', 1))
        down_reader, down_writer = await asyncio.open_connection(sock=down_socket)

        self.process.stdin.write(
            '''\
config.feed <- 'feed';
config.send <- 'send';
config.command_out_fd <- 1;
send.fd <- {down_fd};'''
        ).format(down_fd=down_socket.fileno())

        if subscription := self.__config.subscription:

            def print_object(object_):
                self.process.stdin.write()

            subscription.walk(print_object)
            self.process.stdin.write(self._get_payload(str(subscription), subscription.instrument))
            self.process.stdin.write(
                f'''\
config.subscription <- {subscription}';
'''
            ).format()

        self.process.stdin.write(self.__config)
        self.process.stdin.write('\n\n')
        await self.process.stdin.drain()

    async def send_payload(self, instrument):
        self.process.stdin.write(
            '''\
entrypoint.type <- 'payload';'''
        )
        self.process.stdin.write(self._get_payload('entrypoint', instrument))
        await self.process.stdin.drain()

    async def send_subscribe(self, instrument: int, **kwargs):
        self.process.stdin.write(
            '''\
entrypoint.type <- 'subscribe';'''
        )
        self.process.stdin.write(self._get_payload('entrypoint', instrument))
        await self.process.stdin.drain()
        self.process.stdin.write(
            write_config(
                {
                    'entrypoint': {
                        **kwargs,
                    }
                }
            ).encode()
            + b'\n\n'
        )
        await self.process.stdin.drain()

    async def send_unsubscribe(self, instrument: int):
        self.process.stdin.write(write_config({'entrypoint': {'type': 'unsubscribe', 'instrument': instrument}}).encode() + b'\n\n')
        await self.process.stdin.drain()

    async def quit(self):
        self.process.stdin.write(write_config({'entrypoint': {'type': 'quit'}}).encode() + b'\n\n')
        await self.process.stdin.drain()
        return_code = await self.process.wait()
        logging.info(return_code)

    async def __command_reader(self):
        while True:
            command = await self.process.stdout.readuntil('\n\n')
            request = Walker(parse(command), 'request')
            if request.type == 'request_payload':
                self.send_payload(request.instrument)
            logging.info(line)

    async def __stderr_reader(self):
        async for line in self.process.stderr:
            logging.info(line)

    async def run(self):
        pass


def main(argv=None):
    parser = ArgumentParser()
    parser.add_argument('--logging-ini')
    parser.add_argument("--config", default=Path(__file__).parent / "ppf.conf")
    args = parser.parse_args(argv)

    if args.logging_ini:
        logging.config.fileConfig(args.logging_ini)
    else:
        logging.basicConfig(level=logging.INFO)

    async def run():
        config = Walker(parse(Path(args.config).read_text()), 'ppf')
        fairy = Fairy(config)
        await fairy.setup()
        await fairy.run()

    asyncio.run(run())
