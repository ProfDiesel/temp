# ---
# jupyter:
#   jupytext:
#     text_representation:
#       extension: .py
#       format_name: light
#       format_version: '1.5'
#       jupytext_version: 1.9.1
#   kernelspec:
#     display_name: Python 3
#     language: python
#     name: python3
# ---

from IPython.core.display import display, HTML
display(HTML("<style>.container { width:100% !important; }</style>"))


# +
from pathlib import Path

import cppyy

root = Path('/home/jonathan/dev/pipo/temp')
dependencies = Path('/home/jonathan/dev/pipo/dependencies')

for d in ('notebook', 'src', 'include'):
    cppyy.add_include_path(str(root / d))

cppyy.cppdef('#include "trigger_helper.hpp"')

cppyy.add_library_path(str(root / 'build/notebook/debug'))
cppyy.load_library('trigger_helper')

#backtest = cppyy.gbl.backtest
feed = cppyy.gbl.feed
# -

config = '''\
entrypoint.instant_threshold <- 2;
entrypoint.threshold <- 3;
entrypoint.period <- 10;'''
trigger = cppyy.gbl.make_trigger(config)

update = feed.update(0, 0)
cppyy.gbl.run(trigger, 0, update, lambda timestamp: True)

'''
import dpkt
import datashader
import parquet

for timestamp, update in read_pcap():
    backtest.run(trigger, timestamp, update, lambda timestamp: return True)
'''
