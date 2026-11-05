"""Small real ZIP with duplicates, implicit parents and Unicode; optional scale.

Generated archives are test inputs, never production data or committed assets.
"""
import argparse
from pathlib import Path
import warnings
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--entries', type=int, default=300)
parser.add_argument('--long-names', action='store_true')
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
warnings.filterwarnings('ignore', message='Duplicate name:')
with zipfile.ZipFile(args.output, 'w', compression=zipfile.ZIP_STORED) as archive:
    for path, data in [('隐式/子目录/可可🐄🐈.txt', b'hello'), ('empty/', b''),
                       ('duplicate.txt', b'first'), ('duplicate.txt', b'second'),
                       ('CAFÉ.txt', b'Unicode'), ('こんにちは.txt', b'Japanese'),
                       ('한국어.txt', b'Korean')]:
        archive.writestr(path, data)
    for i in range(args.entries):
        archive.writestr(f'many/file-{i:07d}.txt', b'')
    if args.long_names:
        for i in range(64):
            archive.writestr('long/' + '文' * 4000 + f'-{i:03d}.txt', b'')
print(args.output)
