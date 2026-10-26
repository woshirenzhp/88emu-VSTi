"""Package the current source snapshot and available dependencies, without ROMs or local outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = 'gearmulator-88emu-PCv2-source'
INCLUDED_ROOTS = ('source', 'scripts', 'doc', 'deploy', 'installer', '.github', 'vsthost-main')
ROOT_FILES = {'README.md', 'LICENSE.md', 'CMakeLists.txt', 'CMakePresets.json',
              '.gitignore', '.gitattributes', '.gitmodules', 'ISSUES.md', 'linux_dependencies.sh'}
SKIP_DIRS = {'.git', '.svn', '.vs', '.vscode', '.cache', '__pycache__', 'CMakeFiles', 'node_modules'}
SKIP_SUFFIXES = {'.exe', '.dll', '.pdb', '.obj', '.o', '.a', '.lib', '.ilk', '.idb', '.pch', '.pyc',
                 '.log', '.rom', '.mid', '.midi', '.sf2', '.dmp', '.aps', '.tlog'}
SKIP_NAMES = {'.git', '.env', 'CMakeCache.txt', 'CMakeUserPresets.json', '.DS_Store', 'Thumbs.db',
              'cmake_install.cmake', 'compile_commands.json', 'install_manifest.txt'}


def eligible(path):
    return (not path.is_symlink() and path.name not in SKIP_NAMES
            and path.suffix.lower() not in SKIP_SUFFIXES
            and path.relative_to(ROOT).as_posix() != 'source/3rdparty/portmidi/portmidi_cdt.zip'
            and (not any(part.lower() in {'vstsdk2.4', 'vstsdk2.4.2'} for part in path.parts)
                 or path.name in {'CMakeLists.txt', 'README.md', 'LICENSE', 'LICENSE.md'}))


def sources():
    paths = []
    for path in ROOT.iterdir():
        if path.is_file() and (path.name in ROOT_FILES or
                               (path.name.startswith('build_') and path.suffix in ('.bat', '.sh'))):
            paths.append(path)
    for name in INCLUDED_ROOTS:
        folder = ROOT / name
        if not folder.is_dir():
            continue
        for parent, dirs, files in os.walk(folder, followlinks=False):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not (Path(parent) / d).is_symlink())
            if Path(parent).relative_to(ROOT).as_posix() == 'source/3rdparty':
                dirs[:] = [d for d in dirs if d.lower() not in {'asiosdk', 'vst2_sdk'}]
            paths += [Path(parent) / f for f in files if eligible(Path(parent) / f)]
    return sorted(paths, key=lambda p: p.relative_to(ROOT).as_posix())


def verify(archive):
    with zipfile.ZipFile(archive) as zipped:
        manifest = json.loads(zipped.read(f'{PREFIX}/SOURCE_MANIFEST.json'))
        expected = {f'{PREFIX}/{entry["path"]}' for entry in manifest}
        expected.update({f'{PREFIX}/SOURCE_MANIFEST.json', f'{PREFIX}/SOURCE_PACKAGE.json'})
        assert set(zipped.namelist()) == expected, 'Archive has missing or unexpected entries'
        for entry in manifest:
            data = zipped.read(f'{PREFIX}/{entry["path"]}')
            assert len(data) == entry['bytes'], entry['path']
            assert hashlib.sha256(data).hexdigest() == entry['sha256'], entry['path']
        assert all('/.git/' not in n and '/sc_backup/' not in n for n in expected)
    print(f'Verified SHA-256 for all {len(manifest):,} source files.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist' / f'{PREFIX}.zip')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    archive = args.output.resolve()
    if not args.verify_only:
        archive.parent.mkdir(parents=True, exist_ok=True)
        manifest = []
        paths = sources()
        print(f'Packaging {len(paths):,} files, including available dependency source...', flush=True)
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
            for number, path in enumerate(paths, 1):
                relative = path.relative_to(ROOT).as_posix()
                data = path.read_bytes()
                zipped.writestr(f'{PREFIX}/{relative}', data)
                manifest.append({'path': relative, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
                if number % 5000 == 0:
                    print(f'Packed {number:,}/{len(paths):,} files.', flush=True)
            metadata = {
                'project': 'Gearmulator 2.2.28 / 88emuPlayer four-port VST3 PCv2',
                'included_roots': list(INCLUDED_ROOTS),
                'files': len(manifest), 'uncompressed_source_bytes': sum(x['bytes'] for x in manifest),
                'scope': 'Current workspace source snapshot with available dependency source; no Git history.',
                'excluded': ['bin, dist, temp, logs, sc_backup, vsthostx86',
                             'personal MIDI files and firmware ROMs', 'Git internals and developer caches',
                             'compiled executables/libraries, symbols and runtime logs',
                             'proprietary SDKs and portmidi_cdt.zip legacy binary archive'],
                'license': 'Main project GPL v3; dependency and legacy host license notices retained.',
                'build': 'See README.md and scripts/build_88vst3.ps1',
                'verification': 'SOURCE_MANIFEST.json lists byte counts and SHA-256 for every source file. '
                                'Manifest and package metadata themselves are not recursive manifest entries.'}
            zipped.writestr(f'{PREFIX}/SOURCE_MANIFEST.json', json.dumps(manifest, indent=2))
            zipped.writestr(f'{PREFIX}/SOURCE_PACKAGE.json', json.dumps(metadata, indent=2))
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
        print(f'Created {archive.name}: {archive.stat().st_size:,} bytes.', flush=True)
    verify(archive)


if __name__ == '__main__':
    main()
