"""Generate an OS-drag fixture and verify an independently copied directory.

This tool never extracts or copies archive entries to the target. Explorer (or
another real drop consumer) must create that output before verification.
"""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(root):
    root.mkdir(parents=True, exist_ok=False)
    files = {
        f'many/file-{i:07d}.txt': f'LiteArc Explorer fixture {i}\n'.encode()
        for i in range(300)
    }
    files['隐式/子目录/可可🐄🐈.txt'] = '中文 / 日本語 / 한국어 / café\n'.encode()
    archive = root / 'folders.xxx'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_STORED) as output:
        output.writestr('empty/', b'')
        for name, data in files.items():
            output.writestr(name, data)
    expected = {
        'archive_sha256': digest(archive),
        'directories': ['empty', 'many', '隐式', '隐式/子目录'],
        'files': {
            name: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            for name, data in files.items()
        },
    }
    (root / 'expected.json').write_text(
        json.dumps(expected, ensure_ascii=False, indent=2) + '\n', encoding='utf-8'
    )
    (root / 'plain.txt').write_text('OS drop creation input\n', encoding='utf-8')
    (root / 'target').mkdir()
    print(f'Created {archive}: 3 top-level directories, 301 files')


def verify(root, target, report):
    expected = json.loads((root / 'expected.json').read_text(encoding='utf-8'))
    if digest(root / 'folders.xxx') != expected['archive_sha256']:
        raise ValueError('Fixture archive changed')
    if not target.is_dir():
        raise ValueError(f'Target directory does not exist: {target}')
    directories, files = [], {}
    for path in target.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Unexpected symlink: {path}')
        name = path.relative_to(target).as_posix()
        if path.is_dir():
            directories.append(name)
        else:
            files[name] = {'size': path.stat().st_size, 'sha256': digest(path)}
    if sorted(directories) != sorted(expected['directories']):
        raise ValueError(f'Directory mismatch: {directories}')
    if files != expected['files']:
        missing = sorted(expected['files'].keys() - files.keys())
        extra = sorted(files.keys() - expected['files'].keys())
        changed = sorted(k for k in files.keys() & expected['files'].keys()
                         if files[k] != expected['files'][k])
        raise ValueError(f'File mismatch: missing={missing}, extra={extra}, changed={changed}')
    result = {
        'target': str(target.resolve()),
        'archive_sha256': expected['archive_sha256'],
        'directories': len(directories),
        'files': len(files),
        'bytes': sum(v['size'] for v in files.values()),
        'verified': True,
    }
    if report:
        report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n',
                          encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'verify'])
    parser.add_argument('root', type=Path)
    parser.add_argument('--target', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.action == 'prepare':
        prepare(args.root)
    else:
        verify(args.root, args.target or args.root / 'target', args.report)


if __name__ == '__main__':
    main()
