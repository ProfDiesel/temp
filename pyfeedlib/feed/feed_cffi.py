#!/usr/bin/env python3

from io import StringIO
from pathlib import Path

from cffi import FFI
from pcpp import Preprocessor

HEADER = '/home/jonathan/dev/pipo/temp/feedlib/include/feed/feedlib.h'

p = Preprocessor()
p.parse(Path(HEADER).read_text())
p.include = lambda tokens: ()

out = StringIO()
p.write(out)

ffi = FFI()
ffi.cdef(out.getvalue())
ffi.set_source('feedlib', f'#include <{HEADER}>', libraries=['feedlib'])
ffi.compile(verbose=True)
