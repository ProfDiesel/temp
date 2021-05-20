import asyncio
from typing import Dict, Tuple, List, Callable, Awaitable, cast
from dataclasses import dataclass
from datetime import timedelta
import json

from ppf.config import Address

@dataclass
class Handshake:
    version: int
    credentials: bytes
    datagram_address: Address


@dataclass
class HandshakeResponse:
    ok: bool
    message: str


@dataclass
class Message:
    instrument: int
    content: bytes


class _DatagramBumper(asyncio.DatagramProtocol):
    def __init__(self, down):
        self.__down = down

    def datagram_received(self, datagram, addr):
        self.__down.datagram_received(datagram, addr)

    def error_received(self, exc):
        pass


@dataclass
class _ClientConnection:
    stream_reader: asyncio.StreamReader
    stream_writer: asyncio.StreamWriter
    datagram_address: Address


ConditionCallable = Callable[[_ClientConnection, Message, bool], bool]


class Down:
    def __init__(self):
        self.connections = []
        self.__awaiting_futures: List[Tuple[ConditionCallable, 'asyncio.Future[Message]']] = []
        self.__connections_by_udp_address: Dict[Address, _ClientConnection] = {}

    async def connect(self, stream_address: Address, datagram_address: Address, *, loop=None):
        if not loop:
            loop = asyncio.get_event_loop()
        self.__stream_server = await asyncio.start_server(self.on_new_connection, *stream_address)
        self.__datagram_transport, self.__datagram_protocol = await loop.create_datagram_endpoint(lambda: _DatagramBumper(self), local_addr=datagram_address)

    @staticmethod
    async def create(stream_address: Address, datagram_address: Address, *, loop=None):
        result = Down()
        await result.connect(stream_address, datagram_address, loop=loop)
        return result

    @staticmethod
    def __decode(data, message_class=Message):
        return message_class(**json.loads(data.decode()))

    @staticmethod
    def __encode(message):
        return json.dumps(message).encode()

    async def on_new_connection(self, reader, writer):
        handshake = self.__decode(await reader.readuntil(b"\n\n"), Handshake)
        connection = _ClientConnection(reader, writer, handshake.datagram_address)
        self.__connections_by_udp_address[handshake.datagram_address] = connection
        writer.write(self.__encode(HandshakeResponse(ok=True, message="ok")) + b"\n\n")
        await writer.drain()
        while data := await reader.readuntil("\n\n"):
            message = self.__decode(data)
            self.__notify_message(connection, message)

    def datagram_received(self, datagram: bytes, addr: Address):
        connection = self.__connections_by_udp_address.get(addr)
        if not connection:
            return
        message = self.__decode(datagram)
        self.__notify_message(connection, message, True)

    async def wait_for_message(self, condition: ConditionCallable, time: timedelta) -> Message:
        future: 'asyncio.Future[Message]' = asyncio.Future()
        self.__awaiting_futures.append((condition, future))
        return await future

    def __notify_message(self, connection: _ClientConnection, message: Message, as_datagram=False):
        for i, (condition, future) in enumerate(self.__awaiting_futures):
            try:
                if condition(connection, message, as_datagram):
                    future.set_result(message)
                    del self.__awaiting_futures[i]
            except Exception as exception:
                future.set_exception(exception)
                del self.__awaiting_futures[i]
