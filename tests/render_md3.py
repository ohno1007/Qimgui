#!/usr/bin/env python3
"""Render exact native ImGui draw streams with Mesa EGL (no HTML imitation)."""
import argparse
import struct
from pathlib import Path
import moderngl
import numpy as np
from PIL import Image

class Reader:
    def __init__(self, path):
        self.file = path.open('rb')
        assert self.file.read(8) == b'MD3DRAW1'
    def read(self, fmt):
        return struct.unpack('<' + fmt, self.file.read(struct.calcsize('<' + fmt)))
    def uint(self):
        return self.read('I')[0]

def render(ctx, path, output, paint_background=None):
    r = Reader(path)
    width, height, texture_count = r.read('III')
    textures = {}
    for _ in range(texture_count):
        identifier, w, h, bpp = r.read('QIII')
        pixels = r.file.read(w * h * bpp)
        if bpp == 1:
            alpha = np.frombuffer(pixels, dtype='u1').reshape(h, w)
            rgba = np.full((h, w, 4), 255, dtype='u1')
            rgba[:, :, 3] = alpha
            pixels = rgba.tobytes()
        assert bpp in (1, 4)
        texture = ctx.texture((w, h), 4, pixels, alignment=1)
        texture.filter = (moderngl.LINEAR, moderngl.LINEAR)
        texture.repeat_x = texture.repeat_y = False
        textures[identifier] = texture
    program = ctx.program(vertex_shader='''#version 330
        uniform vec2 viewport_size;
        in vec2 position; in vec2 texcoord; in vec4 color;
        out vec2 uv; out vec4 tint;
        void main() {
            gl_Position = vec4(position.x / viewport_size.x * 2.0 - 1.0,
                               1.0 - position.y / viewport_size.y * 2.0, 0, 1);
            uv = texcoord; tint = color;
        }
    ''', fragment_shader='''#version 330
        uniform sampler2D image;
        in vec2 uv; in vec4 tint; out vec4 pixel;
        void main() { pixel = tint * texture(image, uv); }
    ''')
    program['viewport_size'].value = (width, height)
    program['image'].value = 0
    target = ctx.texture((width, height), 4)
    framebuffer = ctx.framebuffer(color_attachments=[target])
    framebuffer.use(); framebuffer.clear(1, 1, 1, 1)
    if paint_background:
        paint_background(ctx, width, height, framebuffer)
        framebuffer.use()
    ctx.viewport = (0, 0, width, height)
    ctx.enable(moderngl.BLEND)
    ctx.blend_func = (moderngl.SRC_ALPHA, moderngl.ONE_MINUS_SRC_ALPHA,
                      moderngl.ONE, moderngl.ONE_MINUS_SRC_ALPHA)
    draw_calls = 0
    for _ in range(r.uint()):
        vertex_count = r.uint()
        vertices = r.file.read(vertex_count * 20)
        index_count = r.uint()
        indices = np.frombuffer(r.file.read(index_count * 4), dtype='<u4')
        vertex_buffer = ctx.buffer(vertices) if vertices else None
        for _ in range(r.uint()):
            identifier, elements, index_offset, vertex_offset, x1, y1, x2, y2 = r.read('QIIIffff')
            if not elements: continue
            x1, y1 = max(0, int(x1)), max(0, int(y1))
            x2, y2 = min(width, int(x2)), min(height, int(y2))
            if x2 <= x1 or y2 <= y1: continue
            assert identifier in textures, f'Missing native texture {identifier}'
            adjusted = indices[index_offset:index_offset + elements] + vertex_offset
            assert adjusted.max() < vertex_count
            ib = ctx.buffer(adjusted.astype('u4').tobytes())
            vao = ctx.vertex_array(program, [(vertex_buffer, '2f 2f 4f1', 'position', 'texcoord', 'color')],
                                   index_buffer=ib, index_element_size=4)
            ctx.scissor = (x1, height - y2, x2 - x1, y2 - y1)
            textures[identifier].use(0)
            vao.render(moderngl.TRIANGLES)
            draw_calls += 1
            vao.release(); ib.release()
        if vertex_buffer: vertex_buffer.release()
    assert r.file.read(1) == b'', 'Unexpected bytes in draw stream'
    pixels = np.frombuffer(framebuffer.read(components=4, alignment=1), dtype='u1').reshape(height, width, 4)[::-1]
    assert draw_calls > 0 and pixels[:, :, :3].std() > 5, 'Empty native capture'
    output.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(pixels).convert('RGB').save(output)
    ctx.scissor = None
    framebuffer.release(); target.release(); program.release()
    for texture in textures.values(): texture.release()
    r.file.close()
    print(f'{output}: {width}x{height}, {draw_calls} native draw calls')
    return {'width': width, 'height': height, 'draw_calls': draw_calls}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path, help='Native .draw file or directory')
    p.add_argument('--output', type=Path, default=Path('docs/images/md3'))
    a = p.parse_args()
    context = moderngl.create_standalone_context(backend='egl')
    paths = sorted(a.source.glob('*.draw')) if a.source.is_dir() else [a.source]
    assert paths, 'No native draw captures were supplied'
    for path in paths: render(context, path, a.output / (path.stem + '.png'))
    print('Renderer:', context.info['GL_RENDERER'])
if __name__ == '__main__': main()
