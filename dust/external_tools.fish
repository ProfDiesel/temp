#!/usr/bin/env fish

set BUILD_DIR _build

ninja $PWD/$BUILD_DIR/low_overhead/preprocessed/main.s
llvm-mca-14 $BUILD_DIR/low_overhead/preprocessed/main.s -o $BUILD_DIR/main.mca

# dump struct layouts
ninja $PWD/$BUILD_DIR/low_overhead/preprocessed/main.i
clang -cc1 -x c++ -std=gnu++20 -fdump-record-layouts $BUILD_DIR/low_overhead/preprocessed/main.i > $BUILD_DIR/struct_layouts.txt

# complexity / duplication metrics
lizard --working_threads (nproc) --modified --extension duplicate --output_file $BUILD_DIR/lizard.log src
