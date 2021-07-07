from pathlib import Path
import json
import shlex
import re

import cppyy

root = Path(__file__).parent / '..'
flavour = 'debug'
with open('compile_commands.json') as commands_file:
    for command in json.load(commands_file):
        if (root / f'build/{flavour}/test/pic/up.o').resolve() == Path(command['output']).resolve():
            for arg in shlex.split(command['command']):
                if match := re.match('(-I|-isystem)(?P<directory>.*)', arg):
                    cppyy.add_include_path(match.groupdict()['directory'])
cppyy.add_include_path(str(root / 'test'))
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
