from .up import Encoder
from pathlib import Path

e = Encoder()

d = Path('.')

with (d / 'scenario').open('wb') as out:
    packets = (
        e.encode(0, {42:dict(b0=10, bq0=5)}),
        e.encode(1000, {42:dict(b0=10, bq0=5, o0=11, oq0=1)}),
        e.encode(2500, {1:dict(b0=64, bq0=1)}),
    )
    print(packets)
    for packet in packets:
        out.write(packet)
