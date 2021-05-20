from ppf.config_reader import walker_type, Walker, ConfigSerializable, Value
from typing import Tuple, Type, Optional
from base64 import b64encode, b64decode

class Address(Tuple[str, int], ConfigSerializable):
    @classmethod
    def of_value(cls, value: Value) -> 'Address':
        assert(isinstance(value, str))
        host, port = value.split(':', 1)
        return cls((host, int(port)))

    def as_value(self) -> Value:
        return f'{self[0]}:{self[1]}'

class Base64(bytes, ConfigSerializable):
    @classmethod
    def of_value(cls, value: Value) -> 'Base64':
        assert(isinstance(value, str))
        return cls(b64decode(value.encode()))

    def as_value(self) -> Value:
        return b64encode(self).decode()

@walker_type
class Feed:
    snapshot: Address
    update: Address
    spin_duration: int

@walker_type
class Fairy:
    executable: str
    feed: Feed
    down_address: Address

@walker_type
class Subscription:
    instrument: int
    instant_threshold: float
    threshold: Optional[float]
    period: Optional[int]
    cooldown: int
    message: Base64
    datagram: Optional[Base64]

@walker_type
class Send:
    fd: int
    datagram: Address
    disposable_payload: bool

@walker_type
class Ppf:
    up_snapshot_address: Address
    up_updates_address: Address
    down_address: Address
    feed: Feed
    send: Send
    subscription: Optional[Subscription]

@walker_type
class Command:
    pass

@walker_type
class Exit(Command):
    pass

@walker_type
class RequestPayload(Command):
    instrument: int

@walker_type
class Payload:
    instrument: int
    message: Base64
    datagram: Optional[Base64]
