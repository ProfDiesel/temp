from asyncio.streams import StreamWriter, StreamReader
from .config_reader import parse, write as write_config, Walker, format_assignment, as_int, as_object, as_str
from argparse import ArgumentParser
import logging
from pathlib import Path
import asyncio
from asyncio.subprocess import PIPE
import socket
from base64 import b64encode
import json
from dataclasses import dataclass
from typing import Dict, cast


LOGGER: logging.Logger = logging.getLogger('Fairy')


@dataclass
class Instrument:
    _id: int
    _serial: int = 0


class Fairy:
    def __init__(self, config: Walker):
        self.__config:Walker = config
        self.__instruments: Dict[int, Instrument] = {}

        if subscription := as_object(self.__config.subscription):
            instrument:int = as_int(subscription.instrument)
            self.__instruments[instrument] = Instrument(instrument)

    @staticmethod
    def _get_payload(name: str, instrument: Instrument) -> str:
        def encode(message) -> str:
            return b64encode(json.dumps(message).encode()).decode()

        message, datagram = {'instrument': instrument._id, 'content': f'hit #{instrument._serial}'}, {'instrument': instrument._id, 'content': f'datagram hit #{instrument._serial}'}
        return f'''\
{name}.instrument <- {instrument._id};
{name}.message <- '{encode(message)}';
{name}.datagram <- '{encode(datagram)}';
'''

    async def __send(self, data: str) -> None:
        out: StreamWriter = cast(StreamWriter, self.process.stdin)
        LOGGER.debug('send="%s"', data.replace('"', '\\"'))
        out.write(data.encode() + b'\n\n')
        await out.drain()

    async def setup(self, *, loop=None) -> None:
        self.process = await asyncio.create_subprocess_shell(
            'rr ' + as_str(self.__config.executable),
            stdin=PIPE,
            stdout=PIPE,
            stderr=PIPE,
            close_fds=False,
        )
        LOGGER.info('pid=%d Subprocess launched', self.process.pid)
        loop = loop or asyncio.get_event_loop()
        loop.create_task(self.__command_reader())
        loop.create_task(self.__stderr_reader())

        down_host, down_port = as_str(self.__config.down_address).split(':', 1)
        down_socket = socket.create_connection((down_host, int(down_port)))
        down_reader, down_writer = await asyncio.open_connection(sock=down_socket)

        command = f'''\
config.feed <- 'feed';
config.send <- 'send';
config.command_out_fd <- 1;
feed.snapshot <- '{as_str(self.__config.up_snapshot_address)}';
feed.updates <- '{as_str(self.__config.up_updates_address)}';
send.fd <- {down_socket.fileno()};
'''

        if subscription := as_object(self.__config.subscription):
            instrument: int = as_int(subscription.instrument)
            command += "\n".join(format_assignment(object_.name, field, value) for object_, field, value in subscription.walk()) + "\n"
            command += self._get_payload(subscription.name, self.__instruments[instrument])
            command += f'''\
config.subscription <- '{subscription}';
'''
        await self.__send(command)

    async def send_payload(self, instrument:int) -> None:
        await self.__send('''\
entrypoint.type <- 'payload';
''' + self._get_payload('entrypoint', self.__instruments[instrument]))

    async def send_subscribe(self, instrument: int, **kwargs) -> None:
        await self.__send('''\
entrypoint.type <- 'subscribe';
''' + self._get_payload('entrypoint', self.__instruments[instrument]) + write_config( { 'entrypoint': { **kwargs, } }))

    async def send_unsubscribe(self, instrument: int) -> None:
        await self.__send(write_config({'entrypoint': {'type': 'unsubscribe', 'instrument': instrument}}))

    async def quit(self) -> None:
        await self.__send(write_config({'entrypoint': {'type': 'quit'}}))
        return_code:int = await self.process.wait()
        logging.info(return_code)

    async def __command_reader(self) -> None:
        while True:
            command:str = (await cast(StreamReader, self.process.stdout).readuntil(b'\n\n')).decode()
            request = Walker(parse(command), 'request')
            if request.type == 'request_payload':
                self.send_payload(as_int(request.instrument))
            if request.type == 'exit':
                break
            logging.info(command)

    async def __stderr_reader(self) -> None:
        async for line in cast(StreamReader, self.process.stderr):
            logging.info(f'[SUB] {line.decode()}')

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
