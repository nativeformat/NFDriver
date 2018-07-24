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

    def installDependencies(self, android=False):
        self.android = android

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
