#!/bin/bash

# Exit on any non-zero status
set -e
pwd

# Install system dependencies
sudo apt-get update
sudo apt-get install -y --no-install-recommends apt-utils \
                                                libasound2-dev \
                                                clang-format-3.9 \
                                                ninja-build \
                                                clang-3.9 \
                                                libc++-dev \
                                                python-pip \
                                                python-virtualenv \
                                                wget \
                                                libyaml-dev \
                                                python-dev \
                                                python3-dev \
                                                gcc \
                                                git \
                                                unzip
sudo apt-get install -y --reinstall binutils


# Install cmake 3.6.x
wget https://cmake.org/files/v3.6/cmake-3.6.3-Linux-x86_64.sh
chmod +x cmake-3.6.3-Linux-x86_64.sh
sudo sh cmake-3.6.3-Linux-x86_64.sh --prefix=/usr/local --exclude-subdir

export CC=clang-3.9
export CXX=clang++-3.9

# Install virtualenv
virtualenv nfdriver_env
. nfdriver_env/bin/activate

# Install Python Packages
pip install pyyaml \
             flake8 \
             cmakelint

# Execute our python build tools
if [ -n "$BUILD_ANDROID" ]; then
    # Install Android NDK
    wget https://dl.google.com/android/repository/android-ndk-r17b-linux-x86_64.zip
    unzip -q android-ndk-r17b-linux-x86_64.zip
    mv android-ndk-r17b ~/ndk
    chmod +x -R ~/ndk

    python ci/androidlinux.py "$@"
else
    python ci/linux.py "$@"
fi
