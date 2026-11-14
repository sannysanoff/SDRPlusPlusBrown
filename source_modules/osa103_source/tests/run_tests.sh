#!/bin/sh
# Builds and runs the OSA103 module's offline tests. No SDR++ build and no hardware
# are needed: the protocol layer is compiled standalone and the D2XX binding is driven
# against tests/stub_d2xx.c.
set -e
cd "$(dirname "$0")"
CXX=${CXX:-clang++}
CC=${CC:-clang}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

LIBEXT=dylib
case "$(uname -s)" in
    Linux*) LIBEXT=so ;;
esac

"$CXX" -std=c++17 -O1 -Wall -Wextra -I ../src \
    frame_test.cpp ../src/osa103_proto.cpp -o "$TMP/frame_test"
"$TMP/frame_test"

"$CC" -shared -o "$TMP/libftd2xx.$LIBEXT" stub_d2xx.c
"$CXX" -std=c++17 -O1 -Wall -Wextra -I ../src \
    d2xx_test.cpp ../src/osa103_proto.cpp ../src/ftdi_d2xx.cpp \
    -L"$TMP" -lftd2xx -Wl,-rpath,"$TMP" -o "$TMP/d2xx_test"
"$TMP/d2xx_test" "$TMP/libftd2xx.$LIBEXT"
