#!/usr/bin/env bash
# Update one upstream dependency vendored under deps/ to a new ref.
#
#   scripts/update-dep.sh <name> <ref>
#
# <ref> is a tag, branch or commit of the upstream repository. This is a thin
# wrapper around:
#   git subtree pull --prefix deps/<name> <url> <ref> --squash
#
# Update deps/README.md (ref and date columns) in the same change. Dependencies
# track upstream master: scripts/update-dep.sh <name> master
set -euo pipefail

# name -> upstream repository URL (keep in sync with deps/README.md)
declare -A DEP_URLS=(
    [cmake_common]="https://github.com/ModOrganizer2/cmake_common"
    [uibase]="https://github.com/ModOrganizer2/modorganizer-uibase"
    [archive]="https://github.com/ModOrganizer2/modorganizer-archive"
    [bsatk]="https://github.com/ModOrganizer2/modorganizer-bsatk"
    [esptk]="https://github.com/ModOrganizer2/modorganizer-esptk"
    [lootcli]="https://github.com/ModOrganizer2/modorganizer-lootcli"
    [dds-header]="https://github.com/microsoft/DirectXTex"
    [game_bethesda]="https://github.com/ModOrganizer2/modorganizer-game_bethesda"
    [installer_quick]="https://github.com/ModOrganizer2/modorganizer-installer_quick"
    [installer_manual]="https://github.com/ModOrganizer2/modorganizer-installer_manual"
    [installer_fomod]="https://github.com/ModOrganizer2/modorganizer-installer_fomod"
)

usage() {
    local names
    names=$(printf '%s\n' "${!DEP_URLS[@]}" | sort | paste -sd' ')
    echo "usage: $0 <name> <ref>" >&2
    echo "  names: ${names}" >&2
}

if [[ $# -ne 2 || -z "${1}" || -z "${2}" ]]; then
    echo "error: expected exactly two arguments: <name> <ref>" >&2
    usage
    exit 2
fi

name=$1
ref=$2

if [[ -z "${DEP_URLS[$name]+set}" ]]; then
    echo "error: unknown dependency '${name}'" >&2
    usage
    exit 2
fi

root=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
cd "${root}"

if [[ ! -d "deps/${name}" ]]; then
    echo "error: deps/${name} does not exist; add it with 'git subtree add' first" >&2
    exit 1
fi

exec git subtree pull --prefix "deps/${name}" "${DEP_URLS[$name]}" "${ref}" --squash
