#!/usr/bin/env python3
"""Generate guest code, iPhone Metal cache and embedded resources on the Mac.

Guest code, SPIR-V headers and resources go to their usual (ignored) places in
this checkout and match what the Android build generates. The game shader cache
has the iOS XenosRecomp layout, so it goes to <output>/generated instead.
"""
import argparse
import os
from pathlib import Path
import platform
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def hlsl_headers():
    # iOS still includes the renderer's SPIR-V headers even with native Metal.
    # DXC runs on the Mac, not on the device; use the vendored host library.
    arch = 'arm64' if platform.machine() == 'arm64' else 'x64'
    dxc_root = ROOT / 'tools/XenosRecomp/thirdparty/dxc-bin'
    env = dict(os.environ)
    env['DYLD_LIBRARY_PATH'] = str(dxc_root / 'lib' / arch) + (':' + env['DYLD_LIBRARY_PATH'] if env.get('DYLD_LIBRARY_PATH') else '')
    app = ROOT / 'UnleashedRecomp'
    for kind, name in re.findall(r'^compile_(vertex|pixel)_shader\(([^)]+)\)', (app / 'CMakeLists.txt').read_text(), re.M):
        source = app / 'gpu/shader/hlsl' / (name + '.hlsl')
        args = [str(dxc_root / 'bin' / arch / 'dxc-macos'), '-T', 'vs_6_0' if kind == 'vertex' else 'ps_6_0', '-HV', '2021', '-all-resources-bound', '-spirv', '-fvk-use-dx-layout', '-DUNLEASHED_RECOMP', '-E', 'shaderMain', '-Fh', str(source) + '.spirv.h', str(source), '-Vn', 'g_' + name + '_spirv']
        if kind == 'vertex':
            args.append('-fvk-invert-y')
        subprocess.run(args, env=env, check=True)


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/ios')
    args = parser.parse_args()
    output = args.output.resolve()
    host = output / 'host'
    private = ROOT / 'UnleashedRecompLib/private'
    for name in ('default.xex', 'default.xexp', 'shader.ar'):
        if not (private / name).is_file():
            raise RuntimeError(f'Missing original game input: {private / name}')
    # The host tools have these paths compiled in (see ios/host/CMakeLists.txt).
    (output / 'generated').mkdir(parents=True, exist_ok=True)
    run(host / 'x_decompress/x_decompress', private / 'shader.ar', private / 'shader_decompressed.ar')
    run(host / 'xenon/XenonRecomp/XenonRecomp')
    run(host / 'xenos/XenosRecomp/XenosRecomp')
    hlsl_headers()
    app = ROOT / 'UnleashedRecomp'
    text = (app / 'CMakeLists.txt').read_text()
    count = 0
    for match in re.finditer(r'^BIN2C\((.*?)\)$', text, re.M):
        fields = dict(re.findall(r'(SOURCE_FILE|DEST_FILE|ARRAY_NAME|COMPRESSION_TYPE)\s+"([^"]+)"', match[1]))
        def resolve(value):
            return value.replace('${RESOURCES_SOURCE_PATH}', str(ROOT / 'UnleashedRecompResources')).replace('${RESOURCES_OUTPUT_PATH}', str(app / 'res'))
        source = resolve(fields['SOURCE_FILE'])
        destination = resolve(fields.get('DEST_FILE', fields['SOURCE_FILE']))
        Path(destination).parent.mkdir(parents=True, exist_ok=True)
        run(host / 'file_to_c/file_to_c', source, fields['ARRAY_NAME'], fields.get('COMPRESSION_TYPE', 'none'), destination + '.c', destination + '.h')
        count += 1
    print(f'Generated {count} resources.')


if __name__ == '__main__':
    main()
