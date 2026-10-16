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

# Only affects icon sharpness: without librsvg cmake falls back to converting the
# .ico, whose largest entry is 256px, so the 512 and 1024 sizes get upsampled.
if ! command -v rsvg-convert >/dev/null 2>&1; then
    echo "NOTE: rsvg-convert not found, the app icon will be generated from the .ico." >&2
    echo "      'brew install librsvg' renders it from the SVG at every size." >&2
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

hdiutil create -volname "${BUNDLE_NAME}" -srcfolder "${STAGE}" \
    -ov -format UDZO "${DMG}"
rm -rf "${STAGE}"

set +x
echo "Created ${DMG}"