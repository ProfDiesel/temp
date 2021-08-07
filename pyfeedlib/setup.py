#!/usr/bin/env python3
from setuptools import setup

setup(
    ...,
    setup_requires=['cffi', 'pytest-runner'],
    cffi_modules=['package/foo_build.py:ffibuilder'],
    install_requires=['cffi'],
)setup()
