#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
  echo "usage: ./run.sh /absolute/path/to/hermes [module-count] [functions-per-module]"
  exit 2
fi

repro_dir="$(cd "$(dirname "$0")" && pwd)"
hermes_source="$1"
module_count="${2:-24000}"
functions_per_module="${3:-8}"
bundle_path="$repro_dir/bundle-${module_count}x${functions_per_module}.js"
build_dir="$repro_dir/.build"
export CCACHE_DIR="$build_dir/.ccache"

node "$repro_dir/generate-bundle.mjs" \
  "$module_count" \
  "$functions_per_module" \
  "$bundle_path"

cmake_args=(
  -S "$repro_dir"
  -B "$build_dir"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DHERMES_SOURCE_DIR="$hermes_source"
)

if [[ "$(uname -s)" == "Darwin" ]]; then
  cmake_args+=(
    "-DCMAKE_C_ARCHIVE_CREATE=/usr/bin/libtool -static -o <TARGET> <OBJECTS>"
    "-DCMAKE_C_ARCHIVE_FINISH="
    "-DCMAKE_CXX_ARCHIVE_CREATE=/usr/bin/libtool -static -o <TARGET> <OBJECTS>"
    "-DCMAKE_CXX_ARCHIVE_FINISH="
  )
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --target hermes-lazy-bench -j 8
"$build_dir/bin/hermes-lazy-bench" smart "$bundle_path"
