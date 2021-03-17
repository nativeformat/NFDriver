#!/bin/bash
# Copyright (c) 2021 Spotify AB.
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# Exit on any non-zero status
set -e
pwd

# Install basics
apt-get -q update
apt-get install sudo

# Install system dependencies
export PYTHON_VERSION="3.7.3"
sudo apt-get update
sudo apt-get install -y --no-install-recommends apt-utils software-properties-common
sudo apt-get install -y --no-install-recommends build-essential \
                                                zlib1g-dev \
                                                libncurses5-dev \
                                                libgdbm-dev \
                                                libnss3-dev \
                                                libssl-dev \
                                                libreadline-dev \
                                                libffi-dev \
                                                wget \
                                                libbz2-dev \
                                                libsqlite3-dev \
                                                liblzma-dev
wget --tries=5 "https://www.python.org/ftp/python/$PYTHON_VERSION/Python-$PYTHON_VERSION.tgz"
tar -xf "Python-$PYTHON_VERSION.tgz"
(
    cd "Python-$PYTHON_VERSION"
    ./configure --enable-loadable-sqlite-extensions
    make -j 8
    sudo make altinstall
)
sudo apt-get install -y --no-install-recommends python3-setuptools \
                                                libasound2-dev \
                                                clang-format-3.9 \
                                                ninja-build \
                                                clang-3.9 \
                                                libc++-dev \
                                                python-virtualenv \
                                                wget \
                                                libyaml-dev \
                                                python-dev \
                                                python3-dev \
                                                git \
                                                unzip \
                                                cmake \
                                                git
sudo apt-get install -y --reinstall binutils

# Update submodules
git submodule sync
git submodule update --init --recursive

# Install virtualenv
python3.7 -m venv nfdriver_env
. nfdriver_env/bin/activate

# Install Python Packages
pip3 install pyyaml \
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
