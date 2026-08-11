#version 450 core

// Full-pane triangle strip: four corners of the glass rect, so the fragment
// shader receives 0..1 across the pane and can work in its own local space
// regardless of where on screen the pane sits.

layout(location = 0) out vec2 vUV;

layout(push_constant) uniform Push {
    vec4 rect;      // pane in screen px: xy = min, zw = size
    vec4 screen;    // xy = display size px (UV), zw = surface size px (NDC)
    vec4 params;
    vec4 tint;
} pc;

void main() {
    // 0,0 / 1,0 / 0,1 / 1,1 from the vertex index — no vertex buffer needed.
    vUV = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    vec2 px  = pc.rect.xy + vUV * pc.rect.zw;
    // NDC is relative to the render target, which is the square surface —
    // max(w,h) on a side so rotation needs no rebuild — not the visible
    // display. Dividing by the display here magnifies the pane by the ratio
    // between the two.
    vec2 ndc = px / pc.screen.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
