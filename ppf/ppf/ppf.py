import asyncio
import json
import logging
import socket
from argparse import ArgumentParser
from asyncio.streams import StreamReader, StreamWriter
from asyncio.subprocess import PIPE, Process
from base64 import b64encode
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, cast, Callable

from .config_reader import Walker, as_int, as_object, as_object_opt, as_str, parse
from .config_reader import write as write_config
from . import config

LOGGER: logging.Logger = logging.getLogger('Fairy')


@dataclass
class Instrument:
    _id: int
    _serial: int = 0


class Subprocess:
    def __init__(self, process):
        self.__process = process

    @staticmethod
    async def launch(command, on_request: Callable[[config.Command], bool]) -> 'Subprocess':
        process = await asyncio.create_subprocess_shell(
            command,
            stdin=PIPE,
            stdout=PIPE,
            stderr=PIPE,
            close_fds=False,
        )
        LOGGER.info('command="%s" pid=%d Subprocess launched', command, process.pid)

        self_ = Subprocess(process)

        async def command_reader() -> None:
            while True:
                command:str = (await cast(StreamReader, process.stdout).readuntil(b'\n\n')).decode()
                LOGGER.debug('recv="%s"', command.replace('"', '\\"'))
                command = config.Command(parse(command), 'request')
                if await on_request(command):
                    break
        command_reader_task = asyncio.create_task(command_reader())

        async def stderr_reader() -> None:
            async for line in cast(StreamReader, process.stderr):
                LOGGER.info('[SUB %d] %s', process.pid, line.decode())
        stderr_reader_task = asyncio.create_task(stderr_reader())

        async def wait_subprocess() -> None:
            await process.wait()
            command_reader_task.cancel()
            stderr_reader_task.cancel()
            LOGGER.log(logging.INFO if process.returncode == 0 else logging.ERROR, 'pid=%d returncode=%d Subprocess exited', process.pid, process.returncode)
            self_.__process = None
        asyncio.create_task(wait_subprocess())

        return self_


    async def send(self, data: str) -> None:
        if self.__process is None:
            return
        out: StreamWriter = cast(StreamWriter, self.__process.stdin)
        LOGGER.debug('send="%s"', data.replace('"', '\\"'))
        out.write(data.encode() + b'\n\n')
        await out.drain()


class Fairy:
    def __init__(self, config_: config.Ppf):
        self.__config:config.Ppf = config_
        self.__instruments: Dict[int, Instrument] = {}
        self.__process: Optional[Subprocess] = None

        if subscription := self.__config.subscription:
            instrument:int = subscription.instrument
            self.__instruments[instrument] = Instrument(instrument)

    @staticmethod
    def _get_payload(name: str, instrument: Instrument) -> config.Payload:
        def encode(message) -> str:
            return b64encode(json.dumps(message).encode()).decode()

        message, datagram = {'instrument': instrument._id, 'content': f'hit #{instrument._serial}'}, {'instrument': instrument._id, 'content': f'datagram hit #{instrument._serial}'}
        return config.Payload(instrument=instrument._id, message=json.dumps(message).encode(), datagram=json.dumps(datagram).encode())

    async def setup(self, *, loop=None) -> None:
        assert(self.__process is None)
        self.__process = await Subprocess.launch('rr ' + self.__config.executable, self.__on_request)

        down_socket = socket.create_connection(self.__config.down_address)
        down_reader, down_writer = await asyncio.open_connection(sock=down_socket)

        command = f'''\
config.feed <- 'feed';
config.send <- 'send';
config.command_out_fd <- 1;
feed.snapshot <- '{self.__config.up_snapshot_address}';
feed.update <- '{self.__config.up_updates_address}';
feed.spin_duration <- 0;
send.fd <- {down_socket.fileno()};
'''

        if subscription := self.__config.subscription:
            instrument: int = subscription.instrument
            #command += "\n".join(format_assignment(object_.name, field, value) for object_, field, value in subscription.walk()) + "\n"
            command += self._get_payload(subscription.name, self.__instruments[instrument])
            command += f'''\
config.subscription <- '{subscription}';
'''
        await self.__process.send(command)

    async def send_payload(self, instrument:int) -> None:
        if self.__process is None:
            return
        await self.__process.send('''\
entrypoint.type <- 'payload';
''' + self._get_payload('entrypoint', self.__instruments[instrument]))

    async def send_subscribe(self, instrument: int, **kwargs) -> None:
        if self.__process is None:
            return
        await self.__process.send('''\
entrypoint.type <- 'subscribe';
''' + self._get_payload('entrypoint', self.__instruments[instrument]) + write_config( { 'entrypoint': { **kwargs, } }))

    async def send_unsubscribe(self, instrument: int) -> None:
        if self.__process is None:
            return
        await self.__process.send(write_config({'entrypoint': {'type': 'unsubscribe', 'instrument': instrument}}))

    async def quit(self) -> None:
        if self.__process is None:
            return
        await self.__process.send(write_config({'entrypoint': {'type': 'quit'}}))

    async def __on_request(self, request: config.Command) -> bool:
        if isinstance(request, config.Exit):
            return True
        elif isinstance(request, config.RequestPayload):
            self.send_payload(request.instrument)
        return False



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
        config = config.Ppf(parse(Path(args.config).read_text()), 'ppf')
        fairy = Fairy(config)
        await fairy.setup()
        await fairy.run()

    asyncio.run(run())
