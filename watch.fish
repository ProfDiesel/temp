#!/usr/bin/env fish
watchman-make -p '**/*.cpp' '**/*.hpp' --run 'ninja debug'
