#!/usr/bin/env python3
"""Composite production GLES glass and native MD3 draw data on a synthetic backdrop."""
import argparse
import hashlib
import json
from pathlib import Path

import moderngl
import numpy as np

from render_md3 import render

SHADERS = Path(__file__).resolve().parents[1] / 'jni/src/core/shaders'
METHOD = ('Synthetic test backdrop + production GLES liquid-glass shader + native ImGui '
          'draw data. Software preview, not an Android screenshot.')


def backdrop(width, height):
    """Top-down test image with enough contrast to expose blur and refraction."""
    y, x = np.mgrid[:height, :width].astype('f4')
    grid = ((x // 72 + y // 72) % 2) * .045
    return np.stack((.16 + .32 * x / width + grid,
                     .19 + .28 * y / height + grid,
                     .46 + .14 * x / width - .13 * y / height + grid), axis=2)


def glass_groups(rects):
    """Mirror BuildGlassGroup: four groups, four shapes, first shape's material."""
    assert len(rects) <= 8, 'Production supports at most eight glass rectangles'
    for rect in rects:
        assert all(np.isfinite(v) for v in rect.values() if isinstance(v, (int, float)))
    for group in range(4):
        selected = [r for r in rects if r['group'] == group and
                    r['w'] >= 2 and r['h'] >= 2 and r['alpha'] > .001][:4]
        if not selected:
            continue
        shapes = np.zeros((4, 4), dtype='f4')
        for i, r in enumerate(selected):
            shapes[i] = (r['x'] + r['w'] / 2, r['y'] + r['h'] / 2,
                         r['w'] / 2, r['h'] / 2 * (-1 if r['hardJoin'] else 1))
        yield selected[0], shapes


def paint(ctx, width, height, framebuffer, metadata, report):
    background = backdrop(width, height)
    rgba = np.concatenate((background, np.ones((height, width, 1), dtype='f4')), axis=2)
    # GL framebuffer storage is bottom-up; the shader's source sampler uses top-down pixels.
    pixels = rgba[::-1].copy()
    report.update(glass_groups=0, glass_changed_pixels=0)
    if metadata:
        assert (metadata['width'], metadata['height']) == (width, height)
        groups = list(glass_groups(metadata['glass_rects']))
        assert groups, 'Glass capture has no eligible production glass shapes'
        vertex = (SHADERS / 'glass_gl.vert').read_text()
        fragment = (SHADERS / 'glass_gl.frag').read_text().replace(
            '#include "glass_common.glsl"', (SHADERS / 'glass_common.glsl').read_text())
        program = ctx.program(vertex_shader=vertex, fragment_shader=fragment)
        source = ctx.texture((width, height), 3, background.tobytes(), dtype='f4')
        source.filter = (moderngl.LINEAR, moderngl.LINEAR)
        source.repeat_x = source.repeat_y = False
        target = ctx.texture((width, height), 4, pixels.tobytes(), dtype='f4')
        glass_framebuffer = ctx.framebuffer(color_attachments=[target])
        glass_framebuffer.use()
        ctx.viewport = (0, 0, width, height)
        ctx.scissor = None
        ctx.disable(moderngl.DEPTH_TEST | moderngl.CULL_FACE)
        ctx.enable(moderngl.BLEND)
        ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA,
                          moderngl.ONE, moderngl.ONE_MINUS_SRC_ALPHA)
        source.use(0)
        program['uScreenTex'].value = 0
        program['uScreenSize'].value = (width, height)
        program['uSurface'].value = (width, height)
        program['uFlipY'].value = False
        vao = ctx.vertex_array(program, [])
        for lead, shapes in groups:
            program['uParams'].value = tuple(lead[k] for k in ('rounding', 'edgeWidth', 'bend', 'alpha'))
            program['uTint'].value = tuple(lead[k] for k in ('tintR', 'tintG', 'tintB', 'tintA'))
            program['uParams2'].value = tuple(lead[k] for k in ('blur', 'lightX', 'lightY', 'merge'))
            program['uShapes'].write(shapes.tobytes())
            vao.render(moderngl.TRIANGLE_STRIP, vertices=4)
        pixels = np.frombuffer(glass_framebuffer.read(components=4, dtype='f4'), dtype='f4')
        pixels = pixels.reshape(height, width, 4)
        assert np.isfinite(pixels).all(), 'Non-finite production shader output'
        changed = np.max(np.abs(pixels[:, :, :3] - rgba[::-1, :, :3]), axis=2) > 1 / 255
        assert changed.any(), 'Production glass pass did not change the backdrop'
        report.update(glass_groups=len(groups), glass_changed_pixels=int(changed.sum()))
        vao.release(); glass_framebuffer.release(); target.release(); source.release(); program.release()
    framebuffer.color_attachments[0].write((np.clip(pixels, 0, 1) * 255).astype('u1').tobytes())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path, help='Native main-app .draw file or directory')
    p.add_argument('--output', type=Path, default=Path('build/md3-main-preview'))
    a = p.parse_args()
    paths = sorted(a.source.glob('*.draw')) if a.source.is_dir() else [a.source]
    assert paths, 'No native draw captures were supplied'
    context = moderngl.create_standalone_context(backend='egl')
    manifest = {'method': METHOD, 'renderer': context.info['GL_RENDERER'], 'shader_sha256': {}, 'captures': []}
    for name in ('glass_gl.vert', 'glass_gl.frag', 'glass_common.glsl'):
        manifest['shader_sha256'][name] = hashlib.sha256((SHADERS / name).read_bytes()).hexdigest()
    for path in paths:
        geometry = path.with_suffix('.glass.json')
        metadata = json.loads(geometry.read_text()) if geometry.exists() else None
        output = a.output / (path.stem + '.png')
        report = {'image': output.name, 'native_draw': str(path),
                  'glass_geometry': str(geometry) if metadata else None}
        report.update(render(context, path, output,
                             lambda ctx, w, h, fbo: paint(ctx, w, h, fbo, metadata, report)))
        manifest['captures'].append(report)
    (a.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(METHOD)
    print('Renderer:', context.info['GL_RENDERER'])


if __name__ == '__main__':
    main()
