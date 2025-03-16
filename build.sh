#!/bin/bash
if [ -z $1 ]; then
    make prepare && make jenkins && make pack-dotnet
else
    case $1 in
        Prepare)
            make prepare
        ;;
        PrepareExternal)
            make prepare-external-git-dependencies
        ;;
        Build)
            make jenkins
        ;;
        Pack)
            make pack-dotnet
        ;;
        Installers)
            make create-installers
        ;;
        Everything)
            make prepare && make jenkins && make pack-dotnet
        ;;
    esac
fi