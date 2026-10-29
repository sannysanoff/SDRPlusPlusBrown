#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
resources=${1:-"$repo/root_dev/inst/share/sdrpp_brown"}
build=$(mktemp -d "${TMPDIR:-/tmp}/omlsa-regression.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
src="$repo/misc_modules/noise_reduction_logmmse/src/omlsa_mcra"
"${CXX:-clang++}" -std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$repo/core/src" -I"$src" "$repo/tests/omlsa_mcra/regression.cpp" \
    "$src/G_calculate.cpp" "$src/Datablock_Read.cpp" "$src/LSA_denoise.cpp" \
    "$src/base4_fft.cpp" "$src/Cordic.cpp" -o "$build/regression"
"$build/regression" "$resources"
