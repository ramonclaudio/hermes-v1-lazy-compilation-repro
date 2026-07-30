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

if [[ ! "$module_count" =~ ^[1-9][0-9]*$ ]]; then
  echo "module-count must be a positive integer" >&2
  exit 2
fi
if [[ ! "$functions_per_module" =~ ^(0|[1-9][0-9]*)$ ]]; then
  echo "functions-per-module must be a non-negative integer" >&2
  exit 2
fi

bundle_path="$repro_dir/bundle-${module_count}x${functions_per_module}.js"
build_dir="${HERMES_BUILD_DIR:-$repro_dir/.build}"
build_type="${HERMES_BUILD_TYPE:-Release}"
export CCACHE_DIR="$build_dir/.ccache"

if [[ "$hermes_source" != /* ]]; then
  echo "Hermes source directory must be an absolute path" >&2
  exit 2
fi
if [[ ! -f "$hermes_source/CMakeLists.txt" ]]; then
  echo "Hermes source directory is invalid: $hermes_source" >&2
  exit 2
fi

for required_command in node cmake ninja; do
  if ! command -v "$required_command" >/dev/null 2>&1; then
    echo "missing required command: $required_command" >&2
    exit 2
  fi
done

detected_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)"
build_jobs="${HERMES_BUILD_JOBS:-$detected_jobs}"
if [[ ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then
  echo "HERMES_BUILD_JOBS must be a positive integer" >&2
  exit 2
fi

node "$repro_dir/generate-bundle.mjs" \
  "$module_count" \
  "$functions_per_module" \
  "$bundle_path"

cmake_args=(
  -S "$repro_dir"
  -B "$build_dir"
  -G Ninja
  "-DCMAKE_BUILD_TYPE=$build_type"
  -DHERMES_SOURCE_DIR="$hermes_source"
)

if command -v ccache >/dev/null 2>&1; then
  cmake_args+=(
    -DCMAKE_C_COMPILER_LAUNCHER=ccache
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  )
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  cmake_args+=(
    "-DCMAKE_C_ARCHIVE_CREATE=/usr/bin/libtool -static -o <TARGET> <OBJECTS>"
    "-DCMAKE_C_ARCHIVE_FINISH="
    "-DCMAKE_CXX_ARCHIVE_CREATE=/usr/bin/libtool -static -o <TARGET> <OBJECTS>"
    "-DCMAKE_CXX_ARCHIVE_FINISH="
  )
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --target hermes-lazy-bench -j "$build_jobs"

expected_checksum="$(
  node -p \
    'const n = Number(process.argv[1]); n * (n - 1) / 2' \
    "$module_count"
)"
"$build_dir/bin/hermes-lazy-bench" \
  smart \
  "$bundle_path" \
  "$expected_checksum"
