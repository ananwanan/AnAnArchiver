#!/usr/bin/env bash
set -euo pipefail
if [[ $# -gt 0 ]]; then printf '%s\n' 'Usage: uninstall-desktop.sh' >&2; exit 2; fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
[[ "$data_home" == /* ]] || { printf '%s\n' 'XDG_DATA_HOME must be absolute.' >&2; exit 2; }
id=com.ananwanan.AnAnArchiver
desktop="$data_home/applications/$id.desktop"
owner="X-AnAnArchiver-Package=${root//\\/\\\\}"
# An older portable directory must not unregister a newer installation.
if [[ -f "$desktop" ]] && grep -Fxq -- "$owner" "$desktop"; then
    rm -f -- "$desktop" "$data_home/icons/$id.png"
    if command -v update-desktop-database >/dev/null; then update-desktop-database "$data_home/applications"; fi
    printf '%s\n' 'Removed LiteArc desktop integration. The package and user settings are preserved.'
else
    printf '%s\n' 'No desktop integration owned by this package was found.'
fi
