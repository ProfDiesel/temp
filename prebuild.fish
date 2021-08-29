#!/usr/bin/env fish
set TMP_ROOT (realpath (dirname (status --current-filename)))
set DEV_ROOT (realpath $TMP_ROOT/..)

set -ax PYTHONPATH $DEV_ROOT/dependencies:$DEV_ROOT/precompiled_header_generator/genjutsu_toolsets

set -x GENJUTSU_OPTS "--builddir=$TMP_ROOT"

#set -x GENJUTSU_C_TOOLSET gcc
set -x GENJUTSU_C_TOOLSET clang
#set -x GENJUTSU_C_TOOLSET afl

#find $TMP_ROOT -name 'prjdef' -executable | xargs -P0 -l1 genjutsu
./prjdef

ninja -f $TMP_ROOT/dust/_build/debug/build.ninja -t compdb cxx > $TMP_ROOT/dust/compile_commands.json
ninja -f $TMP_ROOT/feed/_build/debug/build.ninja -t compdb cxx > $TMP_ROOT/feed/compile_commands.json
