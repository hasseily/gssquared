#!/usr/bin/env python3
"""Build all packaged shader formats from the shared GLSL sources.

Reference tools: glslang 16.5.0; SPIRV-Cross be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a;
DXC v1.9.2607. Tools are build-time dependencies, never runtime dependencies.
Run on Windows/Linux with DXC to regenerate DXIL. --without-dxil is useful for
Mac development but is deliberately not the release/default mode.
"""
import argparse
from pathlib import Path
import subprocess

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--glslang', default='glslangValidator')
    ap.add_argument('--spirv-cross', default='spirv-cross')
    ap.add_argument('--dxc', default='dxc')
    ap.add_argument('--without-dxil', action='store_true')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2] / 'assets/shaders/postprocess'
    for name, stage in [('fullscreen.vert', 'vert'), ('fullscreen_target.vert','vert'),
                        ('crt.frag','frag'), ('composite.frag','frag')]:
        src = root / name
        spv = root / (name + '.spv')
        subprocess.run([args.glslang, '-V', '--target-env', 'vulkan1.0', str(src), '-o', str(spv)], check=True)
        cross = [args.spirv_cross, str(spv)]
        subprocess.run(cross + ['--msl','--msl-decoration-binding','--output',str(root/(name+'.metal'))], check=True)
        subprocess.run(cross + ['--version','300','--es','--output',str(root/(name+'.gles'))], check=True)
        subprocess.run(cross + ['--version','330','--no-es','--output',str(root/(name+'.glsl'))], check=True)
        hlsl = root/(name+'.hlsl')
        subprocess.run(cross + ['--hlsl','--shader-model','60','--output',str(hlsl)], check=True)
        # Keep generated text reviewable and git diff --check clean.
        for suffix in ('.metal','.gles','.glsl','.hlsl'):
            output=root/(name+suffix)
            output.write_text('\n'.join(line.rstrip() for line in output.read_text().splitlines()).rstrip()+'\n')
        if not args.without_dxil:
            subprocess.run([args.dxc,'-T','vs_6_0' if stage=='vert' else 'ps_6_0',
                            '-E','main','-Fo',str(root/(name+'.dxil')),str(hlsl)],check=True)

if __name__ == '__main__':
    main()
