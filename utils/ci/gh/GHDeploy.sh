#!/bin/bash

# GITHUB DEPLOYMENT SCRIPT FOR NAEV
#
# This script should be run after downloading all build artefacts.
# It also makes use of ENV variable GH_TOKEN to login. ensure this is exported.
#
#
# Pass in [-d] [-n] (set this for nightly builds) [-p] (set this for pre-release builds.) [-c] (set this for CI testing) -t <TEMPPATH> (build artefact location) -o <OUTDIR> (dist output directory) -r <TAGNAME> (tag of release *required*) -g <REPONAME> (defaults to naev/naev)

set -e

# Defaults
NIGHTLY="false"
PRERELEASE="false"
REPO="naev/naev"
TEMPPATH="$(pwd)"
OUTDIR="$(pwd)/dist"
DRYRUN="false"
REPONAME="naev/naev"

while getopts dnpct:o:r:g: OPTION "$@"; do
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
    t)
        TEMPPATH="${OPTARG}"
        ;;
    o)
        OUTDIR="${OPTARG}"
        ;;
    r)
        TAGNAME="${OPTARG}"
        ;;
    g)
        REPONAME="${OPTARG}"
        ;;
    esac
done

if [[ -z "$TAGNAME" ]]; then
    echo "usage: `basename $0` [-d] [-n] (set this for nightly builds) [-p] (set this for pre-release builds.) [-c] (set this for CI testing) -t <TEMPPATH> (build artefact location) -o <OUTDIR> (dist output directory) -r <TAGNAME> (tag of release *required*) -g <REPONAME> (defaults to naev/naev)"
    exit -1
fi

if ! [ -x "$(command -v github-assets-uploader)" ]; then
    echo "You don't have github-assets-uploader in PATH"
    exit -1
else
    GH="github-assets-uploader"
fi

run_gau () {
    $GH -retry 5 -logtostderr $@
}

# Collect date and assemble the VERSION suffix

BUILD_DATE="$(date +%Y%m%d)"
VERSION="$(<"$TEMPPATH/naev-version/VERSION")"

if [ "$NIGHTLY" == "true" ]; then
    SUFFIX="$VERSION+DEBUG.$BUILD_DATE"
else
    SUFFIX="$VERSION"
fi


# Make dist path if it does not exist
mkdir -p "$OUTDIR"/dist
mkdir -p "$OUTDIR"/lin64
mkdir -p "$OUTDIR"/macos
mkdir -p "$OUTDIR"/win64
mkdir -p "$OUTDIR"/soundtrack

# Move all build artefacts to deployment locations
# Move Linux binary and set as executable
cp "$TEMPPATH"/naev-linux-x86-64/*.AppImage "$OUTDIR"/lin64/naev-$SUFFIX-linux-x86-64.AppImage
chmod +x "$OUTDIR"/lin64/naev-$SUFFIX-linux-x86-64.AppImage

# Move macOS bundle to deployment location
cp "$TEMPPATH"/naev-macos/*.zip -d "$OUTDIR"/macos/naev-$SUFFIX-macos.zip

# Move Windows installer to deployment location
cp "$TEMPPATH"/naev-win64/naev*.exe "$OUTDIR"/win64/naev-$SUFFIX-win64.exe

# Move Dist to deployment location
cp "$TEMPPATH"/naev-dist/source.tar.xz "$OUTDIR"/dist/naev-$SUFFIX-source.tar.xz

# Move Soundtrack to deployment location if this is a release.
if [ "$NIGHTLY" == "true" ] || [ "$PRERELEASE" == "true" ]; then
    echo "not preparing soundtrack"
else
    cp "$TEMPPATH"/naev-soundtrack/naev-*-soundtrack.zip "$OUTDIR"/dist/naev-$SUFFIX-soundtrack.zip
fi

# Push builds to github via gh
#
# Media types taken from: https://www.iana.org/assignments/media-types/media-types.xhtml
#

if [ "$DRYRUN" == "false" ]; then
    run_gau -version
    run_gau -repo "$REPONAME" -tag "$TAGNAME" -token "$GH_TOKEN" -f "$OUTDIR"/lin64/naev-"$SUFFIX"-linux-x86-64.AppImage -mediatype "application/octet-stream" -overwrite
    run_gau -repo "$REPONAME" -tag "$TAGNAME" -token "$GH_TOKEN" -f "$OUTDIR"/macos/naev-"$SUFFIX"-macos.zip -mediatype "application/zip" -overwrite
    run_gau -repo "$REPONAME" -tag "$TAGNAME" -token "$GH_TOKEN" -f "$OUTDIR"/win64/naev-"$SUFFIX"-win64.exe -mediatype "application/vnd.microsoft.portable-executable" -overwrite
    if [ "$NIGHTLY" == "false" ] && [ "$PRERELEASE" == "false" ]; then
        run_gau -repo "$REPONAME" -tag "$TAGNAME" -token "$GH_TOKEN" -f "$OUTDIR"/dist/naev-"$SUFFIX"-soundtrack.zip -mediatype "application/zip" -overwrite
    fi
    run_gau -repo "$REPONAME" -tag "$TAGNAME" -token "$GH_TOKEN" -f "$OUTDIR"/dist/naev-"$SUFFIX"-source.tar.xz -mediatype "application/x-gtar" -overwrite
elif [ "$DRYRUN" == "true" ]; then
    run_gau -version
    if [ "$NIGHTLY" == "true" ]; then
        echo "github nightly upload"
    elif [ "$PRERELEASE" == "true" ]; then
        echo "github beta upload"
    else
        echo "github release upload"
        echo "github soundtrack upload"
    fi
    ls -l -R "$OUTDIR"
else
    echo "Something went wrong determining which mode to run this script in."
    exit 1
fi
