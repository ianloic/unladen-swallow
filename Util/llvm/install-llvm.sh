#!/bin/bash
#
# Use this script to install LLVM to a temporary directory. This is useful for
# making Unladen Swallow build faster. cd Util/llvm and run one of these:
#
# ./install-llvm.sh debug --prefix=/tmp/llvm-dbg --other-configure-arg
# ./install-llvm.sh release --prefix=/tmp/llvm-rel --some-other-option=foo
#
# debug == passing --with-pydebug to Python's ./configure
# release == omitting --with-pydebug to Python's ./configure
#
# You can then use Unladen Swallow's --with-llvm=/tmp/llvm-dbg option.
# The {debug,release} option you pass to this script should match up with
# whether you use --with-pydebug with ./configure.

# Keep these arguments in sync with the top-level configure.in.
LLVM_ARGS="--enable-jit --enable-targets=x86,cpp --enable-bindings=none"
DEBUG_ARGS="--disable-optimized --enable-debug-runtime --enable-assertions"
RELEASE_ARGS="--enable-optimized --disable-assertions"

case "$1" in
    "debug") LLVM_ARGS="$LLVM_ARGS $DEBUG_ARGS";;
    "release") LLVM_ARGS="$LLVM_ARGS $RELEASE_ARGS";;
    *) echo "First arg needs to be either 'debug' or 'release', not '$1'"; \
       exit 1;;
esac

shift  # Take the first argument off the front of $@.
./configure $LLVM_ARGS $@ && make && make install