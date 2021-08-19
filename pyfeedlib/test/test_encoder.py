#!/usr/bin/env python

import asyncio
from pathlib import Path
from random import lognormvariate, uniform
from math import log

from feed import Encoder


def test_encoder():
    d = Path('.')

    e = Encoder()

    packets = []
    timestamp = 0
    instrument = 42

    with (d / 'scenario').open('wb') as out:
        mid = 100
        for _ in range(1000):
            timestamp += max(round(lognormvariate(log(1000), log(5)), 0), 10000000)
            mid += round(lognormvariate(0, 3), 0)
            while True:
                b0 = mid - round(abs(lognormvariate(0, 1)), 1)
                o0 = mid + round(abs(lognormvariate(0, 1)), 1)
                if b0 != o0:
                    break
            bq0 = uniform(0, 5)
            oq0 = uniform(0, 5)

            packets.append(e.encode(timestamp, {instrument: dict(b0=b0, bq0=bq0, o0=o0, oq0=oq0)}))

        for packet in packets:
            out.write(packet)


if __name__ == '__main__':
    test_encoder()
