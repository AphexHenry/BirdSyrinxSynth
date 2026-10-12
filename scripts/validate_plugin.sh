#!/usr/bin/env bash
# Validate Syrinx VST3 + AU with pluginval (strictness default 5) and auval. WP-C1.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/_common.sh
source "${ROOT}/scripts/_common.sh"

# usage: scripts/validate_plugin.sh [strictness] [--gui]
#   --gui also runs pluginval's editor tests (the plugin window opens and closes several times). WP-R4.
strictness="${1:-5}"
gui_flag="--skip-gui-tests"
if [[ "${2:-}" == "--gui" ]]; then
    gui_flag=""
fi
artefacts="${ROOT}/build/release/plugin/Syrinx_artefacts"
# JUCE 8 may put configs under Release/ or directly under artefacts.
vst3="$(find "${artefacts}" -type d -name 'Syrinx.vst3' | head -n 1 || true)"
au="$(find "${artefacts}" -type d -name 'Syrinx.component' | head -n 1 || true)"
if [[ -z "${vst3}" || -z "${au}" ]]; then
    err "plugin artefacts not found under ${artefacts} (run scripts/build.sh release)"
    find "${ROOT}/build/release" -name '*.vst3' -o -name '*.component' || true
    exit 1
fi

"${ROOT}/scripts/get_pluginval.sh"
pval="${ROOT}/tools/pluginval/pluginval.app/Contents/MacOS/pluginval"
if [[ ! -x "${pval}" ]]; then
    err "pluginval missing"
    exit 1
fi

log "pluginval VST3 strictness=${strictness}: ${vst3}"
"${pval}" --strictness-level "${strictness}" --validate-in-process ${gui_flag} --timeout-ms 300000 --validate "${vst3}"

user_vst="${HOME}/Library/Audio/Plug-Ins/VST3/Syrinx.vst3"
user_au="${HOME}/Library/Audio/Plug-Ins/Components/Syrinx.component"
mkdir -p "$(dirname "${user_vst}")" "$(dirname "${user_au}")"
rm -rf "${user_vst}" "${user_au}"
cp -R "${vst3}" "${user_vst}"
cp -R "${au}" "${user_au}"
log "copied VST3 -> ${user_vst}"
log "copied AU   -> ${user_au}"

# macOS will not see a newly copied AU until the registrar is restarted.
killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2

log "pluginval AU strictness=${strictness}: ${user_au}"
"${pval}" --strictness-level "${strictness}" --validate-in-process ${gui_flag} --timeout-ms 300000 --validate "${user_au}"

if command -v auval >/dev/null 2>&1; then
    log "auval -v aumu Syrx Cuic"
    auval -v aumu Syrx Cuic
else
    err "auval not found (needs full Xcode)"
    exit 1
fi

if command -v clap-info >/dev/null 2>&1; then
    clap="$(find "${artefacts}" -type d -name 'Syrinx.clap' | head -n 1 || true)"
    if [[ -n "${clap}" ]]; then
        log "clap-info ${clap}"
        clap-info "${clap}" || true
    fi
else
    log "note: no clap validator (clap-info) on PATH; CLAP built but not validated"
fi

gui_note="with GUI tests"
if [[ -n "${gui_flag}" ]]; then
    gui_note="no GUI tests"
fi
log "VALIDATE_PLUGIN OK (strictness ${strictness}, ${gui_note})"
