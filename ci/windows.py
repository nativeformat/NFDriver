#!/usr/bin/env python

import sys

from nfbuildwindows import NFBuildWindows


def main():
    library_target = 'NFDriver'
    cli_target = 'NFDriverCLI'
    nfbuild = NFBuildWindows()
    nfbuild.build_print("Installing Dependencies")
    nfbuild.installDependencies()
    # Make our main build artifacts
    nfbuild.build_print("C++ Build Start (x86)")
    nfbuild.makeBuildDirectory()
    nfbuild.generateProject()
    targets = [library_target, cli_target]
    for target in targets:
        nfbuild.buildTarget(target)


if __name__ == "__main__":
    main()
