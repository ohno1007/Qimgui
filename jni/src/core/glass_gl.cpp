#include "glass_gl.h"

#include <android/log.h>

namespace aimgui {
namespace {

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AImGui", __VA_ARGS__)

const char* kVS = R"(#version 300 es
precision highp float;
out vec2 vPx;
uniform vec2 uSurface;
uniform vec4 uShapes[4];
uniform vec4 uParams2;
const float kPad = 48.0;
void main() {

    vec2 lo = uShapes[0].xy - uShapes[0].zw;
    vec2 hi = uShapes[0].xy + uShapes[0].zw;
    for (int i = 1; i < 4; ++i) {
        if (uShapes[i].z <= 0.0) continue;
        lo = min(lo, uShapes[i].xy - uShapes[i].zw);
        hi = max(hi, uShapes[i].xy + uShapes[i].zw);
    }

    float grow = kPad + max(uParams2.w, 0.0);
    lo -= grow; hi += grow;
    vec2 uv = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vPx = mix(lo, hi, uv);

    vec2 ndc = vPx / uSurface * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kFS = R"(#version 300 es
precision highp float;
in  vec2 vPx;
out vec4 fragColor;
uniform sampler2D uScreenTex;
uniform vec2 uScreen;
uniform vec4 uParams;
uniform vec4 uTint;
uniform vec4 uParams2;
uniform vec4 uShapes[4];

const vec2  kShadowOffset = vec2(0.0, 12.0);
const float kShadowSoft   = 28.0;
const float kShadowAlpha  = 0.30;
const float kContourAlpha = 0.12;

const float kCentreBlur = 0.35;

float sdRoundedBox(vec2 p, vec2 halfSz, float r) {
    r = min(r, min(halfSz.x, halfSz.y));
    vec2 q = abs(p) - halfSz + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float smin(float a, float b, float k) {
    if (k <= 0.0) return min(a, b);
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float sceneSDF(vec2 p) {
    float d = sdRoundedBox(p - uShapes[0].xy, uShapes[0].zw, uParams.x);
    for (int i = 1; i < 4; ++i) {
        if (uShapes[i].z <= 0.0) continue;
        d = smin(d, sdRoundedBox(p - uShapes[i].xy, uShapes[i].zw, uParams.x),
                 uParams2.w);
    }
    return d;
}

vec3 toLinear(vec3 c) { return c * c; }
vec3 toSrgb(vec3 c)   { return sqrt(max(c, vec3(0.0))); }

vec3 sampleBlurred(vec2 px, vec2 screen, float radius) {
    vec3 acc = toLinear(texture(uScreenTex, px / screen).rgb);
    float wsum = 1.0;
    for (int i = 0; i < 6; ++i) {
        float a = float(i) * 1.0471975;
        vec2  d1 = vec2(cos(a), sin(a)) * radius;
        vec2  d2 = vec2(cos(a + 0.5236), sin(a + 0.5236)) * radius * 0.55;
        acc += toLinear(texture(uScreenTex, (px + d1) / screen).rgb) * 0.55;
        acc += toLinear(texture(uScreenTex, (px + d2) / screen).rgb) * 0.85;
        wsum += 1.40;
    }
    return acc / wsum;
}

vec3 sampleEnv(vec2 px, vec2 screen, float radius) {
    vec3 acc = toLinear(texture(uScreenTex, px / screen).rgb);
    for (int i = 0; i < 4; ++i) {
        float a = float(i) * 1.5707963;
        vec2  o = vec2(cos(a), sin(a)) * radius;
        acc += toLinear(texture(uScreenTex, (px + o) / screen).rgb);
    }
    return acc * 0.2;
}

void main() {
    float edgeW  = max(uParams.y, 1.0);
    float bendK  = uParams.z;

    vec2 posPx = vPx;

    float d = sceneSDF(posPx);

    float ds      = sceneSDF(posPx - kShadowOffset);
    float shadowA = (1.0 - smoothstep(0.0, kShadowSoft, ds)) * kShadowAlpha
                  + (1.0 - smoothstep(0.0, 1.5, d)) * kContourAlpha;
    shadowA *= uParams.w;

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
    float blurPx  = uParams2.x * mix(kCentreBlur, 1.0, rimness);

    float cornerness = min(abs(n.x), abs(n.y)) * 2.0;
    float disp = bevel * bend * 0.16 * mix(1.0, 1.7, cornerness);
    vec3 lin = vec3(
        sampleBlurred(base - n * disp, uScreen, blurPx).r,
        sampleBlurred(base,            uScreen, blurPx).g,
        sampleBlurred(base + n * disp, uScreen, blurPx).b);

    vec3 col = toSrgb(lin);

    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col *= mix(1.0, 0.65, smoothstep(0.55, 0.95, luma));
    col = mix(col, uTint.rgb, uTint.a);

    float caustic = smoothstep(0.72, 0.97, bevel) * (1.0 - smoothstep(0.97, 1.0, bevel));
    col += caustic * 0.20;

    float edgeLine = smoothstep(-2.5, -0.8, d) * (1.0 - smoothstep(-0.8, 0.0, d));
    col += edgeLine * 0.14;

    vec2  lightDir = normalize(vec2(uParams2.y, uParams2.z) + 1e-6);
    float key      = pow(max(dot(n, lightDir), 0.0), 3.0);
    col += key * bevel * 0.18;

    vec3  envCol  = toSrgb(sampleEnv(posPx + n * edgeW * 2.2, uScreen, edgeW * 0.8));
    float envLuma = dot(envCol, vec3(0.2126, 0.7152, 0.0722));
    float envAmt  = smoothstep(0.45, 1.0, envLuma);
    vec3  envHue  = clamp(envCol / max(envLuma, 1e-3), vec3(0.0), vec3(1.6));
    col += mix(vec3(1.0), envHue, 0.55) * envAmt * bevel * 0.26;

    float aa     = 1.0 - smoothstep(-1.5, 0.0, d);
    float glassA = uParams.w * aa;
    float outA   = glassA + shadowA * (1.0 - glassA);
    fragColor = vec4(col * glassA / max(outA, 1e-4), outA);
}
)";

GLuint Compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("glass shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}

bool GlassGL::Init() {
    GLuint vs = Compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return false; }

    m_Prog = glCreateProgram();
    glAttachShader(m_Prog, vs);
    glAttachShader(m_Prog, fs);
    glLinkProgram(m_Prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(m_Prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_Prog, sizeof(log), nullptr, log);
        LOGE("glass link: %s", log);
        glDeleteProgram(m_Prog);
        m_Prog = 0;
        return false;
    }

    m_LocScreenTex = glGetUniformLocation(m_Prog, "uScreenTex");

    m_LocShapes    = glGetUniformLocation(m_Prog, "uShapes[0]");
    if (m_LocShapes < 0) m_LocShapes = glGetUniformLocation(m_Prog, "uShapes");
    if (m_LocShapes < 0) LOGE("glass: uShapes not found");
    m_LocScreen    = glGetUniformLocation(m_Prog, "uScreen");
    m_LocSurface   = glGetUniformLocation(m_Prog, "uSurface");
    m_LocParams    = glGetUniformLocation(m_Prog, "uParams");
    m_LocTint      = glGetUniformLocation(m_Prog, "uTint");
    m_LocParams2   = glGetUniformLocation(m_Prog, "uParams2");

    glGenVertexArrays(1, &m_VAO);
    m_Ready = true;
    return true;
}

void GlassGL::Shutdown() {
    if (m_VAO)  glDeleteVertexArrays(1, &m_VAO);
    if (m_Prog) glDeleteProgram(m_Prog);
    m_VAO = m_Prog = 0;
    m_Ready = false;
}

void GlassGL::Draw(GLuint screenTex, int screenW, int screenH,
                   int surfaceW, int surfaceH,
                   const GlassRect* rects, int count) {
    if (!m_Ready || !screenTex || count <= 0 || screenW <= 0 || screenH <= 0) return;
    if (surfaceW <= 0 || surfaceH <= 0) return;

    glUseProgram(m_Prog);
    glBindVertexArray(m_VAO);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glDisable(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glUniform1i(m_LocScreenTex, 0);
    glUniform2f(m_LocScreen, (float)screenW, (float)screenH);
    glUniform2f(m_LocSurface, (float)surfaceW, (float)surfaceH);

    for (int gi = 0; gi < kMaxGlassGroups; ++gi) {
        GlassGroup g;
        const GlassRect* lead = nullptr;
        if (!BuildGlassGroup(rects, count, &g, gi, &lead) || !lead) continue;
        const GlassRect& r = *lead;
        glUniform4f(m_LocParams, r.rounding, r.edgeWidth, r.bend, r.alpha);
        glUniform4f(m_LocTint, r.tintR, r.tintG, r.tintB, r.tintA);
        glUniform4f(m_LocParams2, r.blur, r.lightX, r.lightY, r.merge);
        glUniform4fv(m_LocShapes, kMaxMergedShapes, g.shapes);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

}
