#!/usr/bin/env fish

#../precompiled_header_generator/build/src/default/pch_gen --input-file src/main.cpp -I include/ -I src/ > precompiled_header

# compdb, filter out phony targets
ninja -f build/debug/build.ninja -t compdb cxx exe | jq '[.[] | select(.command != "")]' > compile_commands.json

clang-tidy-13 -p . src/main.cpp 2>&1 | tee build/clang-tidy.log
cppcheck -j($nproc) --project=compile_commands.json -include build/default/preprocessed/feed/feed.hpp -D__USE_PREPROCESSED_FEED__HPP__ --enable=all --inconclusive --bug-hunting --check-config --xml --clang=clang-12 2> build/cppcheck.xml
iwyu_tool -p . --verbose

ninja $PWD/build/low_overhaed/preprocessed/main.s
llvm-mca-13 build/low_overhaed/preprocessed/main.s -o build/low_overhaed/bin/src/main.mca

# dump struct layouts
ninja $PWD/build/low_overhaed/preprocessed/main.i
clang-13 -cc1 -x c++ -std=gnu++20 -fdump-record-layouts build/low_overhead/preprocessed/main.i > build/struct_layouts.txt
