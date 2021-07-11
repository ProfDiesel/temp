import asyncio
from functools import singledispatchmethod
from pathlib import Path
from typing import Any, Callable, Dict, Optional, Tuple, Union

import cppyy

Address = Tuple[str, Union[int, str]]

root = Path(__file__).parent / '../..'
flavour = 'debug'
with (root / 'compile_commands.json').open() as commands_file:
    for command in json.load(commands_file):
        if (root / f'build/{flavour}/test/pic/up.o').resolve() == Path(command['output']).resolve():
            for arg in shlex.split(command['command']):
                if match := re.match('(-I|-isystem)(?P<directory>.*)', arg):
                    cppyy.add_include_path(match.groupdict()['directory'])
cppyy.add_include_path(str(root / 'include'))
cppyy.cppdef('#include "feed/feedlibpp.hpp"')
cppyy.load_library(str(root / f'_build/{flavour}/libfeedlib.so'))

from cppyy.gbl import up  # pylint: disable=import-error
from cppyy.ll import set_signals_as_exception

set_signals_as_exception(True)

#cppyy.set_debug()
Instrument = int
Field = int
Timestamp = int

class State:
    def __init__(self, instrument: Instrument):
        self.__state = up.state(instrument)

    def update(self, field: Field, value: float):
        self.__state.update(field, value)

    def update_uint(self, field: Field, value: int):
        self.__state.update(field, value)


class Encoder:
    def __init__(self):
        self.__buffer = bytearray(256)
        self.__encoder = up.make_encoder()
        self.__sequence_ids: Dict[Instrument, int] = {}

    def __next_sequence_id(self, instrument: Instrument) -> int:
        result = self.__sequence_ids.setdefault(instrument, 0)
        self.__sequence_ids[instrument] = result + 1
        return result

    @property
    def buffer(self):
        return self.__buffer

    def encode(self, timestamp, updates: Dict[Instrument, Dict[str, Any]]):
        to_flush = states_vector()
        for instrument, fields in updates.items():
            state = update_state(instrument)
            state.state.sequence_id = self.__next_sequence_id(instrument)
            apply(state, **fields)
            to_flush.push_back(state)

        n = self.__encoder.encode(timestamp, to_flush, self.__buffer, len(self.__buffer))
        if n > len(self.__buffer):
            self.__buffer = bytearray(n)
            self.__encoder.encode(timestamp, to_flush, self.__buffer, n)
        return memoryview(self.__buffer[:n])


class Decoder:
    def __init__(self, on_update_float: Callable[[Field, float], None], on_update_uint: Callable[[Field, int], None]) -> None:
        self.__decoder = up.decoder(on_update_float, on_update_uint)

    def decode(self, buffer: memoryview):
        self.__decoder.decode(buffer, len(buffer))


Future = up.future

class Server:
    def __init__(self, snapshot: Address, update: Address):
        self.__snapshot_address = snapshot
        self.__update_address = update
        self.__server: Optional[Any] = None
        self.__futures: Dict[Future, asyncio.Future[None]] = {}

    async def connect(self):
        snapshot_host, snapshot_port = self.__snapshot_address
        updates_host, updates_port = self.__update_address
        future = Future()
        self.__server = up.server(snapshot_host, str(snapshot_port), updates_host, str(updates_port), future)
        asyncio.get_event_loop().create_task(self.__loop())
        try:
            await self.__wait(future)
        except:
            self.__server = None
            raise

    async def __loop(self):
        while self.__server is not None:
            self.__server.poll()
            for future, future_ in self.__futures.items():
                if future.is_set():
                    del self.__futures[future]
                    future_.set_result()
            await asyncio.sleep(0.1)

    async def __wait(self, future: Future):
        future_ = asyncio,Future()
        self.__futures[future] = future_
        await future_
        assert(future.is_set())
        if not future:
            raise RuntimeError(future.get_message())

    def get_state(self, instrument: Instrument) -> State:
        assert(self.__server is not None)
        return self.__server.get_state(instrument)

    async def push_update(self, states):
        await self.__wait(self.__server.push_update(states))

    async def replay(self, buffer: memoryview):
        assert(self.__server is not None)
        await self.__wait(self.__server.replay(buffer, len(buffer)))
