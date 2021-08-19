from abc import abstractmethod
import asyncio
from dataclasses import dataclass
from typing import Any, Awaitable, Callable, Dict, Final, Optional, Tuple

from feedlib import lib as _feedlib
from feedlib import ffi

Instrument = int
SequenceId = int
Field = int


@dataclass
class Address:
    host: str
    port: int


class State:
    def __init__(self, instrument: Instrument):
        self._self = _feedlib.up_state_new(instrument)

    def __del__(self):
        _feedlib.up_state_free(self._self)

    @property
    def sequence_id(self) -> SequenceId:
        return _feedlib.up_state_get_sequence_id(self._self)

    @sequence_id.setter
    def set_sequence_id(self, seq_id: SequenceId):
        _feedlib.up_state_set_sequence_id(self._self, seq_id)

    @property
    def bitset(self) -> int:
        return _feedlib.up_state_get_bitset(self._self)

    def get_float(self, field: Field) -> float:
        return _feedlib.up_state_get_float(self._self, field)

    def get_uint(self, field: Field) -> int:
        return _feedlib.up_state_get_uint(self._self, field)

    def update_float(self, field: Field, value: float):
        _feedlib.up_state_update_float(self._self, field, value)

    def update_uit(self, field: Field, value: int):
        _feedlib.up_state_update_uint(self._self, field, value)


class Encoder:
    def __init__(self):
        self._self = _feedlib.up_encoder_new()
        self.__buffer = bytearray(256)
        self.__sequence_ids: Dict[Instrument, int] = {}

    def __next_sequence_id(self, instrument: Instrument) -> int:
        result = self.__sequence_ids.setdefault(instrument, 0)
        self.__sequence_ids[instrument] = result + 1
        return result

    def __del__(self):
        _feedlib.up_encoder_free(self._self)

    @property
    def buffer(self):
        return self.__buffer

    def encode(self, timestamp, updates: Dict[Instrument, Dict[str, Any]]):
        to_flush = ffi.new('up_state_t*[]', len(updates))
        for n, (instrument, fields) in enumerate(updates.items()):
            state = State(instrument)
            _feedlib.up_state_set_sequence_id(state._self, self.__next_sequence_id(instrument))
            for field, value in fields.items():
                state.update_float(getattr(_feedlib, field), float(value))
            to_flush[n] = state._self

        n = _feedlib.up_encoder_encode(self._self, timestamp, to_flush, len(to_flush), ffi.from_buffer(self.__buffer), len(self.__buffer))
        if n > len(self.__buffer):
            self.__buffer = bytearray(n)
            _feedlib.up_encoder_encode(self._self, timestamp, to_flush, len(to_flush), ffi.from_buffer(self.__buffer), n)
        return memoryview(self.__buffer[:n])

@ffi.def_extern()
def pyfeedlib_up_on_message(instrument_id: Instrument, user_data):
    decoder: Final[Decoder] = ffi.from_handle(user_data)
    decoder.on_message(instrument_id)

@ffi.def_extern()
def pyfeedlib_up_on_update_float(field: Field, value: float, user_data):
    decoder: Final[Decoder] = ffi.from_handle(user_data)
    decoder.on_update_float(field, value)

@ffi.def_extern()
def pyfeedlib_up_on_update_uint(field: Field, value: int, user_data):
    decoder: Final[Decoder] = ffi.from_handle(user_data)
    decoder.on_update_uint(field, value)

class Decoder:
    def __init__(self) -> None:
        self._handle = ffi.new_handle(self)
        self._self = _feedlib.up_decoder_new(_feedlib.pyfeedlib_up_on_message, _feedlib.pyfeedlib_up_on_update_float, _feedlib.pyfeedlib_up_on_update_uint, self._handle)

    def __del__(self):
        _feedlib.up_decoder_free(self._self)

    def decode(self, buffer: memoryview):
        _feedlib.up_decoder_decode(self._self, buffer, len(buffer))

    @abstractmethod
    def on_message(self, instrument_id: Instrument):
        ...

    @abstractmethod
    def on_update_float(self, field, value):
        ...

    @abstractmethod
    def on_update_uint(self, field, value):
        ...


up_future = Any

class Future:
    def __init__(self):
        self._self = _feedlib.up_future_new()

    def __del__(self):
        _feedlib.up_future_free(self._self)

    def is_set(self):
        return _feedlib.up_future_is_set(self._self)

    def check(self):
        self._check(self._self)

    @staticmethod
    def _check(future):
        assert(_feedlib.up_future_is_set(future))
        if not _feedlib.up_future_is_ok(future):
            raise RuntimeError(ffi.string(_feedlib.up_future_get_message(future)))


class Server:
    def __init__(self, snapshot: Address, update: Address):
        self.__snapshot_address = snapshot
        self.__update_address = update
        self._self: Optional[_feedlib.up_server] = None
        self.__futures: Dict[up_future, asyncio.Future[None]] = {}

    async def connect(self):
        future = Future()
        self._self = _feedlib.up_server_new(self.__snapshot_address.host.encode(), str(self.__snapshot_address.port).encode(), self.__update_address.host.encode(), str(self.__update_address.port).encode(), future._self)
        asyncio.get_event_loop().create_task(self.__loop())
        try:
            await self.__wait(future._self)
        except Exception:
            self._self = None
            raise

    async def __loop(self):
        while self._self is not None:

            future = Future()
            _feedlib.up_server_poll(self._self, future._self)
            future.check()

            for future, future_ in list(self.__futures.items()):
                if _feedlib.up_future_is_set(future):
                    del self.__futures[future]
                    future_.set_result(None)
            await asyncio.sleep(0.1)

    async def __wait(self, future: up_future):
        future_: Final[asyncio.Future[None]] = asyncio.Future()
        self.__futures[future] = future_
        await future_
        Future._check(future)

    def get_state(self, instrument: Instrument) -> State:
        assert(self._self is not None)
        return _feedlib.up_server_get_state(self._self, instrument)

    async def push_update(self, states):
        await self.__wait(_feedlib.up_server_push_update(self._self, states))

    async def replay(self, buffer: memoryview):
        assert(self._self is not None)
        await self.__wait(_feedlib.up_server_replay(self._self, buffer, len(buffer)))
