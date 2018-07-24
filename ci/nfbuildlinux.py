#!/usr/bin/env python

import fnmatch
import os
import plistlib
import re
import shutil
import subprocess
import sys

from distutils import dir_util
from nfbuild import NFBuild


class NFBuildLinux(NFBuild):
    clang_format_binary = 'clang-format-3.9'

    def __init__(self):
        super(self.__class__, self).__init__()
        self.project_file = 'build.ninja'
        self.cmake_binary = 'cmake'
        self.android_ndk_folder = '~/ndk'

    def generateProject(self,
                        ios=False,
                        android=False,
                        android_arm=False):
        cmake_call = [
            self.cmake_binary,
            '..',
            '-GNinja']
        if android or android_arm:
            android_abi = 'x86_64'
            android_toolchain_name = 'x86_64-llvm'
            if android_arm:
                android_abi = 'arm64-v8a'
                android_toolchain_name = 'arm64-llvm'
            cmake_call.extend([
                '-DANDROID=1',
                '-DCMAKE_TOOLCHAIN_FILE=' + self.android_ndk_folder + '/build/cmake/android.toolchain.cmake',
                '-DANDROID_NDK=' + self.android_ndk_folder,
                '-DANDROID_ABI=' + android_abi,
                '-DANDROID_NATIVE_API_LEVEL=21',
                '-DANDROID_TOOLCHAIN_NAME=' + android_toolchain_name])
        cmake_result = subprocess.call(cmake_call, cwd=self.build_directory)
        if cmake_result != 0:
            sys.exit(cmake_result)

    def buildTarget(self, target, sdk='linux', arch='x86_64'):
        result = subprocess.call([
            'ninja',
            '-C',
            self.build_directory,
            '-f',
            self.project_file,
            target])
        if result != 0:
            sys.exit(result)
