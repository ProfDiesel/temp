#!/usr/bin/env fish

set BUILD_DIR _build

#../precompiled_header_generator/build/src/default/pch_gen --input-file src/main.cpp -I include/ -I src/ > precompiled_header

# compdb, filter out phony targets
ninja -f $BUILD_DIR/debug/build.ninja -t compdb cxx exe | jq '[.[] | select(.command != "")]' > compile_commands.json

#clang-tidy -p . src/main.cpp 2>&1 | tee $BUILD_DIR/clang-tidy.log

#ninja $PWD/$BUILD_DIR/default/preprocessed/feed/feed.hpp
#cppcheck -j(nproc) --project=compile_commands.json -include $BUILD_DIR/default/preprocessed/feed/feed.hpp -D__USE_PREPROCESSED_FEED__HPP__ --enable=all --inconclusive --bug-hunting --check-config --xml 2> $BUILD_DIR/cppcheck.xml
cppcheck -j(nproc) --project=compile_commands.json --enable=all --inconclusive --bug-hunting --check-config --xml 2> $BUILD_DIR/cppcheck.xml

#iwyu_tool -p . --verbose > $BUILD_DIR/iwyu.log

ninja $PWD/$BUILD_DIR/low_overhead/preprocessed/main.s
llvm-mca-14 $BUILD_DIR/low_overhead/preprocessed/main.s -o $BUILD_DIR/main.mca

# dump struct layouts
ninja $PWD/$BUILD_DIR/low_overhead/preprocessed/main.i
clang -cc1 -x c++ -std=gnu++20 -fdump-record-layouts $BUILD_DIR/low_overhead/preprocessed/main.i > $BUILD_DIR/struct_layouts.txt

# complexity / duplication metrics
lizard --working_threads (nproc) --modified --extension duplicate --output_file $BUILD_DIR/lizard.log src
