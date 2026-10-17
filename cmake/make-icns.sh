#!/usr/bin/env bash
# Builds a .icns from the SVG, preferring tools that can actually rasterise it.
#
# Why the SVG and not the .ico: the .ico carries 16 through 256 and nothing larger,
# and an icns wants 512 and 1024 as well. Converting the .ico upsamples those from
# 256, which is soft in the Dock and in a Finder Get Info panel. The SVG renders at
# any size, so every entry is drawn at its native resolution instead.
#
# Three tiers, best first, so no extra installation is strictly required:
#
#   1. rsvg-convert  renders each size natively from the SVG. Needs librsvg
#                    (brew install librsvg), which is the only way to get a true
#                    multi-size render.
#   2. qlmanage      renders the SVG once at 1024 with a macOS built-in, then sips
#                    downscales to the remaining sizes. Needs nothing installed.
#   3. sips          converts the .ico instead, giving up the 512 and 1024 sizes.
#                    Always available, so a build never fails over an icon.
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

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
iconset="$workdir/${out##*/}.iconset"

# iconutil reads a directory of PNGs named by a convention, not a single file: one
# entry per point size plus an @2x entry at double that size. 16/32/128/256/512
# therefore generate ten files covering 16 through 1024.
write_iconset_from_png() {
    local source=$1
    mkdir -p "$iconset"
    local size
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$source" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        sips -z $((size * 2)) $((size * 2)) "$source" \
            --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done
}

if command -v rsvg-convert >/dev/null 2>&1; then
    mkdir -p "$iconset"
    for size in 16 32 128 256 512; do
        rsvg-convert --width "$size" --height "$size" "$svg" \
            --output "$iconset/icon_${size}x${size}.png"
        rsvg-convert --width $((size * 2)) --height $((size * 2)) "$svg" \
            --output "$iconset/icon_${size}x${size}@2x.png"
    done
    iconutil --convert icns --output "$out" "$iconset"
    echo "make-icns: rendered every size from the SVG (librsvg)" >&2
    exit 0
fi

if command -v qlmanage >/dev/null 2>&1; then
    # One render at the largest size the iconset needs, then downscaled. Not as good
    # as rendering each size, since a 16px entry is a resample rather than a
    # re-render, but every entry still traces back to the SVG rather than the .ico.
    #
    # Whatever qlmanage produced is found by glob rather than by guessing its name:
    # Quick Look names a thumbnail after the whole input filename, so codepointer.svg
    # comes out as codepointer.svg.png, not codepointer.png. Testing for the latter
    # made this tier look like a failure and quietly sent every build to the .ico.
    if qlmanage -t -s 1024 -o "$workdir" "$svg" >/dev/null 2>&1; then
        rendered=$(find "$workdir" -maxdepth 1 -name '*.png' -print -quit 2>/dev/null || true)
    else
        rendered=""
    fi
    if [ -n "$rendered" ]; then
        echo "make-icns: qlmanage rendered ${rendered##*/}" >&2
        write_iconset_from_png "$rendered"
        if iconutil --convert icns --output "$out" "$iconset"; then
            echo "make-icns: rendered from the SVG at 1024 and downscaled (qlmanage);" >&2
            echo "          install librsvg for a native render at every size" >&2
            exit 0
        fi
    fi
    echo "make-icns: qlmanage could not render $svg, trying the .ico" >&2
fi

if [ ! -f "$ico" ]; then
    echo "make-icns: no SVG rasteriser worked, and no fallback at $ico" >&2
    exit 1
fi

# Always available on macOS, so this is the tier that never leaves the build broken -
# at the cost of the 512 and 1024 sizes, which get upsampled from the .ico's 256.
sips -s format icns "$ico" --out "$out" >/dev/null
echo "make-icns: fell back to the .ico (256px ceiling, softer at large sizes)" >&2