#!/usr/bin/env python3
"""Render the production GLES shaders using Mesa; assert orientation and edges."""
import argparse
import re
import subprocess
from pathlib import Path
import moderngl
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT/'jni/src/core/shaders'
W, H = 960, 540

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--compare-ref', help='Optional repository ref for before/after preview')
    args = parser.parse_args()
    ctx = moderngl.create_standalone_context(backend='egl')
    common = (SRC/'glass_common.glsl').read_text()
    vertex = (SRC/'glass_gl.vert').read_text()
    fragment = (SRC/'glass_gl.frag').read_text().replace('#include "glass_common.glsl"', common)
    yy, xx = np.mgrid[0:H, 0:W]
    checker = ((xx//20 + yy//20) % 2).astype(float)
    backdrop = np.stack([0.12 + xx/W*0.50 + checker*0.12,
                         0.10 + yy/H*0.55 + checker*0.12,
                         0.65 - yy/H*0.38 + checker*0.12], axis=-1).astype('f4')
    shapes = np.zeros((4,4), dtype='f4')
    shapes[0] = [W*0.5, 190, 330, 135]

    def render(vs, fs, flipped=False, shape_data=shapes):
        program = ctx.program(vertex_shader=vs, fragment_shader=fs)
        source = ctx.texture((W,H), 3, (backdrop[::-1].copy() if flipped else backdrop).tobytes(), dtype='f4')
        source.filter = (moderngl.LINEAR, moderngl.LINEAR)
        source.repeat_x = source.repeat_y = False
        target = ctx.texture((W,H), 4, dtype='f4')
        fbo = ctx.framebuffer(color_attachments=[target])
        fbo.use(); fbo.clear()
        source.use(0)
        uniforms = {'uScreenTex': 0, 'uScreenSize': (W,H), 'uScreen': (W,H),
                    'uSurface': (W,H), 'uParams': (24,40,0.65,1),
                    'uTint': (0,0,0,0.11), 'uParams2': (6,-0.6,-0.8,0),
                    'uFlipY': flipped}
        for name, value in uniforms.items():
            if name in program: program[name].value = value
        program['uShapes'].write(shape_data.tobytes())
        vao = ctx.vertex_array(program, [])
        vao.render(mode=moderngl.TRIANGLE_STRIP, vertices=4)
        pixels = np.frombuffer(fbo.read(components=4,dtype='f4'),dtype='f4').reshape(H,W,4)[::-1].copy()
        vao.release(); fbo.release(); target.release(); source.release(); program.release()
        return pixels

    glass = render(vertex,fragment)
    assert np.isfinite(glass).all(), 'NaN/Inf in glass output'
    assert glass[190,480,3] > 0.999, 'panel not at its intended top-down coordinates'
    assert glass[480,480,3] == 0, 'panel/shadow incorrectly mirrored vertically'
    assert 0 < glass[190,150,3] < 1, 'edge is not antialiased'
    difference = np.max(np.abs(glass-render(vertex,fragment,True)))
    print('Framebuffer orientation difference:', difference)
    assert difference < 1.0/255, 'framebuffer sampling has a vertical flip'
    # Screen-edge clamping: refraction stays valid even when the panel extends off-screen.
    edge_shapes = shapes.copy(); edge_shapes[0] = [30,40,200,150]
    edge = render(vertex,fragment,shape_data=edge_shapes)
    assert np.isfinite(edge).all() and edge[20,20,3] > 0.99

    if args.output:
        def composite(pixels):
            alpha = pixels[:,:,3:4]
            return Image.fromarray(np.uint8(np.clip(pixels[:,:,:3]*alpha+backdrop*(1-alpha),0,1)*255))
        panels = [('After - shared production GLES shader', composite(glass))]
        if args.compare_ref:
            old = subprocess.check_output(['git','show',f'{args.compare_ref}:jni/src/core/glass_gl.cpp'],cwd=ROOT,text=True)
            vs = re.search(r'const char\* kVS = R"\((.*?)\)";',old,re.S)[1]
            fs = re.search(r'const char\* kFS = R"\((.*?)\)";',old,re.S)[1]
            # Align the comparison geometry; orientation itself is asserted above.
            vs = vs.replace('vec4(ndc, 0.0, 1.0)', 'vec4(ndc.x, -ndc.y, 0.0, 1.0)')
            panels.insert(0, ('Before - same material/coordinates', composite(render(vs,fs))))
        canvas = Image.new('RGB',(W,len(panels)*(H+40)), '#14181e')
        draw = ImageDraw.Draw(canvas)
        for i,(label,panel) in enumerate(panels):
            y=i*(H+40); canvas.paste(panel,(0,y+40)); draw.text((18,y+12),label,fill='white')
        args.output.parent.mkdir(parents=True,exist_ok=True); canvas.save(args.output)
    print('PASS: GLES compilation, finite pixels, edge coverage, top-down placement, framebuffer sampling and edge clamping')
    print(ctx.info['GL_RENDERER'])

if __name__ == '__main__': main()
