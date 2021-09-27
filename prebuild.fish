#!/usr/bin/env fish
set TMP_ROOT (realpath (dirname (status --current-filename)))
set DEV_ROOT (realpath $TMP_ROOT/..)

set -ax PYTHONPATH $DEV_ROOT/dependencies:$DEV_ROOT/precompiled_header_generator/genjutsu_toolsets

set -x GENJUTSU_OPTS "--builddir=$TMP_ROOT"

#set -x GENJUTSU_C_TOOLSET gcc
set -x GENJUTSU_C_TOOLSET clang
#set -x GENJUTSU_C_TOOLSET afl

./prjdef

# compdb, filter out phony targets
ninja -f $TMP_ROOT/_build/default/build.ninja -t compdb cxx exe | jq '[.[] | select(.command != "")]' > $TMP_ROOT/compile_commands.json
