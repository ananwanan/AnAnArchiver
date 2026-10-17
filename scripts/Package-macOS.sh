#!/usr/bin/env bash
set -euo pipefail

usage() { printf '%s\n' 'Usage: Package-macOS.sh BUILD_DIRECTORY [OUTPUT_DIRECTORY] [--dmg] [--sign IDENTITY]'; }
if [[ ${1:-} == --help ]]; then usage; exit 0; fi
if [[ $# -lt 1 ]]; then usage >&2; exit 2; fi
if [[ $(uname -s) != Darwin ]]; then printf '%s\n' 'macOS packaging requires a native macOS host.' >&2; exit 2; fi
build=$(cd "$1" && pwd -P)
shift
output=''
make_dmg=false
identity=''
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dmg) make_dmg=true; shift ;;
        --sign) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; identity=$2; shift 2 ;;
        --*) usage >&2; exit 2 ;;
        *) [[ -z "$output" ]] || { usage >&2; exit 2; }; output=$1; shift ;;
    esac
done
if [[ -f "$build/application/CMakeCache.txt" ]]; then build="$build/application"; fi
cache="$build/CMakeCache.txt"
[[ -f "$cache" ]] || { printf '%s\n' 'Configure and build the desktop application first.' >&2; exit 2; }
configuration=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache")
[[ -z "$configuration" || "$configuration" == Release ]] || {
    printf '%s\n' 'Select a Release build directory for packaging.' >&2; exit 2;
}
source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")
[[ -f "$source/packaging/macos/Info.plist.in" ]] || exit 2
output=${output:-"$source/out/artifacts/macos"}
mkdir -p -- "$output"
output=$(cd "$output" && pwd -P)
stage=$(mktemp -d "$output/.macos-stage.XXXXXXXX")
cleanup() { [[ "$stage" == "$output"/.macos-stage.* ]] && rm -rf -- "$stage"; }
trap cleanup EXIT

cmake -S "$source" -B "$build" -DANANARCHIVER_DEPLOY_RUNTIME=ON
# Record final provenance after the executable build, including when this cache
# already contains metadata from a previous application revision.
cmake --build "$build" --config Release --target ananarchiver_build_info --parallel "${ANANARCHIVER_JOBS:-4}"
metadata="$build/frontend/qt/packaging-metadata.txt"
version=$(sed -n 's/^version=//p' "$metadata")
qt_version=$(sed -n 's/^qt_version=//p' "$metadata")
qt_prefix=$(sed -n 's/^qt_prefix=//p' "$metadata")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf '%s\n' 'Invalid package version.' >&2; exit 2; }
deploy="$qt_prefix/bin/macdeployqt"
[[ -x "$deploy" ]] || { printf '%s\n' 'The recorded Qt kit has no macdeployqt.' >&2; exit 2; }
cmake --install "$build" --config Release --prefix "$stage"
app="$stage/AnAnArchiver.app"
[[ -x "$app/Contents/MacOS/AnAnArchiver" ]] || exit 1
resources="$app/Contents/Resources"
install -m 644 "$metadata" "$resources/packaging-metadata.txt"
install -m 644 "$source/packaging/macos/README.md" "$stage/README.md"
for provenance in dependency-provenance.json resolved-dependencies.json; do
    if [[ -f "$build/$provenance" ]]; then install -m 644 "$build/$provenance" "$resources/$provenance"; fi
done
backend_patch="$stage/share/AnAnArchiver/libarchive-compatibility.patch"
if [[ -f "$backend_patch" ]]; then
    mkdir -p "$resources/share/AnAnArchiver"
    install -m 644 "$backend_patch" "$resources/share/AnAnArchiver/libarchive-compatibility.patch"
fi
license_directory=${ANANARCHIVER_QT_LICENSE_DIRECTORY:-"$source/third_party/qt/LICENSES"}
python3 "$source/packaging/linux/collect_licenses.py" \
    --project-root "$source" --source-root "${ANANARCHIVER_DEPENDENCY_SOURCE_DIRECTORY:-$source}" \
    --manifest "$build/resolved-dependencies.json" --output "$resources/licenses" \
    --qt-license-dir "$license_directory" --qt-prefix "$qt_prefix" --qt-version "$qt_version"

deploy_args=(AnAnArchiver.app "-qmldir=$source/frontend/qt/qml" -always-overwrite -verbose=2)
# These runtime SVG plugins are used by QML icons but do not create a direct
# link from the executable to QtSvg. Explicit scanning deploys their frameworks.
for plugin in imageformats/libqsvg.dylib iconengines/libqsvgicon.dylib; do
    [[ -f "$qt_prefix/plugins/$plugin" ]] || { printf '%s\n' "Missing Qt SVG plugin: $plugin" >&2; exit 1; }
    mkdir -p "$app/Contents/PlugIns/$(dirname "$plugin")"
    cp "$qt_prefix/plugins/$plugin" "$app/Contents/PlugIns/$plugin"
    deploy_args+=("-executable=$app/Contents/PlugIns/$plugin")
done
if [[ -n "$identity" ]]; then
    deploy_args+=("-sign-for-notarization=$identity")
fi
plutil -lint "$app/Contents/Info.plist"
arch=$(lipo -archs "$app/Contents/MacOS/AnAnArchiver" | tr ' ' '-')
name="AnAnArchiver-$version-macos-$arch"
printf 'version=%s\nqt_version=%s\narchitecture=%s\n' "$version" "$qt_version" "$arch" > "$resources/package-verification.txt"

# Sign only after all bundle resources have reached their final state. Run in
# the staging directory so a DMG never acquires the developer's full path name.
pushd "$stage" >/dev/null
"$deploy" "${deploy_args[@]}"
codesign --verify --deep --strict --verbose=2 AnAnArchiver.app
while IFS= read -r -d '' binary; do
    if file "$binary" | grep -q 'Mach-O'; then
        if otool -L "$binary" | tail -n +2 | awk '{print $1}' | grep '^/' | grep -Ev '^(/System/Library/|/usr/lib/)' ; then
            printf '%s\n' "Non-system absolute dependency remains in $binary" >&2; exit 1
        fi
    fi
done < <(find AnAnArchiver.app -type f -print0)
env -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH -u DYLD_LIBRARY_PATH \
    ANANARCHIVER_SETTINGS_FILE="$stage/smoke-settings/settings.ini" \
    ./AnAnArchiver.app/Contents/MacOS/AnAnArchiver --smoke-test 500
ditto -c -k --sequesterRsrc --keepParent AnAnArchiver.app "$name.zip"
if $make_dmg; then
    # Include the standard Applications shortcut and installation instructions.
    mkdir dmg-root
    cp -R AnAnArchiver.app dmg-root/
    cp README.md dmg-root/
    ln -s /Applications dmg-root/Applications
    hdiutil create -volname AnAnArchiver -srcfolder dmg-root -format UDZO "$name.dmg"
    if [[ -n "$identity" ]]; then codesign --timestamp --sign "$identity" "$name.dmg"; fi
    mv -f -- "$name.dmg" "$output/$name.dmg"
    (cd "$output" && shasum -a 256 "$name.dmg" > "$name.dmg.sha256")
fi
mv -f -- "$name.zip" "$output/$name.zip"
(cd "$output" && shasum -a 256 "$name.zip" > "$name.zip.sha256")
popd >/dev/null
printf '%s\n' "$output/$name.zip"
