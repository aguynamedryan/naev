#!/bin/bash

# STEAM DEPLOYMENT SCRIPT FOR NAEV
# Requires SteamCMD to be installed within a Github Actions ubuntu-latest runner.
#
# This script should be run after downloading all build artefacts
# If -n is passed to the script, a nightly build will be generated
# and uploaded to Steam
#
# Pass in [-d] [-n] (set this for nightly builds) [-p] (set this for pre-release builds.) [-c] (set this for CI testing) -s <SCRIPTROOT> (Sets path to look for additional steam scripts.) -t <TEMPPATH> (Steam build artefact location) -o <STEAMPATH> (Steam dist output directory)

set -e

# Defaults
NIGHTLY="false"
PRERELEASE="false"
SCRIPTROOT="$(pwd)"
DRYRUN="false"

while getopts dnpc:s:t:o: OPTION "$@"; do
    case $OPTION in
    d)
        set -x
        ;;
    n)
        NIGHTLY="true"
        ;;
    p)
        PRERELEASE="true"
        ;;
    c)
        DRYRUN="true"
        ;;
    s)
        SCRIPTROOT="${OPTARG}"
        ;;
    t)
        TEMPPATH="${OPTARG}"
        ;;
    o)
        STEAMPATH="${OPTARG}"
        ;;
    esac
done

retry() {
    local -r -i max_attempts="$1"; shift
    local -i attempt_num=1
    until "$@"
    do
        if ((attempt_num==max_attempts))
        then
            echo "Attempt $attempt_num failed and there are no more attempts left!"
            return 1
        else
            echo "Attempt $attempt_num failed! Trying again in $attempt_num seconds..."
            sleep $((attempt_num++))
        fi
    done
}

# Make Steam dist path if it does not exist
mkdir -p "$STEAMPATH"/content/lin64
mkdir -p "$STEAMPATH"/content/macos
mkdir -p "$STEAMPATH"/content/win64
mkdir -p "$STEAMPATH"/content/ndata

# Move Depot Scripts to Steam staging area
cp -v -r "$SCRIPTROOT"/scripts "$STEAMPATH"

# Move all build artefacts to deployment locations
# Move Linux binary and set as executable
cp -v "$TEMPPATH"/naev-steamruntime/naev.x64 "$STEAMPATH"/content/lin64
chmod +x "$STEAMPATH"/content/lin64/naev.x64

# Move macOS bundle to deployment location
unzip "$TEMPPATH/naev-macos/naev-macos.zip" -d "$STEAMPATH/content/macos/"

# Unzip Windows binary and DLLs and move to deployment location
tar -Jxf "$TEMPPATH/naev-win64/steam-win64.tar.xz" -C "$STEAMPATH/content/win64"
mv "$STEAMPATH"/content/win64/naev*.exe "$STEAMPATH/content/win64/naev.exe"

# Move data to deployment location
tar -Jxf "$TEMPPATH/naev-ndata/steam-ndata.tar.xz" -C "$STEAMPATH/content/ndata"

# Runs STEAMCMD, and builds the app as well as all needed depots.

if [ "$DRYRUN" == "false" ]; then

    # Trigger 2FA request and get 2FA code
    steamcmd +login $STEAMCMD_USER $STEAMCMD_PASS +quit || true

    # Wait a bit for the email to arrive
    sleep 60s
    python3 "$SCRIPTROOT/2fa/get_2fa.py"
    STEAMCMD_TFA="$(<"$SCRIPTROOT/2fa/2fa.txt")"

    if [ "$NIGHTLY" == "true" ]; then
        # Run steam upload with 2fa key
        retry 5 steamcmd +login $STEAMCMD_USER $STEAMCMD_PASS $STEAMCMD_TFA +run_app_build_http $STEAMPATH/scripts/app_build_598530_nightly.vdf +quit
    else
        if [ "$PRERELEASE" == "true" ]; then
            # Run steam upload with 2fa key
            retry 5 steamcmd +login $STEAMCMD_USER $STEAMCMD_PASS $STEAMCMD_TFA +run_app_build_http $STEAMPATH/scripts/app_build_598530_prerelease.vdf +quit

        elif [ "$PRERELEASE" == "false" ]; then
            mkdir -p "$STEAMPATH"/content/soundtrack
            # Move soundtrack stuff to deployment area
            cp "$TEMPPATH"/naev-steam-soundtrack/*.* "$STEAMPATH/content/soundtrack"

            # Run steam upload with 2fa key
            retry 5 steamcmd +login $STEAMCMD_USER $STEAMCMD_PASS $STEAMCMD_TFA +run_app_build_http $STEAMPATH/scripts/app_build_598530_release.vdf +quit
            retry 5 steamcmd +login $STEAMCMD_USER $STEAMCMD_PASS +run_app_build_http $STEAMPATH/scripts/app_build_1411430_soundtrack.vdf +quit

        else
            echo "Something went wrong determining if this is a PRERELEASE or not."
        fi
    fi
elif [ "$DRYRUN" == "true" ]; then
    # Trigger 2FA request and get 2FA code
    echo "steamcmd login"

    #Wait a bit for the email to arrive
    echo "2fa script"

    if [ "$NIGHTLY" == "true" ]; then
        # Run steam upload with 2fa key
        echo "steamcmd nightly build"
        ls -l -R "$STEAMPATH"
    else
        if [ "$PRERELEASE" == "true" ]; then
            # Run steam upload with 2fa key
            echo "steamcmd PRERELEASE build"
            ls -l -R "$STEAMPATH"
        elif [ "$PRERELEASE" == "false" ]; then
            # Move soundtrack stuff to deployment area
            echo "copy soundtrack files"

            # Run steam upload with 2fa key
            echo "steamcmd release build"
            echo "steamcmd soundtrack build"
            ls -l -R "$STEAMPATH"

        else
            echo "Something went wrong determining if this is a PRERELEASE or not."
        fi
    fi

else
    echo "Something went wrong determining which mode to run this script in."
    exit 1
fi
