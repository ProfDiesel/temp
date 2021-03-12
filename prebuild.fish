#!/usr/bin/env fish
set DEV_ROOT (realpath (dirname (status --current-filename))/..)

set -ax PYTHONPATH $DEV_ROOT/dependencies

./prjdef

ninja -f build/debug/build.ninja -t compdb cxx > compile_commands.json
