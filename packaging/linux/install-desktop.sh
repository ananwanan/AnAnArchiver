#!/usr/bin/env bash
set -euo pipefail
if [[ $# -gt 0 ]]; then printf '%s\n' 'Usage: install-desktop.sh' >&2; exit 2; fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
[[ "$data_home" == /* ]] || { printf '%s\n' 'XDG_DATA_HOME must be absolute.' >&2; exit 2; }
id=com.ananwanan.AnAnArchiver
desktop="$data_home/applications/$id.desktop"
template="$root/share/applications/$id.desktop"
[[ -x "$root/AnAnArchiver.sh" && -f "$template" && -f "$root/share/pixmaps/$id.png" ]] || { printf '%s\n' 'Extract the complete Linux package first.' >&2; exit 2; }
[[ "$root" != *$'\n'* && "$root" != *$'\r'* ]] || { printf '%s\n' 'Installation path contains a line break.' >&2; exit 2; }
# Desktop Exec parsing has two escaping layers: the key value, then quoted
# arguments. Field codes are escaped independently. Never evaluate a shell.
escaped=${root//\\/\\\\\\\\}
escaped=${escaped//\"/\\\\\"}
escaped=${escaped//\$/\\\\\$}
escaped=${escaped//\`/\\\\\`}
escaped=${escaped//%/%%}
mkdir -p -- "$data_home/applications" "$data_home/icons"
temporary_directory=$(mktemp -d "$data_home/applications/ananarchiver-check-XXXXXXXX")
temporary="$temporary_directory/$id.desktop"
icon_temporary=''
cleanup() {
    rm -f -- "$temporary"
    if [[ -n "$icon_temporary" ]]; then rm -f -- "$icon_temporary"; fi
    rmdir -- "$temporary_directory"
}
trap cleanup EXIT
# Register cleanup before the second allocation, so an icon mktemp/copy failure
# also removes the already allocated desktop staging directory.
icon_temporary=$(mktemp "$data_home/icons/.$id.XXXXXXXX")
while IFS= read -r line || [[ -n "$line" ]]; do
    case "$line" in
        '[Desktop Entry]') printf '%s\nX-AnAnArchiver-Package=%s\n' "$line" "${root//\\/\\\\}" ;;
        'Exec=AnAnArchiver '*) printf 'Exec="%s/AnAnArchiver.sh" %s\n' "$escaped" "${line#Exec=AnAnArchiver }" ;;
        TryExec=*) ;; # Absolute Exec remains valid even when the package is not on PATH.
        *) printf '%s\n' "$line" ;;
    esac
done < "$template" > "$temporary"
if command -v desktop-file-validate >/dev/null; then desktop-file-validate "$temporary"; fi
chmod 644 "$temporary"
cp -- "$root/share/pixmaps/$id.png" "$icon_temporary"
chmod 644 "$icon_temporary"
mv -f -- "$icon_temporary" "$data_home/icons/$id.png"
mv -f -- "$temporary" "$desktop"
if command -v update-desktop-database >/dev/null; then update-desktop-database "$data_home/applications"; fi
printf '%s\n' "Installed $desktop" 'Choose AnAnArchiver using your file manager’s Open With menu to change a default application.'
