#!/bin/bash

# Exit on any non-zero status
set -e

# Install system dependencies
sudo apt-get update
sudo apt-get install -y --no-install-recommends apt-utils
sudo apt-get install -y libasound2-dev
sudo apt-get install -y clang-format-3.9
sudo apt-get install -y ninja-build
sudo apt-get install -y clang-3.9
sudo apt-get install -y libc++-dev
sudo apt-get install -y python-pip
sudo apt-get install -y python-virtualenv
sudo apt-get install -y wget

# Install cmake 3.6.x
wget https://cmake.org/files/v3.6/cmake-3.6.3-Linux-x86_64.sh
chmod +x cmake-3.6.3-Linux-x86_64.sh
sudo sh cmake-3.6.3-Linux-x86_64.sh --skip-license
sudo ln -s /opt/cmake-3.6.3-Linux-x86_64/bin/* /usr/local/bin
ls -l /usr/local/bin

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
