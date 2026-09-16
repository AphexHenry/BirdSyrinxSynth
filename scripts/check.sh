#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

fail() {
    echo "CHECK FAILED: $1"
    exit 1
}

"${ROOT}/scripts/build.sh" release || fail "build"
"${ROOT}/scripts/test.sh" || fail "tests"

(
    cd "${ROOT}/python"
    uv run ruff check .
) || fail "ruff"

if command -v clang-format >/dev/null 2>&1; then
    files=()
    while IFS= read -r f; do
        files+=("$f")
    done < <(find "${ROOT}/core" "${ROOT}/apps" "${ROOT}/python/bindings" \
        \( -name '*.hpp' -o -name '*.hpp.in' -o -name '*.cpp' -o -name '*.h' \) \
        -print | sort)
    if ((${#files[@]} > 0)); then
        clang-format --dry-run --Werror "${files[@]}" || fail "clang-format"
    fi
else
    log "warning: clang-format not installed; skipping format check (brew install clang-format)"
fi

echo "CHECK PASSED"
