#!/usr/bin/env python3
# Generate an assembly blob (.incbin) + a C++ lookup table embedding Live2D
# assets into the binary. Two sources:
#   --shaders <dir>  : Cubism GL shader files, keyed by BASENAME (the renderer
#                      requests them by name, sometimes with a dir prefix).
#   --spv     <dir>  : compiled Vulkan SPIR-V (*.spv), keyed by BASENAME (the
#                      renderer requests "FrameworkShaders/<name>.spv").
#   --model   <dir>  : a model directory, keyed by path RELATIVE to it (exactly
#                      the names model3.json references).
# moc3 etc. are 64-byte aligned (Cubism Core requires it for csmReviveMocInPlace).
#
# Usage: embed_live2d_model.py <out.S> <out.cpp> [--shaders <dir>] [--spv <dir>] [--model <dir>]
import os, sys

def collect(root, key_mode):
    out = []
    if not root or not os.path.isdir(root):
        return out
    root = os.path.abspath(root)
    for r, _, names in os.walk(root):
        for n in sorted(names):
            ab = os.path.join(r, n)
            if key_mode == "basename":
                key = n
            else:  # relpath
                key = os.path.relpath(ab, root).replace(os.sep, "/")
            out.append((key, ab))
    return out

def main():
    out_s, out_cpp = sys.argv[1], sys.argv[2]
    shaders_dir = spv_dir = model_dir = voice_dir = None
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--shaders": shaders_dir = sys.argv[i+1]; i += 2
        elif sys.argv[i] == "--spv": spv_dir = sys.argv[i+1]; i += 2
        elif sys.argv[i] == "--model": model_dir = sys.argv[i+1]; i += 2
        elif sys.argv[i] == "--voice": voice_dir = sys.argv[i+1]; i += 2
        else: i += 1

    files = (collect(shaders_dir, "basename") + collect(spv_dir, "basename")
             + collect(voice_dir, "basename") + collect(model_dir, "relpath"))
    # de-dup keys (first wins), keep order
    seen, uniq = set(), []
    for key, ab in files:
        if key in seen: continue
        seen.add(key); uniq.append((key, ab))
    files = uniq

    with open(out_s, "w") as s:
        s.write('// generated; do not edit\n    .section .rodata\n')
        for i, (_k, ab) in enumerate(files):
            s.write('    .balign 64\n')
            s.write('    .global l2d_%d_start\n    .global l2d_%d_end\n' % (i, i))
            s.write('l2d_%d_start:\n    .incbin "%s"\nl2d_%d_end:\n'
                    % (i, ab.replace('"', '\\"'), i))

    def cstr(x):
        return '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"'

    with open(out_cpp, "w") as c:
        c.write('// generated; do not edit\n#include "live2d/live2d_embedded.h"\n#include <cstring>\n\n')
        c.write('extern "C" {\n')
        for i in range(len(files)):
            c.write('extern const unsigned char l2d_%d_start[];\nextern const unsigned char l2d_%d_end[];\n' % (i, i))
        c.write('}\n\nnamespace aimgui { namespace live2d {\nnamespace {\n')
        c.write('struct E { const char* key; const unsigned char* data; const unsigned char* end; };\n')
        c.write('const E kFiles[] = {\n')
        for i, (k, _ab) in enumerate(files):
            c.write('    { %s, l2d_%d_start, l2d_%d_end },\n' % (cstr(k), i, i))
        c.write('    { nullptr, nullptr, nullptr }\n};\n} // namespace\n\n')
        c.write('const unsigned char* EmbeddedGet(const char* key, unsigned* outSize) {\n')
        c.write('    for (const E* f = kFiles; f->key; ++f) if (std::strcmp(f->key, key) == 0) {\n')
        c.write('        if (outSize) *outSize = (unsigned)(f->end - f->data);\n        return f->data;\n    }\n    return nullptr;\n}\n\n')
        c.write('const char* EmbeddedFindModel3() {\n    const unsigned sl = 12; // ".model3.json"\n')
        c.write('    for (const E* f = kFiles; f->key; ++f) {\n')
        c.write('        unsigned n = (unsigned)std::strlen(f->key);\n')
        c.write('        if (n >= sl && std::strcmp(f->key + n - sl, ".model3.json") == 0) return f->key;\n')
        c.write('    }\n    return nullptr;\n}\n\n} }\n')

    print("embedded %d files (shaders+model)" % len(files))

if __name__ == "__main__":
    main()
