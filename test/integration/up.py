from pathlib import Path
from collections import defaultdict

import cppyy


root = Path(__file__).parent / "../.."
dependencies = Path("/home/jonathan/dev/pipo/dependencies")

cppyy.cppdef('#define __LITTLE_ENDIAN__ 1"')

for path in ("asio/asio/include", "GSL/include", "observer-ptr-lite/include"):
    cppyy.add_include_path(str(dependencies / path))
for path in ("include", "src", "test/integration"):
    cppyy.add_include_path(str(root / path))
cppyy.cppdef('#include "up.hpp"')


from cppyy.gbl import feed  # pylint: disable=import-error


class Up:
    def __init__(self):
        self.__server = feed.make_wrapped_server()
        self.__server_thread = None
        self.__states = defaultdict(lambda: feed.instrument_state())
        self.__to_flush = cppyy.gbl.std.vector[cppyy.gbl.boilerplate.observer_ptr[feed.instrument_state]]()

    def start(self):
        self.__server_thread = self.__server.run_forever()

    def update(self, timestamp, instrument, **fields):
        state = self.__states[instrument]
        self.__to_flush.push_back(state)
        for field, value in fields.items():
            feed.update_state_poly(state, getattr(feed.field, field), value)

    def flush(self):
        self.__server.push_update(self.__to_flush)
