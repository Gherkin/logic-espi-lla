#!/usr/bin/env bash
# Configure, build and test. Run inside the container:
#
#   docker run --rm -v "$PWD":/work espi-build ./scripts/build.sh
#
# Pass extra CMake args through, e.g. -DESPI_BUILD_PLUGIN=ON

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"

cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "$BUILD_DIR"

cd "$BUILD_DIR"
ctest --output-on-failure
