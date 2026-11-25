#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
testbin=$(realpath "$1")
module=$(realpath "$2")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for sector in 512 4096; do
    mkdir "$work/$sector"
    "$testbin" "$module" "$work/$sector/pool.img" "$sector"
    if [ "$#" -gt 2 ]; then
        # zdb uses OpenZFS's user-space import and block walker; it does
        # not load a filesystem kernel driver or write the image.
        "$3" -e -p "$work/$sector" -C nativezfs
        "$3" -e -p "$work/$sector" -bc nativezfs
    fi
done
