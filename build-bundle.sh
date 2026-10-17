#! /bin/sh

# Builds a macOS .app bundle and wraps it in a DMG, the local counterpart of
# build-appimage.sh and build.bat. The steps and their order match the macOS job in
# .github/workflows/build.yml, so a local run and a CI run cannot quietly diverge.
#
# Usage: ./build-bundle.sh [--ce|--dev] [--no-sign]

set -x
set -e

BUILD_TYPE="OFFICIAL"
NAME_SUFFIX=""
SIGN="yes"

while [ $# -gt 0 ]; do
    case "$1" in
        --ce)
            BUILD_TYPE="CE"
            NAME_SUFFIX="-ce"
            ;;
        --dev)
            BUILD_TYPE="DEV"
            NAME_SUFFIX="-dev"
            ;;
        --no-sign)
            SIGN="no"
            ;;
        -h|--help)
            echo "Usage: $0 [--ce|--dev] [--no-sign]"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
    shift
done

APP_NAME="codepointer"
BUNDLE_NAME="${APP_NAME}${NAME_SUFFIX}"

# Same source of truth cmake reads, rather than the second hardcoded copy
# build-appimage.sh keeps. Read at run time so bumping the version does not mean
# touching this file, and so bump-version.py needs no change here.
APP_VERSION=$(sed -n 's/.*setApplicationVersion("\([^"]*\)").*/\1/p' src/main.cpp)
if [ -z "${APP_VERSION}" ]; then
    echo "Could not read the version from setApplicationVersion() in src/main.cpp" >&2
    exit 1
fi

# A non-interactive ssh session gets macOS's default PATH, which leaves out
# Homebrew - so cmake, ninja and brew are all missing on a machine that has them.
# Added here rather than left to the caller, because run from an interactive shell
# the script works fine and fails only over ssh, which is the confusing way round.
for extra in /opt/homebrew/bin /usr/local/bin; do
    if [ -d "$extra" ]; then
        PATH="$PATH:$extra"
    fi
done
export PATH

# Pick an SDK the linker can actually parse, rather than trusting the toolchain's own
# default. A Command Line Tools install can ship an SDK newer than its own clang:
# MacOSX27.0.sdk lists arm64e.x1-macos in libSystem.tbd, which the bundled tapi
# linker rejects as a malformed file, so every link fails at the project() compiler
# check. xcrun reports that broken SDK as the default, so asking it is no help.
#
# The MacOSX.sdk symlink is the better guide - it points at 26.5 on such a machine,
# which matches the macOS 26 kernel and does not carry that architecture. An explicit
# SDKROOT still wins, so a broken install can be worked around without editing.
sdk_is_usable() {
    [ -n "$1" ] && [ -f "$1/usr/lib/libSystem.tbd" ] &&
        ! grep -q "arm64e\.x1" "$1/usr/lib/libSystem.tbd"
}

SDKROOT="${SDKROOT:-}"
if ! sdk_is_usable "$SDKROOT"; then
    _link_target=$(readlink /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk 2>/dev/null || true)
    if sdk_is_usable "/Library/Developer/CommandLineTools/SDKs/${_link_target}"; then
        SDKROOT="/Library/Developer/CommandLineTools/SDKs/${_link_target}"
    elif sdk_is_usable "$(xcrun --show-sdk-path 2>/dev/null || true)"; then
        SDKROOT=$(xcrun --show-sdk-path)
    else
        # Neither the symlink nor xcrun pointed at anything that links. Take any SDK
        # that does rather than none: an older-but-working SDK beats a build that
        # cannot start.
        for candidate in /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk; do
            if sdk_is_usable "$candidate"; then
                SDKROOT="$candidate"
                break
            fi
        done
    fi
fi
if [ -n "$SDKROOT" ]; then
    export SDKROOT
    echo "SDK: ${SDKROOT}" >&2
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "Could not find cmake in PATH." >&2
    echo "PATH is $PATH" >&2
    exit 1
fi

# Qt is not in one fixed place on macOS: the online installer puts it under ~/Qt,
# Homebrew somewhere else again. QTDIR overrides both.
if [ -z "${QTDIR}" ]; then
    if command -v brew >/dev/null 2>&1; then
        QTDIR=$(brew --prefix qt 2>/dev/null || true)
    fi
    if [ -z "${QTDIR}" ] || [ ! -d "${QTDIR}/bin" ]; then
        QTDIR=$(ls -d "${HOME}"/Qt/*/macos 2>/dev/null | head -1 || true)
    fi
fi
if [ -z "${QTDIR}" ] || [ ! -d "${QTDIR}/bin" ]; then
    echo "Could not find Qt." >&2
    echo "Set QTDIR to your Qt installation, e.g. QTDIR=\$HOME/Qt/6.10.3/macos $0" >&2
    exit 1
fi

export QTDIR
export PATH="${QTDIR}/bin:${PATH}"
MACDEPLOYQT="${QTDIR}/bin/macdeployqt"

if [ ! -x "${MACDEPLOYQT}" ]; then
    echo "Could not find macdeployqt at ${MACDEPLOYQT}" >&2
    exit 1
fi

# Not fatal and not a real problem: without librsvg the icon is rendered from the
# SVG at 1024 with the macOS built-in and downscaled, which still traces every size
# to the SVG. librsvg re-renders each size natively, so it is sharper at the small
# end.
if ! command -v rsvg-convert >/dev/null 2>&1; then
    echo "NOTE: rsvg-convert not found; the icon will come from the SVG via qlmanage." >&2
    echo "      'brew install librsvg' renders each size natively instead." >&2
fi

# Mirrors the macOS matrix entry, so the build directory matches what CI uses.
BUILD_DIR="macos"
ARCH=$(uname -m)
DMG="dist/${BUNDLE_NAME}-${APP_VERSION}-${ARCH}.dmg"

rm -fr "build/${BUILD_DIR}"
rm -fr dist

cmake -B "build/${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_VERSION=${BUILD_TYPE}
cmake --build "build/${BUILD_DIR}" --parallel --config Release
cmake --install "build/${BUILD_DIR}" --prefix "dist/${BUILD_DIR}/usr"

# The install prefix is dist/<dir>/usr and a bundle installs at the prefix root.
APP="dist/${BUILD_DIR}/usr/${BUNDLE_NAME}.app"
if [ ! -d "${APP}" ]; then
    echo "Expected an app bundle at ${APP}, found:" >&2
    ls -la "dist/${BUILD_DIR}/usr" >&2
    exit 1
fi

# The bundle icon is generated at build time and then installed into the bundle.
# Neither step failing stops anything - the app builds, installs and runs perfectly
# well with the generic macOS icon - so both are checked here instead of being
# discovered in Finder. The two checks are separate because they have different
# causes: no file at all is a generator problem, a file in the build tree but not in
# the bundle is a missing install rule or the wrong install order.
BUILT_ICNS="build/${BUILD_DIR}/${BUNDLE_NAME}.icns"
BUNDLED_ICNS="${APP}/Contents/Resources/${BUNDLE_NAME}.icns"

if [ ! -f "${BUILT_ICNS}" ]; then
    echo "ERROR: ${BUILT_ICNS} was not generated, so there is no icon to install." >&2
    echo "  Needs rsvg-convert or qlmanage - see the 'macOS bundle icon' cmake line." >&2
    exit 1
fi
if [ ! -f "${BUNDLED_ICNS}" ]; then
    echo "ERROR: ${BUNDLED_ICNS} is missing, so the bundle ships without its icon." >&2
    echo "  ${BUILT_ICNS} was built, so this is a missing install rule or the wrong" >&2
    echo "  install order rather than a generator problem: CMakeLists.txt needs an" >&2
    echo "  install() of the icns into Contents/Resources, declared AFTER the bundle." >&2
    exit 1
fi

# Copy Qt frameworks and the platform plugins inside the bundle, so the result runs
# on a machine with no Qt installed.
"${MACDEPLOYQT}" "${APP}" -verbose=1

# Ad-hoc, like CI. macdeployqt already ad-hoc signs what it deploys, so this signs
# the finished bundle and verifies it. Not notarised: that needs a Developer ID
# certificate and an Apple account.
if [ "${SIGN}" = "yes" ]; then
    codesign --force --deep --sign - "${APP}"
    codesign --verify --deep --strict "${APP}"
fi

# Staged rather than deployed straight from dist, so the DMG can carry the
# /Applications symlink - which is what makes it an installer rather than an
# archive: drag the app across it and it lands installed.
STAGE=$(mktemp -d)
cp -R "${APP}" "${STAGE}/"
ln -s /Applications "${STAGE}/Applications"

# Finder reads .VolumeIcon.icns out of the image root, and only honours it once the
# custom-icon flag is set on the volume itself - see the attach/detach dance below.
cp "${BUNDLED_ICNS}" "${STAGE}/.VolumeIcon.icns"

# A volume icon takes three steps, not one. SetFile -a C has to be applied to the
# mounted volume, because the custom-icon flag lives on the volume root and does not
# survive being set on a staging folder before the image is created - which is why
# the flag was being set on a directory that stopped existing. So: build a writable
# image, mount it, flag the volume, unmount, then compress to UDZO.
#
# HFS+ rather than APFS throughout, since a volume icon is not honoured on APFS.
# Includes the version on purpose. Finder caches volume icons by volume name, so a
# name reused across builds keeps serving the cached icon no matter what the image
# actually contains - which is why a rebuilt DMG can look unchanged. A per-build name
# misses that cache, and showing the version on the mounted volume is useful anyway.
VOLUME_NAME="${BUNDLE_NAME} ${APP_VERSION}"
UDRW="${STAGE}-rw.dmg"

hdiutil create -volname "${VOLUME_NAME}" -srcfolder "${STAGE}" \
    -fs HFS+ -ov -format UDRW "${UDRW}"

# Parsed by taking everything from /Volumes/ to end of line, not by awk's $NF: a
# volume name containing a space splits into several whitespace fields, so $NF
# returned only the last word and SetFile then failed with "File Not Found".
MOUNT_POINT=$(hdiutil attach "${UDRW}" -readwrite -noverify -noautoopen \
    | sed -n 's|.*\(/Volumes/.*\)|\1|p' | head -1)
if [ -z "${MOUNT_POINT}" ]; then
    echo "ERROR: could not mount ${UDRW} to set the volume icon" >&2
    exit 1
fi
SetFile -a C "${MOUNT_POINT}"
sync
hdiutil detach "${MOUNT_POINT}"

rm -f "${DMG}"
hdiutil convert "${UDRW}" -format UDZO -o "${DMG}"
rm -rf "${STAGE}" "${UDRW}"

set +x
echo "Created ${DMG}"