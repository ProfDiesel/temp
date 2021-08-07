from io import StringIO
from pathlib import Path

from cffi import FFI
from pcpp import Preprocessor

p = Preprocessor()
p.parse(Path('feedlib.h').read_text())
p.include = lambda tokens: ()

out = StringIO()
p.write(out)

ffi = FFI()
ffi.cdef(out.str())
ffi.set_source('feedlib', libraries=['feedlib'])
ffi.compile(verbose=True)

