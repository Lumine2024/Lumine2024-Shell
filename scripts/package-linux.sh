#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
repo_name="$(basename "$repo_root")"

default_label="$(git -C "$repo_root" rev-parse --short HEAD 2>/dev/null || echo snapshot)"
if ! git -C "$repo_root" diff --quiet --no-ext-diff || \
   ! git -C "$repo_root" diff --cached --quiet --no-ext-diff || \
   [[ -n "$(git -C "$repo_root" ls-files --others --exclude-standard)" ]]; then
    default_label="${default_label}-working-tree"
fi

archive_base="${1:-${repo_name}-${default_label}}"
archive_base="${archive_base%.tar}"
output_path="$repo_root/dist/${archive_base}.tar"
prefix_name="${archive_base}/"

mkdir -p "$repo_root/dist"

temp_index="$(mktemp)"
cleanup() {
    rm -f "$temp_index"
}
trap cleanup EXIT

export GIT_INDEX_FILE="$temp_index"
export GIT_AUTHOR_NAME="archive"
export GIT_AUTHOR_EMAIL="archive@example.invalid"
export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME"
export GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"

if git -C "$repo_root" rev-parse --verify HEAD >/dev/null 2>&1; then
    git -C "$repo_root" read-tree HEAD
    parent_args=(-p HEAD)
else
    parent_args=()
fi

git -C "$repo_root" add -A
tree_id="$(git -C "$repo_root" write-tree)"
commit_id="$(printf 'archive snapshot\n' | git -C "$repo_root" commit-tree "$tree_id" "${parent_args[@]}")"

git -C "$repo_root" archive \
    --format=tar \
    --prefix="$prefix_name" \
    --output="$output_path" \
    "$commit_id"

printf 'Created %s\n' "$output_path"
