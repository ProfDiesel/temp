#!/usr/bin/env fish
set DEV_ROOT (realpath (dirname (status --current-filename))/..)

set -ax PYTHONPATH $DEV_ROOT/dependencies

set -x GENJUTSU_C_TOOLSET clang
./prjdef
./ppf/prjdef

ninja -f build/debug/build.ninja -t compdb cxx > compile_commands.json
ninja -f ppf/build/debug/build.ninja -t compdb cxx > ppf/compile_commands.json
