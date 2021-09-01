#!/usr/bin/env python

import asyncio
from pathlib import Path
from random import normalvariate, uniform, lognormvariate, seed
from math import log

from feed import Encoder, Field

seed('pipololo')

def test_encoder():
    d = Path('.')

    e = Encoder()

    packets = []
    timestamp = 0
    instrument = 42

    with (d / 'scenario').open('wb') as out:
        mid = 100
        for sequence_id in range(1000):
            time_offset = lognormvariate(log(10000), log(5000))
            timestamp += int(min(round(time_offset), 3000000000))
            mid += normalvariate(0, 3)
            while True:
                b0 = round(mid - abs(normalvariate(0, .2)), 1)
                o0 = round(mid + abs(normalvariate(0, .2)), 1)
                if b0 != o0:
                    break
            bq0 = int(uniform(0, 5))
            oq0 = int(uniform(0, 5))
            if bq0 == 0:
                b0 = 0
            if oq0 == 0:
                o0 = 0

            print(timestamp / 1000000000, instrument, b0, bq0, o0, oq0)
            packets.append(e.encode(timestamp, {instrument: {Field.b0:b0, Field.bq0:bq0, Field.o0:o0, Field.oq0:oq0}}))

        for packet in packets:
            out.write(packet)


if __name__ == '__main__':
    test_encoder()
