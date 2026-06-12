#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

prefix="${1:-${PREFIX:-$HOME/.local}}"
build_dir="${BUILD_DIR:-$repo_root/build/linux-release}"
build_type="${BUILD_TYPE:-Release}"

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -D "CMAKE_BUILD_TYPE=$build_type"
    -D "CMAKE_INSTALL_PREFIX=$prefix"
)

if command -v ninja >/dev/null 2>&1; then
    cmake_args=(-G Ninja "${cmake_args[@]}")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir"
cmake --install "$build_dir"

printf 'Installed binary: %s/bin/lumine2024_shell\n' "$prefix"

case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *)
        printf 'Tip: add %s/bin to PATH if it is not already visible in your shell.\n' "$prefix"
        ;;
esac
