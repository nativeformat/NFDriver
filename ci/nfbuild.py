#!/usr/bin/env python

import fnmatch
import json
import os
import pprint
import shutil
import subprocess
import sys


class NFBuild(object):
    def __init__(self):
        ci_json_file = os.path.join('ci', 'ci.json')
        self.build_configuration = json.load(open(ci_json_file))
        self.pretty_printer = pprint.PrettyPrinter(indent=4)
        self.current_working_directory = os.getcwd()
        self.build_directory = 'build'
        self.output_directory = os.path.join(self.build_directory, 'output')
        self.statically_analyzed_files = []
        self.android = False

    def build_print(self, print_string):
        print print_string
        sys.stdout.flush()

    def makeBuildDirectory(self):
        if os.path.exists(self.build_directory):
            shutil.rmtree(self.build_directory)
        os.makedirs(self.build_directory)
        os.makedirs(self.output_directory)

    def vulcanDownload(self, vulcan_file, vulcan_id):
        vulcan_binary = os.path.join(
            os.path.join(
                os.path.join(
                    'tools',
                    'vulcan'),
                'bin'),
            'vulcan.py')
        extraction_folder = subprocess.check_output([
            'python',
            vulcan_binary,
            '-v',
            '-f',
            vulcan_file,
            '-i',
            'id=' + vulcan_id,
            '-p',
            vulcan_id])
        return extraction_folder.strip()

    def installCmake(self):
        cmake_vulcan_file = os.path.join(
            os.path.join(
                os.path.join(
                    os.path.join(
                        'tools',
                        'buildtools'),
                    'spotify_buildtools'),
                'software'),
            'cmake.vulcan')
        cmake_extraction_folder = self.vulcanDownload(
            cmake_vulcan_file,
            'cmake-3.8.2')
        self.cmake_binary = os.path.join(
            os.path.join(
                os.path.join(
                    cmake_extraction_folder,
                    'cmakebundle'),
                'bin'),
            'cmake')

    def installAndroidNDK(self):
        android_vulcan_file = os.path.join(
            os.path.join(
                os.path.join(
                    os.path.join(
                        'tools',
                        'buildtools'),
                    'spotify_buildtools'),
                'software'),
            'android_ndk.vulcan')
        android_extract_folder = self.vulcanDownload(
            android_vulcan_file,
            'android_ndk-r16b')
        self.android_ndk_folder = android_extract_folder
        os.environ['ANDROID_NDK'] = android_extract_folder
        os.environ['ANDROID_NDK_HOME'] = android_extract_folder

    def installAndroidSDK(self):
        androidsdk_vulcan_file = os.path.join('tools', 'android-sdk.vulcan')
        androidsdk_extract_folder = self.vulcanDownload(
            androidsdk_vulcan_file,
            'android-sdk')
        android_home = os.path.join(androidsdk_extract_folder, 'sdk')
        os.environ['ANDROID_HOME'] = android_home
        licenses_dir = os.path.join(android_home, 'licenses')
        if not os.path.isdir(licenses_dir):
            os.makedirs(licenses_dir)
        license_file = os.path.join(licenses_dir, 'android-sdk-license')
        with open(license_file, 'w') as sdk_license_file:
            sdk_license_file.write('d56f5187479451eabf01fb78af6dfcb131a6481e')
    
    def installVulcanDependencies(self, android=False):
        self.installCmake()
        if android:
          self.installAndroidNDK()
          self.installAndroidSDK()

    def installDependencies(self, android=False):
        self.android = android
        self.installVulcanDependencies(android=android)

    def generateProject(self,
                        ios=False,
                        android=False,
                        android_arm=False):
        assert True, "generateProject should be overridden by subclass"

    def buildTarget(self, target, sdk='macosx'):
        assert True, "buildTarget should be overridden by subclass"

    def lintCPPFile(self, filepath, make_inline_changes=False):
        current_source = open(filepath, 'r').read()
        clang_format_call = [self.clang_format_binary]
        if make_inline_changes:
            clang_format_call.append('-i')
        clang_format_call.append(filepath)
        new_source = subprocess.check_output(clang_format_call)
        if current_source != new_source and not make_inline_changes:
            self.build_print(
                filepath + " failed C++ lint, file should look like:")
            self.build_print(new_source)
            return False
        return True

    def lintCPPDirectory(self, directory, make_inline_changes=False):
        passed = True
        for root, dirnames, filenames in os.walk(directory):
            for filename in filenames:
                if not filename.endswith(('.cpp', '.h', '.m', '.mm')):
                    continue
                full_filepath = os.path.join(root, filename)
                if not self.lintCPPFile(full_filepath, make_inline_changes):
                    passed = False
        return passed

    def lintCPP(self, make_inline_changes=False):
        lint_result = self.lintCPPDirectory('source', make_inline_changes)
        lint_result &= self.lintCPPDirectory('include', make_inline_changes)
        if not lint_result:
            sys.exit(1)

    def lintCmakeFile(self, filepath):
        self.build_print("Linting: " + filepath)
        return subprocess.call(['cmakelint', filepath]) == 0

    def lintCmakeDirectory(self, directory):
        passed = True
        for root, dirnames, filenames in os.walk(directory):
            for filename in filenames:
                if not filename.endswith('CMakeLists.txt'):
                    continue
                full_filepath = os.path.join(root, filename)
                if not self.lintCmakeFile(full_filepath):
                    passed = False
        return passed

    def lintCmake(self):
        lint_result = self.lintCmakeFile('CMakeLists.txt')
        lint_result &= self.lintCmakeDirectory('source')
        if not lint_result:
            sys.exit(1)

    def staticallyAnalyse(self, target, include_regex=None):
        assert True, "staticallyAnalyse should be overridden by subclass"

    def buildGradle(self):
        exit_code = subprocess.call(['./gradlew', 'assemble'])
        if exit_code != 0:
            sys.exit(exit_code)
