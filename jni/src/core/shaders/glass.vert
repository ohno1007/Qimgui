#version 450 core

// Full-pane triangle strip: four corners of the glass rect, so the fragment
// shader receives 0..1 across the pane and can work in its own local space
// regardless of where on screen the pane sits.

layout(location = 0) out vec2 vPx;      // this vertex in screen px

layout(push_constant) uniform Push {
    vec4 rect;      // pane in screen px: xy = min, zw = size
    vec4 screen;    // xy = display size px (UV), zw = surface size px (NDC)
    vec4 params;
    vec4 tint;
    vec4 params2;
} pc;

// The quad is grown past the pane so the fragment stage has somewhere to draw
// the shadow. Must cover the shadow's offset plus its softness — see the
// matching constants at the top of glass.frag.
const float kPad = 48.0;

void main() {
    // 0,0 / 1,0 / 0,1 / 1,1 from the vertex index — no vertex buffer needed.
    vec2 uv = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    // Screen px rather than a 0..1 pane coordinate, because with the padding
    // the two no longer line up and every consumer downstream wants px.
    vPx = pc.rect.xy - kPad + uv * (pc.rect.zw + 2.0 * kPad);
    // NDC is relative to the render target, which is the square surface —
    // max(w,h) on a side so rotation needs no rebuild — not the visible
    // display. Dividing by the display here magnifies the pane by the ratio
    // between the two.
    vec2 ndc = vPx / pc.screen.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
