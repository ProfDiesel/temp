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
from typing import Awaitable, Callable, Dict, Final, Optional, cast

from . import config_objects as confobj
from toothpaste.marshalling import marshall_walker, unmarshall_walker
from toothpaste.types import Config



LOGGER: logging.Logger = logging.getLogger('Fairy')









@dataclass
class Instrument:
    _id: int
    _serial: int = 0


class Subprocess:
    def __init__(self, process):
        self.__process = process

    @staticmethod
    async def launch(command, on_request: Callable[[confobj.Request], Awaitable[bool]]) -> 'Subprocess':
        process = await asyncio.create_subprocess_shell(
            command,
            stdin=PIPE,
            stdout=PIPE,
            stderr=PIPE,
            close_fds=False,
        )
        LOGGER.info('command="%s" pid=%d Subprocess launched', command, process.pid)

        self_ = Subprocess(process)

        async def request_reader() -> None:
            while True:
                request_as_str:str = (await cast(StreamReader, process.stdout).readuntil(b'\n\n')).decode()
                LOGGER.debug('recv="%s"', command.replace('"', '\\"'))
                request: confobj.Request = unmarshall_walker(request_as_str, 'request', confobj.Request)
                if await on_request(request):
                    break
        request_reader_task = asyncio.create_task(request_reader())

        async def stderr_reader() -> None:
            async for line in cast(StreamReader, process.stderr):
                LOGGER.info('[SUB %d] %s', process.pid, line.decode())
        stderr_reader_task = asyncio.create_task(stderr_reader())

        async def wait_subprocess() -> None:
            await process.wait()
            request_reader_task.cancel()
            stderr_reader_task.cancel()
            LOGGER.log(logging.INFO if process.returncode == 0 else logging.ERROR, 'pid=%d returncode=%d Subprocess exited', process.pid, process.returncode)
            self_.__process = None
        asyncio.create_task(wait_subprocess())

        return self_


    async def send(self, command: confobj.Command) -> None:
        if self.__process is None:
            return
        out: StreamWriter = cast(StreamWriter, self.__process.stdin)
        data: Final[str] = marshall_walker(command)
        LOGGER.debug('send="%s"', data.replace('"', '\\"'))
        out.write(data.encode() + b'\n\n')
        await out.drain()
        LOGGER.debug('sent')


    async def wait(self):
        return await self.__process.wait()


class Fairy:
    def __init__(self, config: confobj.Fairy):
        self.__config: confobj.Fairy = config
        self.__instruments: Dict[int, Instrument] = {}
        self.__process: Optional[Subprocess] = None

        assert(self.__config.validate())

        if trigger := self.__config.trigger:
            instrument: int = trigger.instrument
            self.__instruments[instrument] = Instrument(instrument)

    @staticmethod
    def _get_payload(instrument: Instrument) -> confobj.Payload:
        def encode(message) -> str:
            return b64encode(json.dumps(message).encode()).decode()

        message, datagram = {'instrument': instrument._id, 'content': f'hit #{instrument._serial}'}, {'instrument': instrument._id, 'content': f'datagram hit #{instrument._serial}'}
        return confobj.Payload('payload', message=json.dumps(message).encode(), datagram=json.dumps(datagram).encode())

    async def setup(self, *, loop=None) -> None:
        assert(self.__process is None)
        self.__process = await Subprocess.launch(self.__config.executable, self.__on_request)

        down_socket = socket.create_connection(self.__config.down_address)
        down_reader, down_writer = await asyncio.open_connection(sock=down_socket)

        command = confobj.Dust('config', feed=self.__config.feed, send=confobj.Send('send', fd=down_socket.fileno(), datagram=None, disposable_payload=False), command_out_fd=1)
        if trigger := self.__config.trigger:
            command.subscription = confobj.Subscription('subscription', trigger=trigger, payload=self._get_payload(self.__instruments[trigger.instrument]))

        await self.__process.send(command)

    async def send_payload(self, instrument: int) -> None:
        if self.__process is None:
            return
        await self.__process.send(confobj.UpdatePayload('entrypoint', instrument=instrument, payload=self._get_payload(self.__instruments[instrument])))

    async def send_subscribe(self, instrument: int, trigger: confobj.Trigger) -> None:
        if self.__process is None:
            return
        await self.__process.send(confobj.Subscribe('entrypoint', subscription=confobj.Subscription(trigger=trigger, payload = self._get_payload(self.__instruments[trigger.instrument]))))

    async def send_unsubscribe(self, instrument: int) -> None:
        if self.__process is None:
            return
        await self.__process.send(confobj.Unsubscribe('entrypoint', instrument=instrument))

    async def quit(self) -> None:
        if self.__process is None:
            return
        await self.__process.send(confobj.Quit('entrypoint'))
        await self.__process.wait()

    async def __on_request(self, request: confobj.Request) -> bool:
        if isinstance(request, confobj.Quit):
            return True
        elif isinstance(request, confobj.RequestPayload):
            self.send_payload(request.instrument)
        return False



def main(argv=None):
    parser: Final = ArgumentParser()
    parser.add_argument('--logging-ini')
    parser.add_argument("--config", default=Path(__file__).parent / "ppf.conf")
    args = parser.parse_args(argv)

    if args.logging_ini:
        logging.confobj.fileConfig(args.logging_ini)
    else:
        logging.basicConfig(level=logging.INFO)

    async def run():
        config: Final = unmarshall_walker(Path(args.config).read_text(), 'ppf', confobj.Ppf)
        fairy: Final = Fairy(config)
        await fairy.setup()
        await fairy.run()

    asyncio.run(run())
