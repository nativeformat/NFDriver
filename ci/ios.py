#!/usr/bin/env python

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

    buildOptions.setDefaultWorkflow("Empty workflow", [])

    buildOptions.addWorkflow("build", "Production Build", [
        'installDependencies',
        'makeBuildDirectory',
        'generateProject',
        'buildTargetIphoneSimulator',
        'buildTargetIphoneOS',
        'staticAnalysis'
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


if __name__ == "__main__":
    main()
