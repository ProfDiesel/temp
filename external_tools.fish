#!/usr/bin/env fish

set BUILD_DIR _build

clang-tidy -p . src/main.cpp 2>&1 | tee $BUILD_DIR/clang-tidy.log
cppcheck -j(nproc) --project=compile_commands.json --enable=all --inconclusive --bug-hunting --check-config --xml 2> $BUILD_DIR/cppcheck.xml
iwyu_tool -p . --verbose > $BUILD_DIR/iwyu.log
