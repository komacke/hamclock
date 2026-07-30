#!/bin/bash

# Variables to set
IMAGE_BASE=komacke/hamclock
HC_UID=1199
HC_GID=1199
ON_TAG=false

# Don't set anything past here
TAG=$(git describe --exact-match --tags 2>/dev/null)
if [ $? -ne 0 ]; then
    echo "NOTE: Not currently on a tag. Using 'latest'."
    TAG=latest
    GIT_VERSION=$(git rev-parse --short HEAD)
    ON_TAG=false
else
    GIT_VERSION=$TAG
    ON_TAG=true
fi

IMAGE=$IMAGE_BASE:$TAG
CONTAINER=${IMAGE_BASE##*/}

# Get our directory locations in figured out
HERE="$(cd "$(dirname "$0")" && pwd)"
THIS="$(basename "$0")"
cd $HERE

usage() {
    cat<<EOF
$THIS: 

    Builds the latest docker image based on the current git branch. It will figure out
    if on a git tag and will use that for the docker image tag. Otherwise falls back
    to 'latest'.

    -m: multi-platform image buld for: linux/amd64 linux/arm64 linux/arm/v7
        - argument is ignored when run with -c
        - remember to setup a buildx container: 
            docker buildx create --name ohb --driver docker-container --use
            docker buildx inspect --bootstrap
    -n: add --no-cache to build
    -s: set hamclock size to one of the following: 800x480 1600x960 2400x1440 3200x1920
EOF
    exit 0
}

main() {
    RETVAL=0
    MULTI_PLATFORM=false
    NOCACHE=false

    if [[ "$@" =~ --help ]]; then
        usage
    fi

    while getopts ":hmns:" opt; do
        case $opt in
            h)
                usage
                ;;
            m)
                MULTI_PLATFORM=true
                ;;
            n)
                NOCACHE=true
                ;;
            s)
                HC_SIZE="$OPTARG"
                ;;
            \?) # Handle invalid options
                echo "Invalid option: -$OPTARG" >&2
                exit 1
                ;;
            :) # Handle options requiring an argument but none provided
                echo "Option -$OPTARG requires an argument." >&2
                exit 1
                ;;
        esac
    done
    shift $((OPTIND - 1))

    if [[ -n "$1" ]]; then
        echo
        echo "Invalid argument: $1"
        exit 1
    fi

    do_all
    build_done_message
}

do_all() {
    warn_image_tag
    warn_local_edits
    build_image
}

warn_image_tag() {
    if [ $TAG != latest ]; then
        if [ $MULTI_PLATFORM == true ]; then
            docker manifest inspect $IMAGE >/dev/null
            if [ $? -eq 0 ]; then
                echo
                echo "WARNING: the multiplatform docker image for '$IMAGE' already exists in Docker Hub. Please"
                echo "         remove it if you want to rebuild."
                exit 2
            fi
        elif docker image list --format '{{.Repository}}:{{.Tag}}' | grep -qs $IMAGE; then
            echo
            echo "WARNING: the docker image for '$IMAGE' already exists. Please remove it if you want to rebuild."
            exit 2
        fi
    fi
}

warn_local_edits() {
    # check if there are local edits in the filesystem. We probably don't want to push them
    git diff-index --quiet HEAD --
    LOCAL_EDITS=$?

    if [ $LOCAL_EDITS -ne 0 ]; then
        if [ $MULTI_PLATFORM == true ]; then
            echo
            echo "ERROR: There are local edits. stash or reset them before pushing"
            echo "       images to Docker Hub."
            exit 3
        else
            echo
            echo "WARNING: there are local edits. If you didn't intend that, stash"
            echo "         them and build again."
        fi
    fi
    return $LOCAL_EDITS
}

poke_version_cpp() {
    if [ "$1" == restore ]; then
        git restore ESPHamClock/version.cpp
    elif [ $ON_TAG == true ]; then
        # update version in HC source
        HC_TAG=${GIT_VERSION%.*}
        HC_TAG=${HC_TAG#v}
        HC_TAG=${HC_TAG#V}
        sed -i 's/\(hc_version = "\)[^"]\+\(".*\)/\1'$HC_TAG'\2/' ESPHamClock/version.cpp
    fi
}

build_image() {
    if [ $NOCACHE == true ]; then
        NOCACHE_ARG="--no-cache"
    fi
    if [ -n "$HC_SIZE" ]; then
        SET_HC_SIZE="--build-arg HC_SIZE=${HC_SIZE}"
    fi

    # Build the image
    echo
    echo "Building image for '$IMAGE_BASE:$TAG'"
    pushd "$HERE/.." >/dev/null

    poke_version_cpp

    if [ $MULTI_PLATFORM == true ]; then
        docker buildx build \
            $NOCACHE_ARG \
            --pull \
            --build-arg GIT_VERSION=${GIT_VERSION} \
            --build-arg HC_UID=$HC_UID \
            --build-arg HC_GID=$HC_GID \
            $SET_HC_SIZE \
            -t $IMAGE \
            -f docker/Dockerfile \
            --platform linux/amd64,linux/arm64 \
            --push \
            .
        RETVAL=$?
    else
        docker build \
            $NOCACHE_ARG \
            --pull \
            --build-arg GIT_VERSION=${GIT_VERSION} \
            --build-arg HC_UID=$HC_UID \
            --build-arg HC_GID=$HC_GID \
            $SET_HC_SIZE \
            -t $IMAGE \
            -f docker/Dockerfile \
            .
        RETVAL=$?
    fi

    poke_version_cpp restore

    popd >/dev/null
}

build_done_message() {
    if [ $RETVAL -eq 0 ]; then
        # basic info
        echo
        echo "Completed building '$IMAGE'."
    else
        echo "build failed with error: $RETVAL"
    fi
}

main "$@"
exit $RETVAL
