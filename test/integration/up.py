#!/usr/bin/env python3
import cppyy
from pathlib import Path

root = Path(__file__) / '../..'

for path in ('include', 'src', 'test/integration'):
  cppyy.add_include_path(str(root / path))
cppyy.cppdef('#include "up.hpp"')

from cppyy.gbl import feed

class Up:
  def __init__(self):
    self.__server = server_wrapper()
    self.__server_thread = self.__server.run_forever()
    self.__states = defaultdict(lambda: feed.instrument_state())
    self.__to_flush = cppyy.gbl.std.vector[observer_ptr[feed.instrument_state]]()

  def start():
    self.__server_thread = self.__server.run_forever()

  def update(self, timestamp, instrument, **fields):
    state = self.__states[instrument]
    self.__to_flush.push_back(state)
    for field, value in fields.items():
      feed.update_state(state, getattr(feed.field, field), value)

  def flush(self):
    sw.push_update(self.to_flush)

