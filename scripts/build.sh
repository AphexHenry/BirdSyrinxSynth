#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

preset="${1:-release}"
case "$preset" in
    debug|release) ;;
    *)
        err "usage: scripts/build.sh [debug|release]"
        exit 2
        ;;
esac

need_cmd cmake
need_cmd ninja
need_cmd uv

log "python: uv sync"
(
    cd "${ROOT}/python"
    uv sync --python 3.12
)

py="${ROOT}/python/.venv/bin/python"
cmake_extra=()
if [[ -x "${py}" ]]; then
    cmake_extra+=(-DPython_EXECUTABLE="${py}")
    nb_dir="$("${py}" -c "import nanobind; print(nanobind.cmake_dir())" 2>/dev/null || true)"
    if [[ -n "${nb_dir}" ]]; then
        cmake_extra+=(-Dnanobind_DIR="${nb_dir}")
    fi
fi

log "configure preset=${preset}"
cmake --preset "${preset}" "${cmake_extra[@]}"
log "build preset=${preset}"
cmake --build --preset "${preset}"

log "BUILD OK (${preset})"
