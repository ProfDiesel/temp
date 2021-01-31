from asyncio.subprocess import PIPE

from down import Address, Down
from up import Up


class Tori:
    def __init__(self):
        self.__down = Down()
        self.__up = Up()

    async def start(self, down_stream_addr: Address, down_datagram_addr: Address):
        await self.__down.open(down_stream_addr, down_datagram_addr)
        self.__up.start()
