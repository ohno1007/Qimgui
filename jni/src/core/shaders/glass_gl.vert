#version 300 es
precision highp float;
out vec2 vPx;
uniform vec2 uSurface;
uniform vec4 uShapes[4];
uniform vec4 uParams2;
const float kPad = 48.0;
void main() {

    vec2 lo = uShapes[0].xy - abs(uShapes[0].zw);
    vec2 hi = uShapes[0].xy + abs(uShapes[0].zw);
    for (int i = 1; i < 4; ++i) {
        if (uShapes[i].z <= 0.0) continue;
        lo = min(lo, uShapes[i].xy - abs(uShapes[i].zw));
        hi = max(hi, uShapes[i].xy + abs(uShapes[i].zw));
    }

    float grow = kPad + max(uParams2.w, 0.0);
    lo -= grow; hi += grow;
    vec2 uv = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vPx = mix(lo, hi, uv);

    vec2 ndc = vPx / uSurface * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
