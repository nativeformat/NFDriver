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
    buildOptions.addOption("makeBuildDirectory",
                           "Wipe existing build directory")
    buildOptions.addOption("generateProject", "Regenerate xcode project")
    buildOptions.addOption("buildTargetIphoneSimulator",
                           "Build Target: iPhone Simulator")
    buildOptions.addOption("buildTargetIphoneOS", "Build Target: iPhone OS")
    buildOptions.addOption("staticAnalysis", "Run Static Analysis")
    buildOptions.addOption("packageArtifacts", "Package the artifacts produced by the build")
    buildOptions.setDefaultWorkflow("Empty workflow", [])

    buildOptions.addWorkflow("build", "Production Build", [
        'installDependencies',
        'makeBuildDirectory',
        'generateProject',
        'buildTargetIphoneSimulator',
        'buildTargetIphoneOS',
        'staticAnalysis',
        'packageArtifacts'
    ])

    options = buildOptions.parseArgs()
    buildOptions.verbosePrintBuildOptions(options)

    library_target = 'NFDriver'
    nfbuild = NFBuildOSX()

    if buildOptions.checkOption(options, 'installDependencies'):
        nfbuild.installDependencies()

    if buildOptions.checkOption(options, 'makeBuildDirectory'):
        nfbuild.makeBuildDirectory()

    if buildOptions.checkOption(options, 'generateProject'):
        nfbuild.generateProject(ios=True)

    if buildOptions.checkOption(options, 'buildTargetIphoneSimulator'):
        nfbuild.buildTarget(library_target,
                            sdk='iphonesimulator',
                            arch='x86_64')

    if buildOptions.checkOption(options, 'buildTargetIphoneOS'):
        nfbuild.buildTarget(library_target, sdk='iphoneos', arch='arm64')

    if buildOptions.checkOption(options, 'staticAnalysis'):
        nfbuild.staticallyAnalyse(library_target,
                                  include_regex='source/.*')
    if buildOptions.checkOption(options, "packageArtifacts"):
        nfbuild.packageArtifacts()


if __name__ == "__main__":
    main()
