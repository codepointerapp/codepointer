#! /bin/sh

set -x
set -e

BUILD_TYPE="OFFICIAL"
NAME_SUFFIX=""

if [ "$1" = "--ce" ]; then
  BUILD_TYPE="CE"
  NAME_SUFFIX="-ce"
fi

APP_NAME="codepointer"
APP_VERSION="0.1.3"
QT_VERSION="6.10.2"

NAME="${APP_NAME}-v${APP_VERSION}${NAME_SUFFIX}-x86_64"
QTDIR="/usr/lib/qt6"
export matrix_config_build_dir=ubuntu-gcc
export PATH=$QTDIR/bin:$PATH
export LD_LIBRARY_PATH=$QTDIR/lib:$LD_LIBRARY_PATH
export OUTPUT=dist/$NAME.AppImage
export QMAKE="${QTDIR}/bin/qmake"

if [ ! -x ${QMAKE} ]; then
    echo "Could not find qmake at ${QMAKE}"
    exit -1
fi

# without this, the process does not work on some systems
# export NO_STRIP=true

# Some distros use ccache on differnt locations, arch vs debian/ubuntu
if [ -x /usr/lib/ccache/clang-21 ] && [ -x /usr/lib/ccache/clang++-21 ]; then
    export CC="/usr/lib/ccache/clang-21"
    export CXX="/usr/lib/ccache/clang++-21"
elif [ -x /usr/lib/ccache/bin/clang ] && [ -x /usr/lib/ccache/bin/clang++ ]; then
    export CC="/usr/lib/ccache/bin/clang"
    export CXX="/usr/lib/ccache/bin/clang++"
elif command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
    export CC="$(command -v clang)"
    export CXX="$(command -v clang++)"
else
    echo "No usable clang found!" >&2
    return 1
fi

rm -fr "build/${matrix_config_build_dir}"
rm -fr "dist"

cmake -B "build/${matrix_config_build_dir}" -DCMAKE_BUILD_TYPE=Release -DBUILD_VERSION=${BUILD_TYPE}
cmake --build   "build/${matrix_config_build_dir}" --parallel --config Release
cmake --install "build/${matrix_config_build_dir}" --prefix dist/${matrix_config_build_dir}/usr

rm -fr dist/${APP_NAME}
mkdir -p dist/${APP_NAME}/
ln -s  ../${matrix_config_build_dir}/usr/ dist/${APP_NAME}/$NAME
tar -cjhvf dist/$NAME.tar.bz2 -C dist/${APP_NAME} .

# Qt plugin does not bundle the needed plugins, do this manually
mkdir -p "dist/${matrix_config_build_dir}/usr/plugins/iconengines/"
mkdir -p "dist/${matrix_config_build_dir}/usr/plugins/platforms/"
cp -arv $QTDIR/plugins/iconengines/* "dist/${matrix_config_build_dir}/usr/plugins/iconengines/" || true;
cp -arv $QTDIR/plugins/platforms/*.so "dist/${matrix_config_build_dir}/usr/plugins/platforms/" || true
cp -arv $QTDIR/plugins/platforms/wayland-* "dist/${matrix_config_build_dir}/usr/plugins/" || true
cp -arv $QTDIR/plugins/platform-themes/*.so "dist/${matrix_config_build_dir}/usr/plugins/platform-themes/" || true

wget -nc --no-verbose "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
wget -nc --no-verbose "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod +x linuxdeploy*.AppImage

./linuxdeploy-x86_64.AppImage --appdir "dist/${matrix_config_build_dir}" --plugin qt --output appimage
