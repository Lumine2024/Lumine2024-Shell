#!/usr/bin/env bash

set -euo pipefail

compiler="${1:-gcc}"

convert_to_invokable_cxx_compiler() {
    case "$1" in
        gcc|g++) echo "g++" ;;
        clang|clang++) echo "clang++" ;;
        vc|msvc|cl|vs) echo "cl" ;;
        *) echo "Unsupported compiler." >&2; exit 1 ;;
    esac
}

convert_to_invokable_c_compiler() {
    case "$1" in
        gcc|g++) echo "gcc" ;;
        clang|clang++) echo "clang" ;;
        vc|msvc|cl|vs) echo "cl" ;;
        *) echo "Unsupported compiler." >&2; exit 1 ;;
    esac
}

assert_file_content() {
    local expected_path="$1"
    local actual_path="$2"
    local label="$3"
    local expected_text
    local actual_text

    expected_text="$(tr -d '\r' < "$expected_path")"
    actual_text="$(tr -d '\r' < "$actual_path")"

    if [[ "$expected_text" != "$actual_text" ]]; then
        echo "Assertion failed for $label" >&2
        echo "Expected: $expected_path" >&2
        echo "Actual: $actual_path" >&2
        diff -u "$expected_path" "$actual_path" >&2 || true
        exit 1
    fi
}

assert_empty_file() {
    local path="$1"
    local label="$2"

    if [[ ! -f "$path" ]]; then
        echo "Assertion failed for $label" >&2
        echo "Missing file: $path" >&2
        exit 1
    fi

    if [[ -s "$path" ]]; then
        echo "Assertion failed for $label" >&2
        echo "Expected empty file: $path" >&2
        exit 1
    fi
}

resolve_python_command() {
    if command -v python > /dev/null 2>&1; then
        echo "python"
        return
    fi
    if command -v python3 > /dev/null 2>&1; then
        echo "python3"
        return
    fi
    echo "No usable Python interpreter found." >&2
    exit 1
}

render_case() {
    local template_path="$1"
    local output_path="$2"
    local python_command="$3"

    sed "s|@PYTHON@|$python_command|g" "$template_path" > "$output_path"
}

invoke_shell_case() {
    local binary_path="$1"
    local case_path="$2"
    local actual_stdout_path="$3"
    local expected_stdout_path="$4"
    local label="$5"

    "$binary_path" "$case_path" > "$actual_stdout_path"
    if [[ -n "$expected_stdout_path" ]]; then
        assert_file_content "$expected_stdout_path" "$actual_stdout_path" "$label"
    fi
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
invokable_c_compiler="$(convert_to_invokable_c_compiler "$compiler")"
invokable_cxx_compiler="$(convert_to_invokable_cxx_compiler "$compiler")"

cd "$repo_root"

echo "[lumine2024 shell] configuration..."
cmake -E rm -rf build
cmake -E make_directory build
cmake -S . -B build -G Ninja -D CMAKE_C_COMPILER="$invokable_c_compiler" -D CMAKE_CXX_COMPILER="$invokable_cxx_compiler"

echo "[lumine2024 shell] build..."
cmake --build build

echo "[lumine2024 shell] test..."
binary_path="build/src/lumine2024_shell"
artifact_root="build/test-artifacts"
case_artifact_dir="$artifact_root/cases"
redirect_artifact_dir="$artifact_root/redirect"
python_command="$(resolve_python_command)"

cmake -E rm -rf "$artifact_root"
cmake -E make_directory "$case_artifact_dir"
cmake -E make_directory "$redirect_artifact_dir"

render_case "tests/cases/basic.lsh.in" "$case_artifact_dir/basic.lsh" "$python_command"
render_case "tests/cases/pipeline.lsh.in" "$case_artifact_dir/pipeline.lsh" "$python_command"
render_case "tests/cases/redirect_io.lsh.in" "$case_artifact_dir/redirect_io.lsh" "$python_command"

invoke_shell_case "$binary_path" "$case_artifact_dir/basic.lsh" "$artifact_root/basic.stdout" "tests/expected/basic.stdout" "basic"
invoke_shell_case "$binary_path" "$case_artifact_dir/pipeline.lsh" "$artifact_root/pipeline.stdout" "tests/expected/pipeline.stdout" "pipeline"
invoke_shell_case "$binary_path" "$case_artifact_dir/redirect_io.lsh" "$artifact_root/redirect.stdout" "" "redirect"
assert_empty_file "$artifact_root/redirect.stdout" "redirect stdout stream"

assert_file_content "tests/expected/redirect/stdout.txt" "$redirect_artifact_dir/stdout.txt" "redirect stdout"
assert_file_content "tests/expected/redirect/stderr.txt" "$redirect_artifact_dir/stderr.txt" "redirect stderr"
assert_file_content "tests/expected/redirect/combined.txt" "$redirect_artifact_dir/combined.txt" "redirect combined"

echo "[lumine2024 shell] Success!"
