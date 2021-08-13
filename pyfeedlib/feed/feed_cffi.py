#!/usr/bin/env python3

from io import StringIO
from os import environ
from pathlib import Path

from cffi import FFI
from pcpp import Preprocessor

ROOT = Path(__file__).parent.resolve() / '../..'
HEADER = ROOT / 'feedlib/include/feed/feedlib.h'

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
        void pyfeedlib_up_on_update_float(up_field_t, float, void *user_data);
        void pyfeedlib_up_on_update_uint(up_field_t, size_t, void *user_data);
    }
""")
ffi.set_source('feedlib', f'#include <{str(HEADER)}>', libraries=['feedlib'], library_dirs=[str(ROOT / 'feedlib/_build/debug')])
ffi.compile(verbose=True)
