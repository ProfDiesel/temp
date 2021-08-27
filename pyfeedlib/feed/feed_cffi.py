#!/usr/bin/env python3

from io import StringIO
from os import environ
from pathlib import Path

from cffi import FFI
from pcpp import Preprocessor

ROOT = Path(__file__).parent.resolve() / '../..'
HEADER = ROOT / 'feed/include/feed/feedlib.h'

p = Preprocessor()
p.parse(HEADER.read_text())
p.include = lambda tokens: ()

out = StringIO()
p.write(out)

ffi = FFI()
ffi.cdef(out.getvalue() + """
    extern "Python"
    {
        void pyfeedlib_up_on_message(up_instrument_id_t, void *user_data);
        void pyfeedlib_up_on_update_float(enum up_field, float, void *user_data);
        void pyfeedlib_up_on_update_uint(enum up_field, size_t, void *user_data);
    }
""")
ffi.set_source('feedlib', f'#include <{str(HEADER)}>', libraries=['feedlib'], library_dirs=[str(ROOT / 'feed/_build/debug')])
ffi.compile(verbose=True)
