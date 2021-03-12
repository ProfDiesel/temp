from common_types import Address
from down import Down
from up import Up


class Tori:
    async def __new__(cls, *args, **kwargs):
        instance = super().__new__(cls)
        await instance.__init__(*args, **kwargs)
        return instance

    async def __init__(self, up_snapshot_addr: Address, up_updates_addr: Address, down_stream_addr: Address, down_datagram_addr: Address):
        self.up = Up(up_snapshot_addr, up_updates_addr)
        self.down = await Down.create(down_stream_addr, down_datagram_addr)
