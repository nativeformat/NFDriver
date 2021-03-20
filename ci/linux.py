#!/usr/bin/env python
'''
 * Copyright (c) 2021 Spotify AB.
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

import os
import sys

from nfbuildlinux import NFBuildLinux
from build_options import BuildOptions


def main():
    buildOptions = BuildOptions()
    buildOptions.addOption("installDependencies", "Install dependencies")
    buildOptions.addOption("lintCmake", "Lint cmake files")
    buildOptions.addOption("lintCppWithInlineChange",
                           "Lint CPP Files and fix them")

    buildOptions.addOption("makeBuildDirectory",
                           "Wipe existing build directory")
    buildOptions.addOption("generateProject", "Regenerate project")

    buildOptions.addOption("buildTargetCLI", "Build Target: CLI")
    buildOptions.addOption("buildTargetLibrary", "Build Target: Library")
    buildOptions.addOption("packageArtifacts", "Package the binary artifacts")
    buildOptions.addOption("gnuToolchain", "Build with gcc and libstdc++")
    buildOptions.addOption("llvmToolchain", "Build with clang and libc++")

    buildOptions.setDefaultWorkflow("Empty workflow", [])

    buildOptions.addWorkflow("lint", "Run lint workflow", [
        'installDependencies',
        'lintCmake',
        'lintCppWithInlineChange'
    ])

    buildOptions.addWorkflow("clang_build", "Production build with clang", [
        'llvmToolchain',
        'installDependencies',
        'lintCmake',
        'makeBuildDirectory',
        'generateProject',
        'buildTargetLibrary',
        'buildTargetCLI',
        'packageArtifacts'
    ])

    buildOptions.addWorkflow("gcc_build", "Production build with gcc", [
        'gnuToolchain',
        'installDependencies',
        'lintCmake',
        'makeBuildDirectory',
        'generateProject',
        'buildTargetLibrary',
        'buildTargetCLI',
        'packageArtifacts'
    ])

    options = buildOptions.parseArgs()
    buildOptions.verbosePrintBuildOptions(options)

    library_target = 'NFDriver'
    cli_target = 'NFDriverCLI'
    nfbuild = NFBuildLinux()

    if buildOptions.checkOption(options, 'installDependencies'):
        nfbuild.installDependencies()

    if buildOptions.checkOption(options, 'lintCmake'):
        nfbuild.lintCmake()

    if buildOptions.checkOption(options, 'lintCppWithInlineChange'):
        nfbuild.lintCPP(make_inline_changes=True)

    if buildOptions.checkOption(options, 'makeBuildDirectory'):
        nfbuild.makeBuildDirectory()

    if buildOptions.checkOption(options, 'generateProject'):
        if buildOptions.checkOption(options, 'gnuToolchain'):
            os.environ['CC'] = 'gcc'
            os.environ['CXX'] = 'g++'
            nfbuild.generateProject(gcc=True)
        elif buildOptions.checkOption(options, 'llvmToolchain'):
            os.environ['CC'] = 'clang-3.9'
            os.environ['CXX'] = 'clang++-3.9'
            nfbuild.generateProject(gcc=False)
        else:
            nfbuild.generateProject()

    if buildOptions.checkOption(options, 'buildTargetCLI'):
        nfbuild.buildTarget(cli_target)

    if buildOptions.checkOption(options, 'buildTargetLibrary'):
        nfbuild.buildTarget(library_target)

    if buildOptions.checkOption(options, 'packageArtifacts'):
        nfbuild.packageArtifacts()

if __name__ == "__main__":
    main()
