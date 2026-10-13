#!/usr/bin/env python3
"""Download the dependencies the iOS build pins; requires Python 3.12+ and curl.

The game sources are this repository. iOS only replaces plume, SDL and
XenosRecomp (Metal backend and AIR shader cache) and builds FreeType and curl
from source. Their empty submodule directories are linked to the vendored
copies in this checkout. Nothing outside the output directory is modified.
"""
import argparse
import configparser
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
# name: (destination, GitHub repository, revision, archive sha256, vendored counterpart)
SOURCES = {
    'plume': ('plume', 'NerunSmarts/plume', '6bf1caaa504fffaf515f19aa77619f0c13096a84', '109d00b891666962e83123ce740dbf6e2d34987b56920e7d73e3682c29d59d30', 'thirdparty/plume'),
    'xenos': ('XenosRecomp', 'squidbus/XenosRecomp', '490699203914fe240b86ccf2401fe1f7d37b1bef', '1d76adbfa6a22194d22ca4ff3260c84580c735291717d67c67fc9bfe64f4a701', 'tools/XenosRecomp'),
    'sdl': ('SDL', 'NerunSmarts/SDL', '90029b7213e104907abcf9fd50330795264aa1b1', 'f4d5c4bd0a2751613c759a7122643fa277f279974b3a965fca55dd862a16f9dc', None),
    'zstd': ('XenosRecomp/thirdparty/zstd', 'facebook/zstd', 'refs/tags/v1.5.7', '37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3', None),
    'freetype': ('freetype', 'freetype/freetype', 'refs/tags/VER-2-13-3', 'bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103', None),
    'curl': ('curl', 'curl/curl', 'refs/tags/curl-8_11_1', '78baef817c33a0a11b990d031a706dc1486f614b581e22949fc101add32c4cc8', None),
}


def archive(name, repo, revision, digest, downloads):
    path = downloads / (name + '.tar.gz')
    if not path.exists():
        cached = {'freetype': 'freetype-freetype-VER-2-13-3.tar.gz', 'curl': 'curl-curl-curl-8_11_1.tar.gz'}.get(name)
        cache = ROOT / 'thirdparty/vcpkg/downloads' / (cached or '')
        if cached and cache.is_file():
            shutil.copyfile(cache, path)
        else:
            partial = path.with_suffix('.part')
            subprocess.run(['curl', '-fL', '--retry', '2', f'https://codeload.github.com/{repo}/tar.gz/{revision}', '-o', str(partial)], check=True)
            partial.rename(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'Archive checksum mismatch: {path}')
    return path


def extract(path, destination):
    # All archives have one enclosing directory. tarfile's data filter also
    # rejects traversal and links escaping the extraction directory.
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(path) as tar:
        members = tar.getmembers()
        prefix = members[0].name.split('/')[0] + '/'
        for member in members:
            if member.name == prefix[:-1]:
                continue
            if not member.name.startswith(prefix):
                raise RuntimeError('Unexpected archive root')
            member.name = member.name[len(prefix):]
            if member.islnk() and member.linkname.startswith(prefix):
                member.linkname = member.linkname[len(prefix):]
            tar.extract(member, destination, filter='data')


def link_submodules(dependency, vendored):
    # Archives omit submodules; reuse the vendored checkout's copies.
    module_file = dependency / '.gitmodules'
    if not module_file.exists():
        return
    config = configparser.ConfigParser()
    config.read(module_file)
    for section in config.sections():
        child = config[section]['path']
        dest, local = dependency / child, vendored / child
        if dest.exists() and any(dest.iterdir()):
            continue
        if not local.is_dir() or not any(local.iterdir()):
            raise RuntimeError(f'Missing vendored dependency: {local}')
        if dest.exists():
            dest.rmdir()
        dest.symlink_to(local, target_is_directory=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/ios')
    args = parser.parse_args()
    output = args.output.resolve()
    deps, downloads = output / 'deps', output / 'downloads'
    if deps.exists():
        raise RuntimeError(f'{deps} already exists. Remove it to download the dependencies again.')
    downloads.mkdir(parents=True, exist_ok=True)
    for name, (dest, repo, revision, digest, _) in SOURCES.items():
        extract(archive(name, repo, revision, digest, downloads), deps / dest)
    for dest, *_, vendored in SOURCES.values():
        if vendored:
            link_submodules(deps / dest, ROOT / vendored)
    print(f'Prepared iOS dependencies in {deps}')


if __name__ == '__main__':
    main()
