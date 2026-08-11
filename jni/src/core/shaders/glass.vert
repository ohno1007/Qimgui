#version 450 core

// One quad covering every pane in the group, because the fragment stage merges
// their distance fields and so has to see all of them at once — a pane cannot
// be drawn on its own without losing the neck it shares with its neighbours.

layout(location = 0) out vec2 vPx;      // this vertex in screen px

layout(push_constant) uniform Push {
    vec4 screen;    // xy = display size px (UV), zw = surface size px (NDC)
    vec4 params;
    vec4 tint;
    vec4 params2;   // x = blur px, yz = key light, w = merge radius px
    vec4 shapes[4];
} pc;

// Grown past the group so the fragment stage has somewhere to draw the shadow.
// Must cover the shadow's offset plus its softness — see the matching constants
// at the top of glass.frag.
const float kPad = 48.0;

void main() {
    // Bounds are derived here rather than pushed: the union of the shapes is
    // already implied by them, and the vec4 that used to carry it is what pays
    // for the fourth shape inside the 128 bytes Vulkan guarantees.
    vec2 lo = pc.shapes[0].xy - pc.shapes[0].zw;
    vec2 hi = pc.shapes[0].xy + pc.shapes[0].zw;
    for (int i = 1; i < 4; ++i) {
        if (pc.shapes[i].z <= 0.0) continue;
        lo = min(lo, pc.shapes[i].xy - pc.shapes[i].zw);
        hi = max(hi, pc.shapes[i].xy + pc.shapes[i].zw);
    }
    // The smooth union bulges outside the plain union near a join, so the merge
    // radius is added on every side as well as the shadow's padding.
    float grow = kPad + max(pc.params2.w, 0.0);
    lo -= grow;
    hi += grow;

    // 0,0 / 1,0 / 0,1 / 1,1 from the vertex index — no vertex buffer needed.
    vec2 uv = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    // Screen px rather than a 0..1 local coordinate: the merged field is
    // defined in screen space, and with the padding the two do not line up.
    vPx = mix(lo, hi, uv);
    // NDC is relative to the render target, which is the square surface —
    // max(w,h) on a side so rotation needs no rebuild — not the visible
    // display. Dividing by the display here magnifies the pane by the ratio
    // between the two.
    vec2 ndc = vPx / pc.screen.zw * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
