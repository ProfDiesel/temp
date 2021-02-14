from pathlib import Path
from collections import defaultdict

import cppyy


root = Path(__file__).parent / '../..'
dependencies = Path('/home/jonathan/dev/pipo/dependencies')

cppyy.cppdef('#define __LITTLE_ENDIAN__ 1')

for path in ('.', 'asio/asio/include', 'fmt/include', 'GSL/include', 'observer-ptr-lite/include', 'outcome/single-header', 'outcome_extra'):
    cppyy.add_include_path(str(dependencies / path))
for path in ('include', 'src', 'test/integration'):
    cppyy.add_include_path(str(root / path))
cppyy.cppdef('#include "up.hpp"')


from cppyy.gbl import feed  # pylint: disable=import-error

from cppyy.ll import set_signals_as_exception
set_signals_as_exception(True)
import os
os.environ['EXTRA_CLING_ARGS'] = '-g -ggdb3 -O0'


class Up:
    def __init__(self, snapshot_address, updates_address):
        self.__server = feed.wrapped_server(snapshot_address, updates_address)
        self.__server_thread = self.__server.run_forever()
        self.__states = defaultdict(lambda: feed.instrument_state())
        self.__to_flush = cppyy.gbl.std.vector[cppyy.gbl.boilerplate.observer_ptr[feed.instrument_state]]()

    def update(self, timestamp, instrument, **fields):
        state = self.__states[instrument]
        self.__to_flush.push_back(state)
        for field, value in fields.items():
            feed.update_state_poly(state, getattr(feed.field, field), value)

    def flush(self):
        self.__server.push_update(self.__to_flush)
