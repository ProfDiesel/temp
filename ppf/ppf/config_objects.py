from .config.typed_walker import Value
from .config.typed_walker import walker_type, ConfigSerializable
from typing import Tuple, Type, Optional
from base64 import b64encode, b64decode
from collections import namedtuple


_Address = namedtuple('_Adddres', ('host', 'port'))


class Address(_Address, ConfigSerializable):
    def __new__(cls, host: str = '0.0.0.0', port: int = 0):
        return super().__new__(cls, host, port)

    @classmethod
    def of_value(cls, value: Value) -> 'Address':
        assert(isinstance(value, str))
        host, port = value.split(':', 1)
        return cls(host, int(port))

    def as_value(self) -> Value:
        return f'{self.host}:{self.port}'


class Base64(bytes, ConfigSerializable):
    @classmethod
    def of_value(cls, value: Value) -> 'Base64':
        assert(isinstance(value, str))
        return cls(b64decode(value.encode()))

    def as_value(self) -> Value:
        return b64encode(self).decode()


Instrument = int


@walker_type
class Feed:
    snapshot: Address
    update: Address
    spin_duration: int


@walker_type
class Trigger:
    instrument: Instrument
    instant_threshold: float
    threshold: Optional[float]
    period: Optional[int]
    cooldown: int


@walker_type
class Send:
    fd: int
    datagram: Optional[Address]
    disposable_payload: bool


@walker_type
class Payload:
    message: Base64
    datagram: Optional[Base64]


@walker_type
class Subscription:
    trigger: Trigger
    payload: Payload


@walker_type
class Fairy:
    executable: str
    feed: Feed
    down_address: Address
    trigger: Optional[Trigger]


@walker_type
class Command:
    pass


@walker_type
class Request:
    pass


@walker_type
class Dust(Command):
    feed: Feed
    send: Send
    subscription: Optional[Subscription]
    command_out_fd: int


@walker_type
class Subscribe(Command):
    subscription: Subscription


@walker_type
class Unsubscribe(Command):
    instrument: Instrument


@walker_type
class Quit(Command):
    pass


@walker_type
class Exit(Request):
    pass


@walker_type
class RequestPayload(Request):
    instrument: Instrument


@walker_type
class UpdatePayload(Command):
    instrument: Instrument
    payload: Payload
