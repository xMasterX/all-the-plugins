#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/build/tools"
OUT_BIN="${OUT_DIR}/assetgen"

mkdir -p "${OUT_DIR}"

g++ \
  -std=c++17 \
  -O2 \
  -Wall \
  -Wextra \
  -pedantic \
  "${ROOT_DIR}/tools/assetgen/AssetGen.cpp" \
  "${ROOT_DIR}/tools/assetgen/lodepng.cpp" \
  -o "${OUT_BIN}"

echo "${OUT_BIN}"
