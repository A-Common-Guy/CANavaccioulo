#!/usr/bin/env bash
# Lint/format helper for stableCOPS.
#
#   tools/lint.sh format        # rewrite files in place (clang-format -i)
#   tools/lint.sh format-check  # fail if any file is not formatted
#   tools/lint.sh tidy          # run clang-tidy static analysis
#   tools/lint.sh               # format-check + tidy (what CI would do)
#
# Requires clangd/clang-tidy/clang-format on PATH and build/compile_commands.json
# (configure once with: cmake --preset default).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# clang here defaults to a gcc toolchain dir without libstdc++ headers; pin it to
# the gcc that actually ships the C++ headers. Override with GCC_INSTALL_DIR=...
GCC_INSTALL_DIR="${GCC_INSTALL_DIR:-/usr/lib/gcc/x86_64-linux-gnu/11}"
TIDY_ARGS=(-p build "--extra-arg=--gcc-install-dir=${GCC_INSTALL_DIR}")

# Project sources (skip third_party/ and build/).
mapfile -t SOURCES < <(find src examples include -type f \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | sort)

run_format() {
    clang-format -i "${SOURCES[@]}"
    echo "Formatted ${#SOURCES[@]} files."
}

run_format_check() {
    clang-format --dry-run --Werror "${SOURCES[@]}"
    echo "Format check passed (${#SOURCES[@]} files)."
}

run_tidy() {
    # Only .cpp appear in the compile database; headers are checked transitively.
    local cpps=()
    for f in "${SOURCES[@]}"; do [[ "$f" == *.cpp ]] && cpps+=("$f"); done
    clang-tidy "${TIDY_ARGS[@]}" "${cpps[@]}"
}

case "${1:-all}" in
    format)        run_format ;;
    format-check)  run_format_check ;;
    tidy)          run_tidy ;;
    all)           run_format_check; run_tidy ;;
    *) echo "usage: tools/lint.sh [format|format-check|tidy|all]" >&2; exit 2 ;;
esac
