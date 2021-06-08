from ppf.config_objects import Address

from .down import Down
from .up import Up


class Tori:
    def __init__(self, up: Up, down: Down):
        self.up = up
        self.down = down

    @classmethod
    async def create(cls, up_snapshot_addr: Address, up_updates_addr: Address, down_stream_addr: Address, down_datagram_addr: Address):
        up = Up(up_snapshot_addr, up_updates_addr)
        down = Down()
        await down.connect(down_stream_addr, down_datagram_addr)
        return cls(up, down)