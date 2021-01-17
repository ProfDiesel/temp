#!/usr/bin/env fish
set -x DEV_ROOT (realpath (dirname (status --current-filename))/..)

set -x GENJUTSU_RESOURCE_PATH $DEV_ROOT/dependencies:$DEV_ROOT/precompiled_header_generator/scripts/precompiled_header_generator:$DEV_ROOT/libs/test/scripts/test_driver
#set -x GENJUTSU_TOOLSETS filesystem:clang.ToolsetExtra:3rdparty:precompiled_header_generator:test_driver
#set -x GENJUTSU_TOOLSETS filesystem:gcc:3rdparty:precompiled_header_generator
#set -x GENJUTSU_TOOLSETS filesystem:gcc.ToolsetExtra:3rdparty:precompiled_header_generator
set -x GENJUTSU_TOOLSETS filesystem:clang.ToolsetExtra:3rdparty:precompiled_header_generator 
