#!/bin/bash -x
if test -e build; then
    echo 'build dir already exists; rm -rf build and re-run'
    exit 1
fi

if type ccache > /dev/null 2>&1 ; then
    echo "enabling ccache"
    ARGS="$ARGS -DWITH_CCACHE=ON"
fi

if hash ninja-build 2>/dev/null; then
    # Fedora uses this name
    NINJA=ninja-build
elif hash ninja 2>/dev/null; then
    NINJA=ninja
fi

MACHINE=`uname -i`
NINJA_ARGS=" "
if [ "x$MACHINE" == "xsw_64" ]; then
    NINJA_ARGS="-j14"
fi

ARGS="$ARGS -DCMAKE_BUILD_TYPE=Debug"

if ["${SPDK_ROOT}" == ""]; then
    echo -e "\033[31m Don't set SPDK_ROOT \033[0m"
    exit 0
fi  
currentDir=`pwd`
cd ${SPDK_ROOT}
./configure 
make 

cd ${currentDir}
mkdir build
cd build
#NPROC=${NPROC:-$(nproc)}
#cmake -DBOOST_J=$NPROC $ARGS "$@" ..

if [ "x$NINJA" == "x" ]; then
    cmake  $ARGS "$@" ..
    cmake --build . -- -j8
else
    cmake -GNinja $ARGS "$@" ..
    $NINJA $NINJA_ARGS
fi

echo done.

