#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_dir="${BUILD_DIR:-$repo_root/build/linux-release}"
binary_path="$build_dir/src/lumine2024_shell"

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -D "CMAKE_BUILD_TYPE=Release"
)

if command -v ninja >/dev/null 2>&1; then
    cmake_args=(-G Ninja "${cmake_args[@]}")
fi

if [[ ! -x "$binary_path" ]]; then
    cmake "${cmake_args[@]}"
fi

cmake --build "$build_dir"
exec "$binary_path" "$@"
