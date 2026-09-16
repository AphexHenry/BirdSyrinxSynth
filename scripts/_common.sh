#!/usr/bin/env bash
# Shared helpers for scripts/ — sourced, not executed.
# Usage: source "$(dirname "${BASH_SOURCE[0]}")/_common.sh"

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Apple Silicon Homebrew, Intel Homebrew, and a workspace-local uv (tools/uv).
export PATH="/opt/homebrew/bin:/usr/local/bin:${ROOT}/tools/uv:${PATH}"
export UV_CACHE_DIR="${ROOT}/.cache/uv"
export UV_PYTHON_INSTALL_DIR="${ROOT}/.cache/uv-python"
export MPLCONFIGDIR="${ROOT}/.cache/matplotlib"
mkdir -p "${UV_CACHE_DIR}" "${MPLCONFIGDIR}"

log() { printf '%s\n' "$*"; }
err() { printf 'error: %s\n' "$*" >&2; }

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        err "required command not found: $1"
        return 1
    fi
}
