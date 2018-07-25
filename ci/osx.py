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

import sys

from nfbuildosx import NFBuildOSX
from build_options import BuildOptions


def main():
    buildOptions = BuildOptions()
    buildOptions.addOption("installDependencies", "Install dependencies")

    buildOptions.addOption("lintCmake", "Lint cmake files")
    buildOptions.addOption("lintCpp", "Lint CPP Files")
    buildOptions.addOption("lintCppWithInlineChange",
                           "Lint CPP Files and fix them")

    buildOptions.addOption("makeBuildDirectory",
                           "Wipe existing build directory")
    buildOptions.addOption("generateProject", "Regenerate xcode project")

    buildOptions.addOption("buildTargetCLI", "Build Target: CLI")
    buildOptions.addOption("buildTargetLibrary", "Build Target: Library")

    buildOptions.addOption("staticAnalysis", "Run Static Analysis")

    buildOptions.addOption("makeCLI", "Deploy CLI Binary")

    buildOptions.setDefaultWorkflow("Empty workflow", [])

    buildOptions.addWorkflow("lint", "Run lint workflow", [
        'installDependencies',
        'lintCmake',
        'lintCppWithInlineChange'
    ])

    buildOptions.addWorkflow("build", "Production Build", [
        'installDependencies',
        'lintCmake',
        'lintCpp',
        'makeBuildDirectory',
        'generateProject',
        'buildTargetCLI',
        'buildTargetLibrary',
        'staticAnalysis'
    ])

    options = buildOptions.parseArgs()
    buildOptions.verbosePrintBuildOptions(options)

    nfbuild = NFBuildOSX()
    library_target = 'NFDriver'
    cli_target = 'NFDriverCLI'

    if buildOptions.checkOption(options, 'installDependencies'):
        nfbuild.installDependencies()

    if buildOptions.checkOption(options, 'lintCmake'):
        nfbuild.lintCmake()

    if buildOptions.checkOption(options, 'lintCppWithInlineChange'):
        nfbuild.lintCPP(make_inline_changes=True)
    elif buildOptions.checkOption(options, 'lintCpp'):
        nfbuild.lintCPP(make_inline_changes=False)

    if buildOptions.checkOption(options, 'makeBuildDirectory'):
        nfbuild.makeBuildDirectory()

    if buildOptions.checkOption(options, 'generateProject'):
        nfbuild.generateProject()

    if buildOptions.checkOption(options, 'buildTargetLibrary'):
        nfbuild.buildTarget(library_target)
        if buildOptions.checkOption(options, 'staticAnalysis'):
            nfbuild.staticallyAnalyse(library_target,
                                      include_regex='source/.*')

    if buildOptions.checkOption(options, 'buildTargetCLI'):
        nfbuild.buildTarget(cli_target)
        if buildOptions.checkOption(options, 'staticAnalysis'):
            nfbuild.staticallyAnalyse(cli_target,
                                      include_regex='source/.*')


if __name__ == "__main__":
    main()
