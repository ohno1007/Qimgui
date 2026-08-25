#version 450 core

layout(location = 0) in  vec2 vPx;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D uScreen;

layout(push_constant) uniform Push {
    vec4 screen;
    vec4 params;
    vec4 tint;
    vec4 params2;
    vec4 shapes[4];
} pc;

const vec2  kShadowOffset = vec2(0.0, 12.0);
const float kShadowSoft   = 28.0;
const float kShadowAlpha  = 0.30;
const float kContourAlpha = 0.12;

const float kCentreBlur = 0.35;

float sdRoundedBox(vec2 p, vec2 half_, float r) {
    r = min(r, min(half_.x, half_.y));
    vec2 q = abs(p) - half_ + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float smin(float a, float b, float k) {
    if (k <= 0.0) return min(a, b);
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float sceneSDF(vec2 p) {
    float d = sdRoundedBox(p - pc.shapes[0].xy, pc.shapes[0].zw, pc.params.x);
    for (int i = 1; i < 4; ++i) {
        if (pc.shapes[i].z <= 0.0) continue;
        d = smin(d, sdRoundedBox(p - pc.shapes[i].xy, pc.shapes[i].zw, pc.params.x),
                 pc.params2.w);
    }
    return d;
}

vec3 toLinear(vec3 c) { return c * c; }
vec3 toSrgb(vec3 c)   { return sqrt(max(c, vec3(0.0))); }

vec3 sampleBlurred(vec2 px, vec2 screen, float radius) {
    vec3 acc = toLinear(texture(uScreen, px / screen).rgb);
    float wsum = 1.0;
    for (int i = 0; i < 6; ++i) {
        float a = float(i) * 1.0471975;
        vec2  d1 = vec2(cos(a), sin(a)) * radius;
        vec2  d2 = vec2(cos(a + 0.5236), sin(a + 0.5236)) * radius * 0.55;
        acc += toLinear(texture(uScreen, (px + d1) / screen).rgb) * 0.55;
        acc += toLinear(texture(uScreen, (px + d2) / screen).rgb) * 0.85;
        wsum += 1.40;
    }
    return acc / wsum;
}

vec3 sampleEnv(vec2 px, vec2 screen, float radius) {
    vec3 acc = toLinear(texture(uScreen, px / screen).rgb);
    for (int i = 0; i < 4; ++i) {
        float a = float(i) * 1.5707963;
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

    float ds      = sceneSDF(posPx - kShadowOffset);
    float shadowA = (1.0 - smoothstep(0.0, kShadowSoft, ds)) * kShadowAlpha
                  + (1.0 - smoothstep(0.0, 1.5, d)) * kContourAlpha;
    shadowA *= alpha;

    if (d > 0.0) {
        fragColor = vec4(0.0, 0.0, 0.0, shadowA);
        return;
    }

    const float e = 1.0;
    vec2 n = normalize(vec2(
        sceneSDF(posPx + vec2(e, 0.0)) - sceneSDF(posPx - vec2(e, 0.0)),
        sceneSDF(posPx + vec2(0.0, e)) - sceneSDF(posPx - vec2(0.0, e))) + 1e-6);

    float t     = clamp(1.0 + d / edgeW, 0.0, 1.0);
    float bevel = t * t * (3.0 - 2.0 * t) * t;
    float bend  = bevel * edgeW * bendK;

    vec2 base = posPx + n * bend;

    float rimness = smoothstep(-edgeW * 2.5, 0.0, d);
    float blurPx  = pc.params2.x * mix(kCentreBlur, 1.0, rimness);

    float cornerness = min(abs(n.x), abs(n.y)) * 2.0;
    float disp = bevel * bend * 0.16 * mix(1.0, 1.7, cornerness);
    vec3 lin = vec3(
        sampleBlurred(base - n * disp, pc.screen.xy, blurPx).r,
        sampleBlurred(base,            pc.screen.xy, blurPx).g,
        sampleBlurred(base + n * disp, pc.screen.xy, blurPx).b);

    vec3 col = toSrgb(lin);

    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col *= mix(1.0, 0.65, smoothstep(0.55, 0.95, luma));

    col = mix(col, pc.tint.rgb, pc.tint.a);

    float caustic = smoothstep(0.72, 0.97, bevel) * (1.0 - smoothstep(0.97, 1.0, bevel));
    col += caustic * 0.20;

    float edgeLine = smoothstep(-2.5, -0.8, d) * (1.0 - smoothstep(-0.8, 0.0, d));
    col += edgeLine * 0.14;

    vec2  lightDir = normalize(vec2(pc.params2.y, pc.params2.z) + 1e-6);
    float key      = pow(max(dot(n, lightDir), 0.0), 3.0);
    col += key * bevel * 0.18;

    vec3  envCol  = toSrgb(sampleEnv(posPx + n * edgeW * 2.2, pc.screen.xy, edgeW * 0.8));
    float envLuma = dot(envCol, vec3(0.2126, 0.7152, 0.0722));

    float envAmt  = smoothstep(0.45, 1.0, envLuma);

    vec3  envHue  = clamp(envCol / max(envLuma, 1e-3), vec3(0.0), vec3(1.6));
    col += mix(vec3(1.0), envHue, 0.55) * envAmt * bevel * 0.26;

    float aa     = 1.0 - smoothstep(-1.5, 0.0, d);
    float glassA = alpha * aa;
    float outA   = glassA + shadowA * (1.0 - glassA);
    fragColor = vec4(col * glassA / max(outA, 1e-4), outA);
}
