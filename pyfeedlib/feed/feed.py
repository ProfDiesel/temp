import asyncio
from typing import Any, Callable, Dict, Optional, Tuple

import feedlib.lib as _feedlib

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
        to_flush = states_vector()
        for instrument, fields in updates.items():
            state = State(instrument)
            state.sequence_id = self.__next_sequence_id(instrument)
            for field, value in fields.items():
                state.update(getattr(up_field, field), float(value))
            to_flush.push_back(_move(state._wrapped))

        n = _feedlib.up_encoder_encode(self._self, timestamp, to_flush, self.__buffer, len(self.__buffer))
        if n > len(self.__buffer):
            self.__buffer = bytearray(n)
            _feedlib.up_encoder_encode(self._self, timestamp, to_flush, self.__buffer, n)
        return memoryview(self.__buffer[:n])


class Decoder:
    def __init__(self, on_update_float: Callable[[Field, float], None], on_update_uint: Callable[[Field, int], None]) -> None:
        self._self = _feedlib.up_decoder_new(on_update_float, on_update_uint)

    def __del__(self):
        _feedlib.up_decoder_free(self._self)

    def decode(self, buffer: memoryview):
        _feedlib.up_decoder_decode(self._self, buffer, len(buffer))


class Server:
    class Future:
        def __init__(self):
            self._self = _feedlib.up_future_new()

        def __del__(self):
            _feedlib.up_future_free(self._self)

    def __init__(self, snapshot: Address, update: Address):
        self.__snapshot_address = snapshot
        self.__update_address = update
        self._self: Optional[up_server] = None
        self.__futures: Dict[Future, asyncio.Future[None]] = {}

    async def connect(self):
        snapshot_host, snapshot_port = self.__snapshot_address
        updates_host, updates_port = self.__update_address
        future = Future()
        self._self = _feedlib.up_server_new(snapshot_host, str(snapshot_port), updates_host, str(updates_port), future)
        asyncio.get_event_loop().create_task(self.__loop())
        try:
            await self.__wait(future)
        except:
            self._self = None
            raise

    async def __loop(self):
        while self._self is not None:
            _feedlib.up_server_poll(self._self)
            for future, future_ in self.__futures.items():
                if future.is_set():
                    del self.__futures[future]
                    _feedlib.up_future_set_result(future_)
            await asyncio.sleep(0.1)

    async def __wait(self, future: Future):
        future_ = asyncio,Future()
        self.__futures[future] = future_
        await future_
        assert(future.is_set())
        if not _feedlib.up_future_is_ok(future):
            raise RuntimeError(_feedlib.up_future_get_message(future))

    def get_state(self, instrument: Instrument) -> State:
        assert(self._self is not None)
        return _feedlib.up_server_get_state(self._self, instrument)

    async def push_update(self, states):
        await self.__wait(_feedlib.up_server_push_update(self._self, states))

    async def replay(self, buffer: memoryview):
        assert(self._self is not None)
        await self.__wait(_feedlib.up_server_replay(self._self, buffer, len(buffer)))

