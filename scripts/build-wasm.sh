#!/usr/bin/env bash
# Build the WebAssembly module into web/wasm/ (poker.js + poker.wasm).
#
# Requires Emscripten on PATH (emcc / emcmake):
#   macOS:  brew install emscripten
#   any OS: git clone https://github.com/emscripten-core/emsdk && cd emsdk \
#           && ./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v emcmake >/dev/null 2>&1; then
  echo "error: emcmake not found - install Emscripten first (see comments in $0)" >&2
  exit 1
fi

emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j
ls -lh web/wasm/
echo "Built web/wasm/poker.js + poker.wasm. Serve the site with: python3 -m http.server -d web 8000"
