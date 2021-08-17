#!/usr/bin/env python

import asyncio
from pathlib import Path
from random import lognormalvariate, uniform

from feed import Encoder


def test_encoder():
    d = Path('.')

    e = Encoder()

    with (d / 'scenario').open('wb') as out:
        mid = 100
        for _ in range(1000):
            mid += round(lognormalvariate(0, 3), 0)
            new_b0 = mid - round(abs(lognormalvariate(0, 1)), 1)
            new_o0 = mid + round(abs(lognormalvariate(0, 1)), 1)
            if new_b0 == new_o0:
                new_b0, new_bq0 = 0, 0
            else:
                new_bq0 = uniform(0, 5)
                new_oq0 = uniform(0, 5)

            packets = (
                e.encode(0, {42:dict(b0=10, bq0=5)}),
                e.encode(1000, {42:dict(b0=10, bq0=5, o0=11, oq0=1)}),
                e.encode(2500, {1:dict(b0=64, bq0=1)}),
            )
            for packet in packets:
                out.write(packet)


if __name__ == '__main__':
    test_encoder()
