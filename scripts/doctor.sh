#!/usr/bin/env bash
# Check that the developer tools needed to build Syrinx are present.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

ok=0
missing=0
warns=0

green() { printf '\033[32m%s\033[0m' "$1"; }
red() { printf '\033[31m%s\033[0m' "$1"; }
yellow() { printf '\033[33m%s\033[0m' "$1"; }

check_cmd() {
    local name="$1"
    local hint="$2"
    local extra="${3:-}"
    if command -v "$name" >/dev/null 2>&1; then
        local ver
        ver="$($name --version 2>/dev/null | head -n 1 | tr '\n' ' ')"
        printf '%-16s %s  %s\n' "$name" "$(green OK)" "$ver"
        ok=$((ok + 1))
        if [[ -n "$extra" ]]; then
            eval "$extra"
        fi
        return 0
    fi
    printf '%-16s %s  install: %s\n' "$name" "$(red MISSING)" "$hint"
    missing=$((missing + 1))
    return 1
}

cmake_version_ok() {
    local raw
    raw="$(cmake --version | head -n 1 | awk '{print $3}')"
    local major minor
    major="${raw%%.*}"
    minor="${raw#*.}"
    minor="${minor%%.*}"
    if (( major > 3 || (major == 3 && minor >= 25) )); then
        return 0
    fi
    printf '%-16s %s  found %s, need ≥ 3.25  (brew install cmake)\n' "cmake" "$(red MISSING)" "$raw"
    missing=$((missing + 1))
    ok=$((ok - 1))
    return 1
}

echo "Syrinx doctor — $(uname -s) $(uname -m)"
echo "ROOT=$ROOT"
echo

printf '%-16s %s  %s\n' "tool" "status" "notes"
printf '%-16s %s  %s\n' "----" "------" "-----"

check_cmd cmake "brew install cmake" cmake_version_ok || true
check_cmd ninja "brew install ninja" || true
check_cmd git "brew install git" || true

if command -v c++ >/dev/null 2>&1; then
    ver="$(c++ --version | head -n 1)"
    printf '%-16s %s  %s\n' "c++ (C++20)" "$(green OK)" "$ver"
    ok=$((ok + 1))
else
    printf '%-16s %s  install: xcode-select --install  (or brew install llvm)\n' "c++ (C++20)" "$(red MISSING)"
    missing=$((missing + 1))
fi

check_cmd uv "brew install uv   (or: curl -LsSf https://astral.sh/uv/install.sh | sh)" || true

if command -v clang-format >/dev/null 2>&1; then
    ver="$(clang-format --version | head -n 1)"
    printf '%-16s %s  %s\n' "clang-format" "$(green OK)" "$ver"
    ok=$((ok + 1))
else
    printf '%-16s %s  install: brew install clang-format  (optional; check.sh will warn)\n' "clang-format" "$(yellow WARN)"
    warns=$((warns + 1))
fi

if xcode-select -p >/dev/null 2>&1; then
    printf '%-16s %s  %s\n' "Xcode CLT" "$(green OK)" "$(xcode-select -p)"
    ok=$((ok + 1))
else
    printf '%-16s %s  run: xcode-select --install  (needed later for AU validation)\n' "Xcode CLT" "$(yellow WARN)"
    warns=$((warns + 1))
fi

echo
echo "summary: ${ok} OK, ${missing} missing, ${warns} warnings"
if (( missing > 0 )); then
    echo "DOCTOR FAILED: install the MISSING tools and re-run ./scripts/doctor.sh"
    exit 1
fi
echo "DOCTOR OK"
exit 0
