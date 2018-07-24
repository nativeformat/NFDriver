#!/bin/bash

# Exit on any non-zero status
set -e

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
                                                gcc
sudo apt-get install -y --reinstall binutils


# Install cmake 3.6.x
wget https://cmake.org/files/v3.6/cmake-3.6.3-Linux-x86_64.sh
chmod +x cmake-3.6.3-Linux-x86_64.sh
sudo sh cmake-3.6.3-Linux-x86_64.sh --prefix=/usr/local --exclude-subdir
sudo whereis ld
whereis ld

export CC=clang-3.9
export CXX=clang++-3.9

# Install virtualenv
virtualenv nfdriver_env
. nfdriver_env/bin/activate

# Install Python Packages
pip install pyyaml
pip install flake8
pip install cmakelint

# Execute our python build tools
if [ -n "$BUILD_ANDROID" ]; then
    python ci/androidlinux.py
else
    python ci/linux.py "$@"
fi
