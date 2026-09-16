#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

wp="${1:-}"
if [[ -z "$wp" ]]; then
    err "usage: scripts/listen.sh <WP-ID>"
    exit 2
fi

manifest="${ROOT}/listening/${wp}/manifest.json"
if [[ ! -f "$manifest" ]]; then
    err "missing ${manifest}"
    exit 1
fi

need_cmd uv
(
    cd "${ROOT}/python"
    uv run python -m cuicui.listen page "$manifest"
)

html="${ROOT}/listening/${wp}/index.html"
log "LISTEN OK: ${html}"
