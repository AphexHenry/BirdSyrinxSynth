#!/usr/bin/env bash
# Download pluginval into tools/pluginval/ (gitignored). WP-C1.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

DEST="${ROOT}/tools/pluginval"
APP="${DEST}/pluginval.app"
BIN="${APP}/Contents/MacOS/pluginval"
if [[ -x "${BIN}" ]]; then
    log "pluginval already at ${BIN}"
    "${BIN}" --version || true
    exit 0
fi

need_cmd curl
need_cmd unzip
ver="v1.0.4"
url="https://github.com/Tracktion/pluginval/releases/download/${ver}/pluginval_macOS.zip"
tmp="$(mktemp -d)"
log "downloading ${url}"
curl -L --fail --retry 3 -o "${tmp}/pluginval.zip" "${url}"
mkdir -p "${DEST}"
unzip -qo "${tmp}/pluginval.zip" -d "${DEST}"
rm -rf "${tmp}"

if [[ ! -x "${BIN}" ]]; then
    # zip layout sometimes has pluginval.app at the top of DEST already
    if [[ -x "${ROOT}/tools/pluginval/pluginval.app/Contents/MacOS/pluginval" ]]; then
        BIN="${ROOT}/tools/pluginval/pluginval.app/Contents/MacOS/pluginval"
    else
        err "pluginval binary not found after unzip; contents of ${DEST}:"
        ls -la "${DEST}" || true
        exit 1
    fi
fi
log "GET_PLUGINVAL OK: ${BIN}"
"${BIN}" --version || true
