"""Build the authoritative CMake pipeline, then stage its shared ABI and records.

ANANARCHIVER_DEPENDENCY_PREFIX optionally replays an existing verified native
resolution. Without it the existing superbuild resolves/builds managed sources.
No backend dependency logic is duplicated here.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import re


def run(args):
    subprocess.run([str(x) for x in args], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--architecture', default=platform.machine())
    parser.add_argument('--flutter-root', type=Path)
    args = parser.parse_args()
    app = Path(__file__).resolve().parents[1]
    root = app.parents[1]
    build = app / 'build' / 'native' / args.architecture.replace(' ', '_').replace(';', '_')
    prefix = os.environ.get('ANANARCHIVER_DEPENDENCY_PREFIX')
    configure = ['cmake', '-S', root, '-B', build, '-G', 'Ninja',
                 '-DCMAKE_BUILD_TYPE=Release', '-DANANARCHIVER_BUILD_QT=OFF',
                 '-DANANARCHIVER_BUILD_C_API=ON', '-DBUILD_TESTING=OFF',
                 f'-DANANARCHIVER_SUPERBUILD={"OFF" if prefix else "ON"}']
    if prefix:
        configure.append(f'-DANANARCHIVER_DEPENDENCY_PREFIX={Path(prefix).resolve()}')
    if platform.system() == 'Darwin':
        configure.append('-DCMAKE_OSX_ARCHITECTURES=' + args.architecture.replace(' ', ';'))
    run(configure)
    run(['cmake', '--build', build, '--parallel', os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL', '4')])
    native = build if prefix else build / 'application'
    name = {'Windows': 'ananarchiver_c_api.dll', 'Darwin': 'libananarchiver_c_api.dylib',
            'Linux': 'libananarchiver_c_api.so'}[platform.system()]
    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(native / name, args.output / name)
    records = args.output / 'litearc-build'
    records.mkdir(exist_ok=True)
    for record in ['dependency-provenance.json', 'resolved-dependencies.json', 'dependency-build.json']:
        for source in [native / record, build / record, root / 'dependencies' / record]:
            if source.is_file():
                shutil.copy2(source, records / record)
                break
    licenses = records / 'licenses'
    run(['cmake', '--install', build if prefix else native, '--prefix', app / 'build' / 'native-install'])
    installed = app / 'build' / 'native-install' / 'share' / 'AnAnArchiver' / 'licenses'
    if installed.is_dir():
        shutil.copytree(installed, licenses, dirs_exist_ok=True)
    provenance = json.loads((records / 'dependency-provenance.json').read_text(encoding='utf-8'))
    for dependency in provenance['dependencies']:
        source = (root / dependency['path']).resolve()
        source.relative_to(root / 'third_party')
        destination = licenses / dependency['name']
        notices = [p for p in source.rglob('*') if p.is_file() and
                   (re.match(r'^(LICENSE|COPYING|COPYRIGHT)([._-].*)?$', p.name, re.I)
                    or p.parent.name == 'LICENSES')]
        if not notices:
            raise RuntimeError(f"Missing native license notices: {dependency['name']}")
        for notice in notices:
            target = destination / notice.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, target)
    # Use the SDK selected by this runner. Never invoke flutter recursively from
    # flutter's own build: its cache lock and build environment belong to the
    # parent process, and PATH might select a different SDK.
    sdk = args.flutter_root
    if sdk is None:
        executable = shutil.which('flutter')
        if executable is None:
            raise RuntimeError('Flutter SDK not found; pass --flutter-root')
        sdk = Path(executable).resolve().parents[1]
    flutter = json.loads((sdk / 'bin/cache/flutter.version.json').read_text(encoding='utf-8'))
    flutter.pop('flutterRoot', None)
    flutter['nativeConfiguration'] = 'Release'
    flutter['architecture'] = args.architecture
    (records / 'flutter-sdk.json').write_text(json.dumps(flutter, indent=2)+'\n', encoding='utf-8')
    shutil.copy2(app / 'pubspec.lock', records / 'pubspec.lock')
    shutil.copy2(app / 'packages/litearc_binding/binding-source.json', records / 'binding-source.json')


if __name__ == '__main__':
    main()
