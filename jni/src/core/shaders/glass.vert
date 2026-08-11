#version 450 core

// One quad covering every pane in the group, because the fragment stage merges
// their distance fields and so has to see all of them at once — a pane cannot
// be drawn on its own without losing the neck it shares with its neighbours.

layout(location = 0) out vec2 vPx;      // this vertex in screen px

layout(push_constant) uniform Push {
    vec4 bounds;    // quad bounds in screen px: xy = min, zw = size
    vec4 screen;    // xy = display size px (UV), zw = surface size px (NDC)
    vec4 params;
    vec4 tint;
    vec4 params2;
    vec4 shapes[3];
} pc;

// The quad is grown past the group so the fragment stage has somewhere to draw
// the shadow. Must cover the shadow's offset plus its softness — see the
// matching constants at the top of glass.frag. The merge radius is already
// accounted for in bounds by the caller.
const float kPad = 48.0;

void main() {
    // 0,0 / 1,0 / 0,1 / 1,1 from the vertex index — no vertex buffer needed.
    vec2 uv = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    // Screen px rather than a 0..1 local coordinate: with the padding the two
    // no longer line up, and the merged field is defined in screen space.
    vPx = pc.bounds.xy - kPad + uv * (pc.bounds.zw + 2.0 * kPad);
    // NDC is relative to the render target, which is the square surface —
    // max(w,h) on a side so rotation needs no rebuild — not the visible
    // display. Dividing by the display here magnifies the pane by the ratio
    // between the two.
    vec2 ndc = vPx / pc.screen.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
