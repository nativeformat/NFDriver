#!/usr/bin/env python
'''
 * Copyright (c) 2018 Spotify AB.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
'''

import fnmatch
import os
import plistlib
import re
import shutil
import subprocess
import sys

from distutils import dir_util
from nfbuild import NFBuild


class NFBuildWindows(NFBuild):
    def __init__(self):
        super(self.__class__, self).__init__()
        self.project_file = 'NFDriver.sln'
        self.cmake_binary = 'cmake'
        self.android = False

    def generateProject(self,
                        ios=False,
                        android=False,
                        android_arm=False):
        cmake_call = [
            self.cmake_binary,
            '..',
            '-G']
        if android or android_arm:
            self.android = True
            self.build_project = 'build.ninja'
            android_abi = 'x86_64'
            android_toolchain_name = 'x86_64-llvm'
            if android_arm:
                android_abi = 'arm64-v8a'
                android_toolchain_name = 'arm64-llvm'
            cmake_call.extend([
                'Ninja',
                '-DANDROID=1',
                '-DCMAKE_TOOLCHAIN_FILE=' + self.android_ndk_folder + '/build/cmake/android.toolchain.cmake',
                '-DANDROID_NDK=' + self.android_ndk_folder,
                '-DANDROID_ABI=' + android_abi,
                '-DANDROID_NATIVE_API_LEVEL=21',
                '-DANDROID_TOOLCHAIN_NAME=' + android_toolchain_name])
        else:
            cl_exe = 'cl.exe'
            rc_exe = 'rc.exe'
            link_exe = 'link.exe'
            cmake_call.extend([
                'Visual Studio 15 2017 Win64'])
        cmake_result = subprocess.call(cmake_call, cwd=self.build_directory)
        if cmake_result != 0:
            sys.exit(cmake_result)

    def buildTarget(self, target, sdk='macosx', arch='x86_64'):
        result = 0
        if self.android:
            result = subprocess.call([
                self.ninja_binary,
                '-C',
                self.build_directory,
                '-f',
                self.project_file,
                target])
        else:
            result = subprocess.call([
                'msbuild.exe',
                os.path.join(self.build_directory, 'NFDriver.sln'),
                '/t:NFDriver;' + target])
        if result != 0:
            sys.exit(result)
