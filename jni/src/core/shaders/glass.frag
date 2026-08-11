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

layout(location = 0) in  vec2 vPx;      // this pixel in screen px
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D uScreen;

layout(push_constant) uniform Push {
    vec4 screen;    // xy = display size px (UV), zw = surface size px (NDC)
    vec4 params;    // x = rounding px, y = edge width px, z = bend, w = alpha
    vec4 tint;      // rgb = wash colour, a = wash strength
    vec4 params2;   // x = blur px, yz = key light direction, w = merge radius px
    vec4 shapes[4]; // xy = centre px, zw = half size px; z <= 0 means unused
} pc;

// Shadow. Whatever these add up to must stay inside glass.vert's kPad, or the
// shadow is clipped by the quad it is drawn on.
const vec2  kShadowOffset = vec2(0.0, 12.0);
const float kShadowSoft   = 28.0;
const float kShadowAlpha  = 0.30;
const float kContourAlpha = 0.12;

// How much of the rim's blur the middle of the pane gets. Not zero: the middle
// is where text sits, and a perfectly clear centre puts UI type straight onto
// whatever photo happens to be behind the window. This is the legibility knob
// that the material's own description argues against having at all.
const float kCentreBlur = 0.35;

// Signed distance to a rounded box centred on the origin. Negative inside.
float sdRoundedBox(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 q = abs(p) - half_ + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Polynomial smooth minimum. Taking min() of two distance fields welds them
// with a crease; this rounds the join over a radius k, which is what surface
// tension does to two bodies of liquid brought close together. Where the gap
// is wider than k the two are left alone, so the neck thins out and lets go on
// its own rather than being switched off.
float smin(float a, float b, float k) {
    if (k <= 0.0) return min(a, b);
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// The whole group as one field. Panes are merged here rather than drawn one at
// a time because a neck belongs to two panes at once: everything downstream —
// the normal, the bevel, the rim — reads this, so the join is lensed and lit
// like any other part of the surface instead of being a seam between two.
float sceneSDF(vec2 p) {
    float d = sdRoundedBox(p - pc.shapes[0].xy, pc.shapes[0].zw, pc.params.x);
    for (int i = 1; i < 4; ++i) {
        if (pc.shapes[i].z <= 0.0) continue;
        d = smin(d, sdRoundedBox(p - pc.shapes[i].xy, pc.shapes[i].zw, pc.params.x),
                 pc.params2.w);
    }
    return d;
}

// The mirrored screen arrives sRGB-encoded, and averaging encoded values is
// not averaging light: it pulls the mean towards the darker sample, so a blur
// over anything high-contrast comes out muddier and dimmer than the scene it
// came from. That is what made the refracted image read as grey rather than
// bright. Gamma 2.0 instead of the exact sRGB curve — the error is far below
// what is visible here and it costs a multiply instead of a pow.
vec3 toLinear(vec3 c) { return c * c; }
vec3 toSrgb(vec3 c)   { return sqrt(max(c, vec3(0.0))); }

// Refracted content, softened, accumulated in linear light. Glass this thick
// does not transmit a sharp image, and more practically: UI text sits on
// whatever happens to be behind the window, and a busy photo underneath makes
// it unreadable no matter how the contrast is tuned. Blurring the transmitted
// image is what buys back the legibility, and it is the one place a blur
// belongs in this material — the rim still bends and concentrates light rather
// than scattering it.
//
// Twelve taps on two rings, offset from each other so the pattern does not
// show up as spokes. The mirror is already half resolution and the sampler is
// linear, so each tap is doing more work than its count suggests.
vec3 sampleBlurred(vec2 px, vec2 screen, float radius) {
    vec3 acc = toLinear(texture(uScreen, px / screen).rgb);
    float wsum = 1.0;
    for (int i = 0; i < 6; ++i) {
        float a = float(i) * 1.0471975;             // 60 degrees
        vec2  d1 = vec2(cos(a), sin(a)) * radius;
        vec2  d2 = vec2(cos(a + 0.5236), sin(a + 0.5236)) * radius * 0.55;
        acc += toLinear(texture(uScreen, (px + d1) / screen).rgb) * 0.55;
        acc += toLinear(texture(uScreen, (px + d2) / screen).rgb) * 0.85;
        wsum += 1.40;
    }
    return acc / wsum;
}

// A cheap, wide read of what surrounds a point, in linear light. This feeds the
// rim reflection, where the only question is how bright it is over there — not
// what the detail is — so four taps on a ring is plenty and a twelve-tap blur
// would be paying for resolution nobody sees.
vec3 sampleEnv(vec2 px, vec2 screen, float radius) {
    vec3 acc = toLinear(texture(uScreen, px / screen).rgb);
    for (int i = 0; i < 4; ++i) {
        float a = float(i) * 1.5707963;             // 90 degrees
        vec2  o = vec2(cos(a), sin(a)) * radius;
        acc += toLinear(texture(uScreen, (px + o) / screen).rgb);
    }
    return acc * 0.2;
}

void main() {
    float edgeW  = max(pc.params.y, 1.0);
    float bendK  = pc.params.z;
    float alpha  = pc.params.w;

    vec2 posPx = vPx;

    float d = sceneSDF(posPx);

    // Shadow, taken from the same distance field shifted down, plus a tight
    // dark contour hugging the rim. Without anything outside it the pane is a
    // hole cut in the screen rather than a sheet lying on it; this is the only
    // cue that gives the glass a height above the desktop.
    float ds      = sceneSDF(posPx - kShadowOffset);
    float shadowA = (1.0 - smoothstep(0.0, kShadowSoft, ds)) * kShadowAlpha
                  + (1.0 - smoothstep(0.0, 1.5, d)) * kContourAlpha;
    shadowA *= alpha;

    if (d > 0.0) {   // outside the pane: shadow only
        fragColor = vec4(0.0, 0.0, 0.0, shadowA);
        return;
    }

    // Gradient of the merged distance field is the glass surface normal here,
    // so the neck between two panes gets a normal that curves smoothly from one
    // into the other — which is the whole reason the merge happens in the field
    // rather than by drawing the panes over each other.
    const float e = 1.0;
    vec2 n = normalize(vec2(
        sceneSDF(posPx + vec2(e, 0.0)) - sceneSDF(posPx - vec2(e, 0.0)),
        sceneSDF(posPx + vec2(0.0, e)) - sceneSDF(posPx - vec2(0.0, e))) + 1e-6);

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

    // Blur only where the glass is doing something. A constant radius across
    // the whole pane is a frosted sheet with a nice edge; the material this is
    // after is clear in the middle and turns over at the rim, so the softening
    // has to follow the same profile the lensing does. The ramp is much wider
    // than the bevel so it reads as a gradient rather than a ring.
    float rimness = smoothstep(-edgeW * 2.5, 0.0, d);
    float blurPx  = pc.params2.x * mix(kCentreBlur, 1.0, rimness);

    // Dispersion: glass bends short wavelengths more than long ones, so each
    // channel is sampled at its own bend. The split is only perceptible at the
    // rim, which is precisely where it does the work of identifying glass.
    // Dispersion rides on top of the blur: each channel is blurred about its
    // own bent position, so the colour split survives the softening.
    // Weighted towards the corners, where the surface actually turns through a
    // sharper angle and a real lens splits hardest. On a straight edge the
    // normal is axis-aligned and this term is 1; at a 45-degree corner it is 0.
    float cornerness = min(abs(n.x), abs(n.y)) * 2.0;
    float disp = bevel * bend * 0.16 * mix(1.0, 1.7, cornerness);
    vec3 lin = vec3(
        sampleBlurred(base - n * disp, pc.screen.xy, blurPx).r,
        sampleBlurred(base,            pc.screen.xy, blurPx).g,
        sampleBlurred(base + n * disp, pc.screen.xy, blurPx).b);
    // Back to display space: everything below was tuned against encoded values
    // and is artistic rather than physical, so it belongs on this side.
    vec3 col = toSrgb(lin);

    // Legibility, per pixel — applied to the refracted background only, before
    // any of the glass's own light is added. Doing it afterwards crushed the
    // caustic and the specular along with everything else, which is what made
    // the rim look grey: the highlight is the material reflecting light, not
    // content to be read through, so it must not be dimmed.
    // Only genuinely bright content gets pulled down, and gently. Reaching
    // further down the range greyed everything — mid-tones included — which
    // both dulled the material and made the rim highlights look blown out
    // against it. Text stays readable at 0.65; below that the picture is
    // being repainted rather than made legible.
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col *= mix(1.0, 0.65, smoothstep(0.55, 0.95, luma));

    // A whisper of wash, never enough to read as a tinted panel.
    col = mix(col, pc.tint.rgb, pc.tint.a);

    // Caustic: bent rays pile up just inside the rim and light concentrates
    // into a bright band. This is the "concentrates light" half of the
    // material, and the strongest cue that the edge has thickness.
    // Kept well short of white: a rim that clips to full brightness stops
    // reading as light concentrated in glass and starts reading as a drawn
    // white border.
    float caustic = smoothstep(0.72, 0.97, bevel) * (1.0 - smoothstep(0.97, 1.0, bevel));
    col += caustic * 0.20;

    // A thin bright stroke right at the border. The caustic sits a little way
    // inside the edge, so without this the pane still ends abruptly — the
    // stroke is the outermost of the three layers a real bevel shows.
    float edgeLine = smoothstep(-2.5, -0.8, d) * (1.0 - smoothstep(-0.8, 0.0, d));
    col += edgeLine * 0.14;

    // Specular, in two parts. The key light gives the rim its shape and comes
    // from wherever the panel is leaning, so the highlight sweeps as the device
    // is tilted — a highlight fixed at one spot reads as painted on, which is
    // what the old constant direction did.
    vec2  lightDir = normalize(vec2(pc.params2.y, pc.params2.z) + 1e-6);
    float key      = pow(max(dot(n, lightDir), 0.0), 3.0);
    col += key * bevel * 0.18;

    // The other part is the surroundings. Look outward along the normal and let
    // whatever is actually out there light up the stretch of rim nearest to it,
    // so the highlight slides when the content behind the pane moves. This is
    // the difference between glass sitting in a scene and glass drawn over one.
    vec3  envCol  = toSrgb(sampleEnv(posPx + n * edgeW * 2.2, pc.screen.xy, edgeW * 0.8));
    float envLuma = dot(envCol, vec3(0.2126, 0.7152, 0.0722));
    // Only genuinely bright surroundings reflect; reaching lower would just
    // smear a muddy average of the desktop around the whole rim.
    float envAmt  = smoothstep(0.45, 1.0, envLuma);
    // Its hue without its brightness, so a blue window reflects as blue rather
    // than as extra brightness that happens to lean blue.
    vec3  envHue  = clamp(envCol / max(envLuma, 1e-3), vec3(0.0), vec3(1.6));
    col += mix(vec3(1.0), envHue, 0.55) * envAmt * bevel * 0.26;

    // Feather the last pixel so the rounded border stays smooth, then lay the
    // glass over its own shadow. Compositing the two here rather than letting
    // the feather fade to nothing is what keeps the border from opening a
    // one-pixel gap straight through to the desktop. The shadow is black, so
    // it contributes no colour of its own.
    float aa     = 1.0 - smoothstep(-1.5, 0.0, d);
    float glassA = alpha * aa;
    float outA   = glassA + shadowA * (1.0 - glassA);
    fragColor = vec4(col * glassA / max(outA, 1e-4), outA);
}
