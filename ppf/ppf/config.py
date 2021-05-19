from ppf.config_reader import TypedWalker
from typing import Tuple, Protocol

Address = Tuple[str, int]

class Object(Protocol):
    type: str

class Subscription(Object):
    instrument: int
    instant_threshold: float
    threshold: float
    period: int

class Ppf(Object):
    executable: str
    up_snapshot_address: Address
    up_updates_address: Address
    down_address: Address
    subscription: Subscription

class Command(Protocol):
    pass

class Exit(Command):
    pass

class RequestPayload(Command):
    instrument: int
