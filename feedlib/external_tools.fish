#!/usr/bin/env fish

set BUILD_DIR _build

# compdb, filter out phony targets
ninja -f $BUILD_DIR/debug/build.ninja -t compdb cxx exe | jq '[.[] | select(.command != "")]' > compile_commands.json

clang-tidy-13 -p . --extra-arg="-isystem/usr/lib/llvm-13/include/c++/v1" src/feedlib.cpp 2>&1 | tee $BUILD_DIR/clang-tidy.log
