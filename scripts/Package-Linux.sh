#!/usr/bin/env bash
set -euo pipefail

usage() { printf '%s\n' 'Usage: Package-Linux.sh BUILD_DIRECTORY [OUTPUT_DIRECTORY]'; }
if [[ ${1:-} == --help ]]; then usage; exit 0; fi
if [[ $# -lt 1 || $# -gt 2 ]]; then usage >&2; exit 2; fi
if [[ $(uname -s) != Linux ]]; then printf '%s\n' 'Linux packaging requires a native Linux host.' >&2; exit 2; fi

build=$(cd "$1" && pwd -P)
if [[ -f "$build/application/CMakeCache.txt" ]]; then build="$build/application"; fi
cache="$build/CMakeCache.txt"
[[ -f "$cache" ]] || { printf '%s\n' 'Configure and build the desktop application first.' >&2; exit 2; }
configuration=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache")
[[ -z "$configuration" || "$configuration" == Release ]] || {
    printf '%s\n' 'Select a Release build directory for packaging.' >&2; exit 2;
}
source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")
[[ -f "$source/packaging/linux/com.ananwanan.AnAnArchiver.desktop" ]] || exit 2
output=${2:-"$source/out/artifacts/linux"}
mkdir -p -- "$output"
output=$(cd "$output" && pwd -P)
stage=$(mktemp -d "$output/.linux-stage.XXXXXXXX")
cleanup() { [[ "$stage" == "$output"/.linux-stage.* ]] && rm -rf -- "$stage"; }
trap cleanup EXIT

# Reuse the exact compiler, codecs and Qt configuration recorded in this cache.
cmake -S "$source" -B "$build" -DANANARCHIVER_DEPLOY_RUNTIME=ON -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$build" --config Release --target AnAnArchiver --parallel "${ANANARCHIVER_JOBS:-4}"
metadata="$build/frontend/qt/packaging-metadata.txt"
version=$(sed -n 's/^version=//p' "$metadata")
qt_version=$(sed -n 's/^qt_version=//p' "$metadata")
qt_prefix=$(sed -n 's/^qt_prefix=//p' "$metadata")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf '%s\n' 'Invalid package version.' >&2; exit 2; }
name="AnAnArchiver-$version-linux-$(uname -m)"
package="$stage/$name"
cmake --install "$build" --config Release --prefix "$package"
install -m 755 "$source/packaging/linux/AnAnArchiver.sh" "$package/AnAnArchiver.sh"
install -m 755 "$source/packaging/linux/install-desktop.sh" "$package/install-desktop.sh"
install -m 755 "$source/packaging/linux/uninstall-desktop.sh" "$package/uninstall-desktop.sh"
install -m 644 "$source/packaging/linux/README.md" "$package/README.md"
install -m 644 "$metadata" "$package/share/packaging-metadata.txt"
for provenance in dependency-provenance.json resolved-dependencies.json; do
    if [[ -f "$build/$provenance" ]]; then install -m 644 "$build/$provenance" "$package/share/$provenance"; fi
done
[[ -d "$qt_prefix" ]] || { printf '%s\n' 'Missing recorded Qt kit.' >&2; exit 2; }
license_directory=${ANANARCHIVER_QT_LICENSE_DIRECTORY:-"$source/third_party/qt/LICENSES"}
python3 "$source/packaging/linux/collect_licenses.py" \
    --project-root "$source" --source-root "${ANANARCHIVER_DEPENDENCY_SOURCE_DIRECTORY:-$source}" \
    --manifest "$build/resolved-dependencies.json" --output "$package/share/licenses" \
    --qt-license-dir "$license_directory" --qt-prefix "$qt_prefix" --qt-version "$qt_version"

# Refuse a package with unresolved ELF dependencies; system graphics/glibc stay
# external by design. Qt's Linux deploy API copies project and Qt libraries.
while IFS= read -r -d '' binary; do
    if file "$binary" | grep -q ELF; then
        dependencies=$(LD_LIBRARY_PATH="$package/lib" ldd "$binary" 2>&1) || {
            printf '%s\n' "Dependency inspection failed: $binary" "$dependencies" >&2; exit 1;
        }
        if grep -q 'not found' <<< "$dependencies"; then
            printf '%s\n' "Unresolved dependency: $binary" "$dependencies" >&2; exit 1;
        fi
    fi
done < <(find "$package/bin" "$package/lib" "$package/plugins" "$package/qml" -type f -print0)
[[ -f "$package/bin/qt.conf" && -f "$package/qml/QtQuick/qmldir" ]] || {
    printf '%s\n' 'Qt runtime deployment is incomplete.' >&2; exit 1;
}
env -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH -u LD_LIBRARY_PATH \
    ANANARCHIVER_SETTINGS_FILE="$stage/smoke-settings/settings.ini" \
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$package/AnAnArchiver.sh" --smoke-test 500
if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$package/share/applications/com.ananwanan.AnAnArchiver.desktop"
fi
printf 'version=%s\nqt_version=%s\narchitecture=%s\nhome_smoke=passed\nelf_dependencies=resolved\n' \
    "$version" "$qt_version" "$(uname -m)" > "$package/share/package-verification.txt"
tar -C "$stage" -czf "$stage/$name.tar.gz" "$name"
mv -f -- "$stage/$name.tar.gz" "$output/$name.tar.gz"
(cd "$output" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
printf '%s\n' "$output/$name.tar.gz"
