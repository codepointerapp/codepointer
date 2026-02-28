FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Dependencies from hack.md (Debian/Ubuntu section)
RUN apt-get update && apt-get install -y \
    python3-pip \
    pipx \
    build-essential \
    xorg-dev \
    libcups2-dev \
    libxcb-cursor-dev \
    libtiff5-dev \
    clang \
    clang-tools \
    ninja-build \
    cmake \
    ccache \
    mold \
    wget \
    curl \
    git \
    file \
    libfuse2 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install aqtinstall as per hack.md
# We use --break-system-packages because Ubuntu 24.04 blocks global pip install
RUN pip3 install --break-system-packages aqtinstall

# Install Qt as per hack.md
# Note: ~/qt for root is /root/qt
RUN aqt install-qt linux desktop 6.10.1 -m all -O /root/qt
RUN aqt install-tool linux desktop tools_qtcreator_gui -O /root/qt

# Environment variables as per hack.md
ENV PATH="/root/qt/6.10.1/gcc_64/bin:${PATH}"
ENV NINJA_STATUS="[%f/%t %p %P][%w + %W] "
# Default ccache paths (build-appimage.sh will try to detect them as well)
ENV CC=/usr/lib/ccache/gcc
ENV CXX=/usr/lib/ccache/g++

# Required for linuxdeploy to run inside Docker
ENV APPIMAGE_EXTRACT_AND_RUN=1
ENV HOME=/root

WORKDIR /app

# Default command runs the build script
CMD ["./build-appimage.sh"]
