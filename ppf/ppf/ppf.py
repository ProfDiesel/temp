from asyncio.streams import StreamWriter, StreamReader
from .config_reader import parse, write as write_config, Walker, format_assignment, as_int, as_object, as_str, as_object_opt
from argparse import ArgumentParser
import logging
from pathlib import Path
import asyncio
from asyncio.subprocess import PIPE, Process
import socket
from base64 import b64encode
import json
from dataclasses import dataclass
from typing import Dict, cast, Optional

LOGGER: logging.Logger = logging.getLogger('Fairy')

@dataclass
class Instrument:
    _id: int
    _serial: int = 0


class Subprocess:
    def __init__(self, process):
        self.__process = process

    @staticmethod
    async def launch(command, on_request) -> 'Subprocess':
        process = await asyncio.create_subprocess_shell(
            command,
            stdin=PIPE,
            stdout=PIPE,
            stderr=PIPE,
            close_fds=False,
        )

        LOGGER.info('pid=%d command="%s" Subprocess launched', process.pid, command.replace('"', r'\"'))

        self_ = Subprocess(process)

        async def command_reader() -> None:
            while True:
                command:str = (await cast(StreamReader, process.stdout).readuntil(b'\n\n')).decode()
                if LOGGER.isEnabledFor(logging.DEBUG):
                    LOGGER.debug('recv="%s"', command.replace('"', r'\"'))
                request = Walker(parse(command), 'request')
                if await on_request(request):
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
        if LOGGER.isEnabledFor(logging.DEBUG):
            LOGGER.debug('send="%s"', data.replace('"', r'\"'))
        out.write(data.encode() + b'\n\n')
        await out.drain()


class Fairy:
    def __init__(self, config: Walker):
        self.__config:Walker = config
        self.__instruments: Dict[int, Instrument] = {}
        self.__process: Optional[Subprocess] = None

        if subscription := as_object_opt(self.__config.get('subscription')):
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

    async def setup(self, *, loop=None) -> None:
        assert(self.__process is None)
        self.__process = await Subprocess.launch('rr ' + as_str(self.__config.executable), self.__on_request)

        down_host, down_port = as_str(self.__config.down_address).split(':', 1)
        down_socket = socket.create_connection((down_host, int(down_port)))
        down_reader, down_writer = await asyncio.open_connection(sock=down_socket)

        command = f'''\
config.feed <- 'feed';
config.send <- 'send';
config.command_out_fd <- 1;
feed.snapshot <- '{as_str(self.__config.up_snapshot_address)}';
feed.update <- '{as_str(self.__config.up_updates_address)}';
feed.spin_duration <- 0;
send.fd <- {down_socket.fileno()};
'''

        if subscription := as_object(self.__config.subscription):
            instrument: int = as_int(subscription.instrument)
            command += "\n".join(format_assignment(object_.name, field, value) for object_, field, value in subscription.walk()) + "\n"
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

    async def __on_request(self, request) -> bool:
        if request.type == 'exit':
            return True
        elif request.type == 'request_payload':
            self.send_payload(as_int(request.instrument))
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
        config = Walker(parse(Path(args.config).read_text()), 'ppf')
        fairy = Fairy(config)
        await fairy.setup()
        await fairy.run()

    asyncio.run(run())
