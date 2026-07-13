#!/usr/bin/env python3
# Generate an assembly blob (.incbin) + a C++ lookup table that embeds every
# file of a Live2D model directory into the binary, keyed by its path relative
# to the model dir (exactly the names model3.json references). moc3 data is
# 64-byte aligned as Cubism Core's csmReviveMocInPlace requires.
#
# Usage: embed_live2d_model.py <model_dir> <out.S> <out.cpp>
import os, sys

def main():
    model_dir, out_s, out_cpp = sys.argv[1], sys.argv[2], sys.argv[3]
    model_dir = os.path.abspath(model_dir)

    files = []
    for root, _, names in os.walk(model_dir):
        for n in sorted(names):
            ab = os.path.join(root, n)
            rel = os.path.relpath(ab, model_dir).replace(os.sep, "/")
            files.append((rel, ab))
    files.sort()

    with open(out_s, "w") as s:
        s.write('// generated; do not edit\n')
        s.write('    .section .rodata\n')
        for i, (_rel, ab) in enumerate(files):
            s.write('    .balign 64\n')
            s.write('    .global l2d_%d_start\n' % i)
            s.write('    .global l2d_%d_end\n' % i)
            s.write('l2d_%d_start:\n' % i)
            s.write('    .incbin "%s"\n' % ab.replace('"', '\\"'))
            s.write('l2d_%d_end:\n' % i)

    def cstr(x):
        return '"' + x.replace('\\', '\\\\').replace('"', '\\"') + '"'

    with open(out_cpp, "w") as c:
        c.write('// generated; do not edit\n')
        c.write('#include "live2d/live2d_embedded.h"\n#include <cstring>\n\n')
        c.write('extern "C" {\n')
        for i in range(len(files)):
            c.write('extern const unsigned char l2d_%d_start[];\n' % i)
            c.write('extern const unsigned char l2d_%d_end[];\n' % i)
        c.write('}\n\n')
        c.write('namespace aimgui { namespace live2d {\n')
        c.write('namespace {\nstruct E { const char* path; const unsigned char* data; const unsigned char* end; };\n')
        c.write('const E kFiles[] = {\n')
        for i, (rel, _ab) in enumerate(files):
            c.write('    { %s, l2d_%d_start, l2d_%d_end },\n' % (cstr(rel), i, i))
        c.write('};\n} // namespace\n\n')
        c.write('const unsigned char* EmbeddedGet(const char* relpath, unsigned* outSize) {\n')
        c.write('    for (const auto& f : kFiles) if (std::strcmp(f.path, relpath) == 0) {\n')
        c.write('        if (outSize) *outSize = (unsigned)(f.end - f.data);\n')
        c.write('        return f.data;\n    }\n    return nullptr;\n}\n\n')
        c.write('const char* EmbeddedFindModel3() {\n')
        c.write('    const char* suffix = ".model3.json";\n    const unsigned sl = 12;\n')
        c.write('    for (const auto& f : kFiles) {\n')
        c.write('        unsigned n = (unsigned)std::strlen(f.path);\n')
        c.write('        if (n >= sl && std::strcmp(f.path + n - sl, suffix) == 0) return f.path;\n')
        c.write('    }\n    return nullptr;\n}\n\n')
        c.write('} }\n')

    print("embedded %d files from %s" % (len(files), model_dir))

if __name__ == "__main__":
    main()
