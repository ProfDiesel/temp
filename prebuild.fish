#!/usr/bin/env fish
set DEV_ROOT (realpath (dirname (status --current-filename))/..)

set -ax PYTHONPATH $DEV_ROOT/dependencies:$DEV_ROOT/precompiled_header_generator/genjutsu_toolsets

set -x GENJUTSU_OPTS "--builddir=$DEV_ROOT/temp"

#set -x GENJUTSU_C_TOOLSET gcc
set -x GENJUTSU_C_TOOLSET clang
#set -x GENJUTSU_C_TOOLSET afl
./boilerplate/prjdef
./dust/prjdef
./feed/prjdef
./feedlib/prjdef

./playground/prjdef

ninja -f dust/_build/debug/build.ninja -t compdb cxx > dust/compile_commands.json
ninja -f feed/_build/debug/build.ninja -t compdb cxx > feed/compile_commands.json
