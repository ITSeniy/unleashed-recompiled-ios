#!/usr/bin/env python3
"""Prepare an isolated iOS prototype; requires Python 3.12+, curl and git.

The platform fork is pinned. Other dependencies and resource inputs come from
this Android checkout. Never modifies its existing sources or dependencies.
"""
import argparse
import configparser
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
# name: (destination, GitHub repository, revision, archive sha256)
SOURCES = {
    'reference': ('.', 'Loganbogan9/UnleashedRecompiOS', '5b2348e0c556a35143f473c4277b89d526675a95', '13be3277f20b8f94c4e871e42e4fb2d17bb74597aa2cf1e1369c1b8e3ba3f87d'),
    'plume': ('thirdparty/plume', 'NerunSmarts/plume', '6bf1caaa504fffaf515f19aa77619f0c13096a84', '109d00b891666962e83123ce740dbf6e2d34987b56920e7d73e3682c29d59d30'),
    'xenos': ('tools/XenosRecomp', 'squidbus/XenosRecomp', '490699203914fe240b86ccf2401fe1f7d37b1bef', '1d76adbfa6a22194d22ca4ff3260c84580c735291717d67c67fc9bfe64f4a701'),
    'xenon': ('tools/XenonRecomp', 'hedge-dev/XenonRecomp', 'c5bfd90d87f2ed0db8cff5c19ea3aff0e161e527', '507c059461e359efe34e7009498463da57c962c3704f516902353bb5b83b0d43'),
    'sdl': ('thirdparty/SDL', 'NerunSmarts/SDL', '90029b7213e104907abcf9fd50330795264aa1b1', 'f4d5c4bd0a2751613c759a7122643fa277f279974b3a965fca55dd862a16f9dc'),
    'zstd': ('tools/XenosRecomp/thirdparty/zstd', 'facebook/zstd', 'refs/tags/v1.5.7', '37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3'),
    'freetype': ('thirdparty/freetype', 'freetype/freetype', 'refs/tags/VER-2-13-3', 'bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103'),
    'curl': ('thirdparty/curl', 'curl/curl', 'refs/tags/curl-8_11_1', '78baef817c33a0a11b990d031a706dc1486f614b581e22949fc101add32c4cc8'),
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


def link_submodules(stage, relative=Path('.')):
    module_file = stage / relative / '.gitmodules'
    if not module_file.exists():
        return
    config = configparser.ConfigParser()
    config.read(module_file)
    for section in config.sections():
        child = relative / config[section]['path']
        dest, local = stage / child, ROOT / child
        if not dest.exists() or not any(dest.iterdir()):
            if not local.is_dir() or not any(local.iterdir()):
                raise RuntimeError(f'Missing vendored dependency: {local}')
            if dest.exists():
                dest.rmdir()
            dest.symlink_to(local, target_is_directory=True)
        elif not dest.is_symlink():
            link_submodules(stage, child)


def patch(source, patch_path, ceiling):
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(ceiling))
    subprocess.run(['git', 'apply', '--check', str(patch_path)], cwd=source, env=env, check=True)
    subprocess.run(['git', 'apply', str(patch_path)], cwd=source, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/ios')
    args = parser.parse_args()
    output = args.output.resolve()
    stage, downloads = output / 'source', output / 'downloads'
    if stage.exists():
        raise RuntimeError(f'{stage} already exists. Choose a fresh --output; existing work is preserved.')
    for name in ('default.xex', 'default.xexp', 'shader.ar'):
        if not (ROOT / 'UnleashedRecompLib/private' / name).is_file():
            raise RuntimeError(f'Missing original game input: {name}')
    downloads.mkdir(parents=True, exist_ok=True)
    for name, (dest, repo, revision, digest) in SOURCES.items():
        extract(archive(name, repo, revision, digest, downloads), stage / dest)
    link_submodules(stage)
    for relative in ('UnleashedRecomp/api', 'UnleashedRecompResources', 'thirdparty/bcdec', 'thirdparty/etcpak'):
        dest = stage / relative
        if dest.is_symlink():
            continue
        if dest.exists():
            dest.rmdir()  # only the empty submodule placeholder
        dest.symlink_to(ROOT / relative, target_is_directory=True)
    private = stage / 'UnleashedRecompLib/private'
    private.mkdir(exist_ok=True)
    for name in ('default.xex', 'default.xexp', 'shader.ar'):
        shutil.copyfile(ROOT / 'UnleashedRecompLib/private' / name, private / name)
    # The upstream game CMake file uses CRLF; our reviewable patch uses LF.
    for relative in ('UnleashedRecomp/CMakeLists.txt', 'UnleashedRecomp/user/config_def.h', 'UnleashedRecomp/install/installer.cpp'):
        source = stage / relative
        source.write_text(source.read_text())
    patch(stage, ROOT / 'ios/patches/game-ios-etc2.patch', output)
    patch(stage / 'thirdparty/plume', ROOT / 'ios/patches/plume-etc2.patch', output)
    # Applied after the fork's Metal lifetime/lifecycle fixes by its CMake setup.
    shutil.copyfile(ROOT / 'ios/patches/plume-ios-pacing.patch', stage / 'patches/dependencies/plume-0008-ios-pacing.patch')
    shutil.copyfile(ROOT / 'ios/patches/plume-ios-gpu-profile.patch', stage / 'patches/dependencies/plume-0009-ios-gpu-profile.patch')
    shutil.copyfile(ROOT / 'ios/patches/plume-ios-metalfx.patch', stage / 'patches/dependencies/plume-0010-ios-metalfx.patch')
    shutil.copyfile(ROOT / 'ios/gpu/bc_texture_upload.inc', stage / 'UnleashedRecomp/gpu/bc_texture_upload.inc')
    shutil.copyfile(ROOT / 'ios/cmake/Dependencies.cmake', stage / 'ios_dependencies.cmake')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (output / 'android-base.txt').write_text(revision + '\n')
    print(f'Prepared {stage} (Android dependency base {revision})')


if __name__ == '__main__':
    main()
