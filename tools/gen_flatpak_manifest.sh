#!/bin/bash

set -e

WIVRN_SRC_TYPE=gitlocal

while [ ! -z "$1" ]
do
    case "$1" in
        "--git")
        WIVRN_SRC_TYPE=git
        shift
        WIVRN_SRC2="url: $1"
        ;;
        "--gitlocal")
        WIVRN_SRC_TYPE=gitlocal
        ;;
        "--dir")
        WIVRN_SRC_TYPE=dir
        ;;
    esac

    shift
done

cd $(dirname $0)

GIT_TAG=$(git describe --exact-match --tags 2>/dev/null || true)
GIT_SHA=$(git rev-parse HEAD)
GIT_COMMIT=$(git describe --exact-match --tags 2>/dev/null || git rev-parse HEAD)
GIT_DESC=$(git describe --tags --always)

MONADO_COMMIT=$(grep -zo "FetchContent_Declare(monado[^)]*)" ../CMakeLists.txt | sed -ze "s/^.*GIT_TAG *\([^ )]*\).*$/\1/" | tr -d '\0')
BOOSTPFR_URL=$(grep -zo "FetchContent_Declare(boostpfr[^)]*)" ../CMakeLists.txt | sed -ze "s/^.*URL *\([^ )]*\).*$/\1/" | tr -d '\0')
BOOSTPFR_SHA256=$(curl --silent --location $BOOSTPFR_URL | sha256sum | cut -f1 -d' ')

if [ $WIVRN_SRC_TYPE = git ]
then
    WIVRN_SRC1="type: git"
    WIVRN_SRC3="tag: ${GIT_COMMIT}"
elif [ $WIVRN_SRC_TYPE = gitlocal ]
then
    WIVRN_SRC1="type: git"
    WIVRN_SRC2="url: "$(realpath $(pwd)/..)
    WIVRN_SRC3="tag: ${GIT_COMMIT}"
else
    WIVRN_SRC1="type: dir"
    WIVRN_SRC2="path: "$(realpath $(pwd)/..)
    WIVRN_SRC3=""
fi

echo "Git commit:       $GIT_COMMIT"        >&2
echo "Git tag:          $GIT_DESC"          >&2
echo "Monado commit:    $MONADO_COMMIT"     >&2
echo "Boost.PFR URL:    $BOOSTPFR_URL"      >&2
echo "Boost.PFR SHA256: $BOOSTPFR_SHA256"   >&2

cat ../flatpak/io.github.wivrn.wivrn.yml.in | sed \
    -e s,WIVRN_SRC1,"$WIVRN_SRC1",                                    \
    -e s,WIVRN_SRC2,"$WIVRN_SRC2",                                    \
    -e s,WIVRN_SRC3,"$WIVRN_SRC3",                                    \
    -e s,WIVRN_GIT_DESC,$GIT_DESC,                                    \
    -e s,WIVRN_GIT_COMMIT,$GIT_COMMIT,                                \
    -e s,BOOSTPFR_URL,$BOOSTPFR_URL,                                  \
    -e s,BOOSTPFR_SHA256,$BOOSTPFR_SHA256,                            \
    -e s,MONADO_COMMIT,$MONADO_COMMIT,
