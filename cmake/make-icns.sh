#!/usr/bin/env bash
# Builds a .icns from the SVG, falling back to the .ico when no SVG rasteriser is
# installed.
#
# Why bother with the SVG: the .ico carries 16 through 256 and nothing larger, and
# an icns wants 512 and 1024 as well. Converting the .ico upsamples those from 256,
# which is soft in the Dock and in a Finder Get Info panel. The SVG renders at any
# size, so every entry is drawn at its native resolution instead.
#
# The .ico stays as the fallback so that a machine with only the base macOS tools
# can still produce a usable - if slightly soft - icon.
#
# Usage: make-icns.sh <input.svg> <fallback.ico> <output.icns>

set -euo pipefail

svg=$1
ico=$2
out=$3

if [ ! -f "$svg" ]; then
    echo "make-icns: no such file: $svg" >&2
    exit 1
fi

if command -v rsvg-convert >/dev/null 2>&1; then
    # iconutil reads a directory of PNGs named by a convention, not a single file:
    # one entry per point size plus an @2x entry at double that size. 16/32/128/256/512
    # therefore generate ten files covering 16 through 1024.
    workdir=$(mktemp -d)
    trap 'rm -rf "$workdir"' EXIT
    iconset="$workdir/${out##*/}.iconset"
    mkdir -p "$iconset"

    for size in 16 32 128 256 512; do
        rsvg-convert --width "$size" --height "$size" "$svg" \
            --output "$iconset/icon_${size}x${size}.png"
        rsvg-convert --width $((size * 2)) --height $((size * 2)) "$svg" \
            --output "$iconset/icon_${size}x${size}@2x.png"
    done

    iconutil --convert icns --output "$out" "$iconset"
    exit 0
fi

if [ ! -f "$ico" ]; then
    echo "make-icns: no rasteriser found, and no fallback at $ico" >&2
    exit 1
fi

echo "make-icns: rsvg-convert not found, falling back to the .ico (256px ceiling)" >&2
sips -s format icns "$ico" --out "$out" >/dev/null