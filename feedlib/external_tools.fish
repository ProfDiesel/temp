#!/usr/bin/env fish

# compdb, filter out phony targets
ninja -f _build/debug/build.ninja -t compdb cxx exe | jq '[.[] | select(.command != "")]' > compile_commands.json

clang-tidy-13 -p . --extra-arg="-isystem/usr/lib/llvm-13/include/c++/v1" src/feedlib.cpp 2>&1 | tee _build/clang-tidy.log
