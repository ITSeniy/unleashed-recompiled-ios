#!/usr/bin/env python3
"""Package the existing arm64 Release app as an unsigned IPA for re-signing.

Only the temporary copy loses its signature and provisioning profile.
Requires macOS codesign and Python 3.9+; no signing identity is needed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def signature_info(path):
    data = path.read_bytes()
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError(f'Expected a thin little-endian Mach-O: {path}')
    offset = 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, offset)
        if command == 0x1D:  # LC_CODE_SIGNATURE
            start, _ = struct.unpack_from('<II', data, offset + 8)
            magic, _, count = struct.unpack_from('>III', data, start)
            if magic != 0xFADE0CC0:
                raise ValueError('Unexpected code signature format')
            for index in range(count):
                slot, relative = struct.unpack_from('>II', data, start + 12 + index * 8)
                if slot == 5:  # XML entitlements slot (also present beside DER)
                    _, length = struct.unpack_from('>II', data, start + relative)
                    return True, plistlib.loads(data[start + relative + 8:start + relative + length])
            return True, {}
        offset += size
    return False, {}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT / 'out/ios/build/UnleashedRecomp/Release-iphoneos/Unleashed Recompiled.app')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/ios/dist/UnleashedRecompiled-iOS-SE3-test-unsigned.ipa')
    args = parser.parse_args()
    app, output = args.app.resolve(), args.output.resolve()
    manifest_path = output.with_suffix('.json')
    if output.exists() or manifest_path.exists():
        raise FileExistsError('Output already exists; choose a fresh --output.')
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    executable = app / info['CFBundleExecutable']
    with executable.open('rb') as file:
        header = file.read(8)
    if header != struct.pack('<II', 0xFEEDFACF, 0x0100000C):
        raise ValueError('This package requires a thin arm64 executable.')
    original_hash = sha256(executable)
    _, entitlements = signature_info(executable)
    standard_keys = {'application-identifier', 'com.apple.developer.team-identifier', 'get-task-allow', 'keychain-access-groups'}
    if set(entitlements) - standard_keys:
        raise ValueError(f'Review custom entitlements before packaging: {sorted(set(entitlements) - standard_keys)}')
    if any(path.is_symlink() for path in app.rglob('*')):
        raise ValueError('Review app symlinks before packaging.')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='ipa-package-', dir=output.parent) as directory:
        root = Path(directory)
        copied = root / 'Payload' / app.name
        shutil.copytree(app, copied)
        for path in copied.rglob('*'):
            if path.is_file():
                with path.open('rb') as file:
                    is_macho = file.read(4) == b'\xcf\xfa\xed\xfe'
                if is_macho and signature_info(path)[0]:
                    subprocess.run(['codesign', '--remove-signature', str(path)], check=True)
                    if signature_info(path)[0]:
                        raise RuntimeError(f'Signature removal failed: {path}')
        for path in list(copied.rglob('_CodeSignature')):
            shutil.rmtree(path)
        for path in list(copied.rglob('embedded.mobileprovision')):
            path.unlink()
        unsigned_hash = sha256(copied / info['CFBundleExecutable'])
        temporary_ipa = root / 'app.ipa'
        with zipfile.ZipFile(temporary_ipa, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted((root / 'Payload').rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(root).as_posix())
        with zipfile.ZipFile(temporary_ipa) as archive:
            if archive.testzip() is not None:
                raise RuntimeError('IPA integrity check failed')
            entries = archive.namelist()
            if any('_CodeSignature/' in name or name.endswith('embedded.mobileprovision') for name in entries):
                raise RuntimeError('IPA still contains signing material')
            binary_entry = archive.getinfo(f'Payload/{app.name}/{info["CFBundleExecutable"]}')
            if not (binary_entry.external_attr >> 16) & 0o111:
                raise RuntimeError('Executable permissions were lost')
        temporary_ipa.replace(output)
    if sha256(executable) != original_hash:
        raise RuntimeError('Original app was unexpectedly modified')
    manifest = {
        'ipa': output.name, 'ipa_sha256': sha256(output), 'ipa_bytes': output.stat().st_size,
        'configuration': 'Release', 'architecture': 'arm64', 'minimum_ios': info['MinimumOSVersion'],
        'bundle_id_before_resigning': info['CFBundleIdentifier'], 'version': info['CFBundleShortVersionString'],
        'source_binary_sha256': original_hash, 'unsigned_binary_sha256': unsigned_hash,
        'source_entitlement_keys': sorted(entitlements), 'required_custom_entitlements': [],
        'signed': False, 'provisioning_profile_included': False, 'game_data_included': False,
        'se3_runtime_tested': False,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'IPA: {output}\nManifest: {manifest_path}\nSize: {output.stat().st_size / 1048576:.1f} MiB')


if __name__ == '__main__':
    main()
