from asyncio.subprocess import PIPE

from down import Address, Down
from up import Up


class Tori:
    async def __new__(cls, *args, **kwargs):
        instance = super().__new__(cls)
        await instance.__init__(*args, **kwargs) 
        return instance

    async def __init__(self, up_snapshot_addr: Address, up_updates_addr: Address, down_stream_addr: Address, down_datagram_addr: Address):
        self.__up = Up(up_snapshot_addr, up_updates_addr) 
        self.__down = await Down(down_stream_addr, down_datagram_addr)