#version 450 core

layout(location = 0) out vec2 vPx;

layout(push_constant) uniform Push {
    vec4 screen;
    vec4 params;
    vec4 tint;
    vec4 params2;
    vec4 shapes[4];
} pc;

const float kPad = 48.0;

void main() {

    vec2 lo = pc.shapes[0].xy - abs(pc.shapes[0].zw);
    vec2 hi = pc.shapes[0].xy + abs(pc.shapes[0].zw);
    for (int i = 1; i < 4; ++i) {
        if (pc.shapes[i].z <= 0.0) continue;
        lo = min(lo, pc.shapes[i].xy - abs(pc.shapes[i].zw));
        hi = max(hi, pc.shapes[i].xy + abs(pc.shapes[i].zw));
    }

    float grow = kPad + max(pc.params2.w, 0.0);
    lo -= grow;
    hi += grow;

    vec2 uv = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));

    vPx = mix(lo, hi, uv);

    vec2 ndc = vPx / pc.screen.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
