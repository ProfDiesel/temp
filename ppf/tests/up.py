import asyncio
from pathlib import Path, PurePath
from typing import Dict, Type
import shlex
import json
import re

from common_types import Address

import cppyy

root = Path(__file__).parent / '..'
flavour = 'debug'
with open('compile_commands.json') as commands_file:
    for command in json.load(commands_file):
        if (root / f'build/{flavour}/tests/pic/up.o').resolve() == Path(command['output']).resolve():
            for arg in shlex.split(command['command']):
                if match := re.match('(-I|-isystem)(?P<directory>.*)', arg):
                    cppyy.add_include_path(match.groupdict()['directory'])
cppyy.add_include_path(str(root / 'tests'))
cppyy.cppdef('#include "up.hpp"')
cppyy.load_library(str(root / f'build/{flavour}/libup.so'))

from cppyy.gbl import feed  # pylint: disable=import-error
from cppyy.gbl import up  # pylint: disable=import-error

from cppyy.ll import set_signals_as_exception
set_signals_as_exception(True)

#cppyy.set_debug()

instrument_type = int
update_state = cppyy.gbl.up.update_state
states_vector = cppyy.gbl.std.vector[update_state]


class Up:
    def __init__(self, snapshot_address: Address, updates_address: Address):
        snapshot_host, snapshot_port = snapshot_address
        updates_host, updates_port = updates_address
        self.__server = cppyy.gbl.up.make_server(snapshot_host, str(snapshot_port), updates_host, str(updates_port))
        self.__states: Dict[instrument_type, update_state] = {}
        self.__to_flush = states_vector()
        asyncio.get_event_loop().create_task(self.poll())

    async def poll(self):
        self.__server.poll()
        await asyncio.sleep(0.1)

    def get_state(self, instrument: instrument_type) -> update_state:
        try:
            return self.__states[instrument]
        except KeyError:
            result = update_state(instrument)
            self.__states[instrument] = result
            return result

    def update(self, timestamp, instrument, **fields):
        state = self.get_state(instrument)
        self.__to_flush.push_back(state)
        for field, value in fields.items():
            feed.update_state_poly['double'](state.state, getattr(feed.field, field), float(value))

    def flush(self):
        self.__server.push_update(self.__to_flush)
