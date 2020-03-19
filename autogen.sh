#!/bin/sh

set -e # exit on errors

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

(
    cd "$srcdir"
    git submodule update --init --recursive
    autoreconf -v --force --install
)

CONFIGURE_ARGS=""

if [ -z "$NOCONFIGURE" ]; then
    echo "Running configure with $CONFIGURE_ARGS $@"
    "$srcdir/configure" $CONFIGURE_ARGS "$@"
fi
