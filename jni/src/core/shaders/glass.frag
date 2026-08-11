#version 450 core

// Liquid glass: the pane refracts the live screen behind it.
//
// Apple's description of the material is the specification worth following —
// earlier "frosted" materials scattered light, whereas this one bends and
// *concentrates* it. So the middle stays genuinely clear rather than blurred,
// and the pane announces itself entirely at the rim: by lensing, by the bright
// caustic where bent rays pile up, by colour splitting as glass disperses it,
// and by a specular highlight along the lit edge.
//
// Everything is evaluated per-pixel here. The previous version approximated
// this by displacing the UVs of a tessellated quad on the CPU, which cannot
// express dispersion (each channel needs its own bend) and quantises the
// lensing to the grid exactly where it varies fastest — at the border.

layout(location = 0) in  vec2 vUV;      // position within the pane, 0..1
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D uScreen;

layout(push_constant) uniform Push {
    vec4 rect;      // pane in screen px: xy = min, zw = size
    vec4 screen;    // xy = display size px (UV), zw = surface size px (NDC)
    vec4 params;    // x = rounding px, y = edge width px, z = bend, w = alpha
    vec4 tint;      // rgb = wash colour, a = wash strength
} pc;

// Signed distance to a rounded box centred on the origin. Negative inside.
float sdRoundedBox(vec2 p, vec2 half_, float r) {
    vec2 q = abs(p) - half_ + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 size    = pc.rect.zw;
    vec2 halfSz  = size * 0.5;
    float round_ = pc.params.x;
    float edgeW  = max(pc.params.y, 1.0);
    float bendK  = pc.params.z;
    float alpha  = pc.params.w;

    vec2 posPx = pc.rect.xy + vUV * size;   // this pixel, in screen space
    vec2 rel   = posPx - (pc.rect.xy + halfSz);

    float d = sdRoundedBox(rel, halfSz, round_);
    if (d > 0.0) { fragColor = vec4(0.0); return; }   // outside the rounded corners

    // Gradient of the distance field is the glass surface normal here.
    const float e = 1.0;
    vec2 n = normalize(vec2(
        sdRoundedBox(rel + vec2(e, 0.0), halfSz, round_) - sdRoundedBox(rel - vec2(e, 0.0), halfSz, round_),
        sdRoundedBox(rel + vec2(0.0, e), halfSz, round_) - sdRoundedBox(rel - vec2(0.0, e), halfSz, round_)) + 1e-6);

    // 0 deep inside, 1 at the rim. The bevel term keeps the surface near-flat
    // until it turns over hard at the border, which is what makes the edge read
    // as a lens rather than a smear.
    float t     = clamp(1.0 + d / edgeW, 0.0, 1.0);
    float bevel = t * t * (3.0 - 2.0 * t) * t;
    float bend  = bevel * edgeW * bendK;

    // Sample from further out along the normal: the rim drags in and compresses
    // what lies just outside the pane. No overall magnification — the middle of
    // the pane shows what is actually behind it, undistorted.
    vec2 base = posPx + n * bend;

    // Dispersion: glass bends short wavelengths more than long ones, so each
    // channel is sampled at its own bend. The split is only perceptible at the
    // rim, which is precisely where it does the work of identifying glass.
    float disp = bevel * bend * 0.16;
    vec2 uvR = (base - n * disp)       / pc.screen.xy;
    vec2 uvG = base                    / pc.screen.xy;
    vec2 uvB = (base + n * disp)       / pc.screen.xy;

    vec3 col = vec3(
        texture(uScreen, uvR).r,
        texture(uScreen, uvG).g,
        texture(uScreen, uvB).b);

    // Caustic: bent rays pile up just inside the rim and light concentrates
    // into a bright band. This is the "concentrates light" half of the
    // material, and the strongest cue that the edge has thickness.
    float caustic = smoothstep(0.72, 0.97, bevel) * (1.0 - smoothstep(0.97, 1.0, bevel));
    col += caustic * 0.55;

    // Specular: brightest where the surface tilts towards the light, taken as
    // up-and-left, falling off around the rim.
    const vec2 lightDir = normalize(vec2(-0.6, -0.8));
    float spec = max(dot(n, lightDir), 0.0);
    col += pow(spec, 3.0) * bevel * 0.42;


    // Legibility, per pixel. Rather than flipping the whole palette from an
    // average — which is the wrong granularity, and leaves text unreadable on
    // whichever half of the window disagrees with the average — squeeze bright
    // areas down locally. Dark regions are left alone, so the material stays
    // clear over them, and one text colour then works everywhere.
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col *= mix(1.0, 0.40, smoothstep(0.30, 0.80, luma));

    // A whisper of wash, never enough to read as a tinted panel.
    col = mix(col, pc.tint.rgb, pc.tint.a);

    // Feather the last pixel so the rounded border stays smooth.
    float aa = 1.0 - smoothstep(-1.5, 0.0, d);
    fragColor = vec4(col, alpha * aa);
}
