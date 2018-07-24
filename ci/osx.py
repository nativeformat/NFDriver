#!/usr/bin/env python

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
