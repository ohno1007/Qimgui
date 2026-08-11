#include "glass_gl.h"

#include <android/log.h>

namespace aimgui {
namespace {

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AImGui", __VA_ARGS__)

// Four corners straight from gl_VertexID — no vertex buffer. The quad is grown
// past the pane so the fragment stage has somewhere to draw the shadow; kPad
// must cover the shadow's offset plus its softness.
const char* kVS = R"(#version 300 es
precision highp float;
out vec2 vPx;          // this vertex in screen px
uniform vec4 uRect;    // xy = pane min px, zw = pane size px
uniform vec2 uSurface; // render target size px
const float kPad = 48.0;
void main() {
    vec2 uv = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vPx = uRect.xy - kPad + uv * (uRect.zw + 2.0 * kPad);
    // NDC is relative to the render target — the square surface — not the
    // visible display, which is what the fragment stage samples against.
    vec2 ndc = vPx / uSurface * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

// ES translation of shaders/glass.frag. See that file for what each term is
// for; the two must stay in step.
const char* kFS = R"(#version 300 es
precision highp float;
in  vec2 vPx;          // this pixel in screen px
out vec4 fragColor;
uniform sampler2D uScreenTex;
uniform vec4 uRect;
uniform vec2 uScreen;
uniform vec4 uParams;  // x = rounding, y = edge width, z = bend, w = alpha
uniform vec4 uTint;    // rgb = wash colour, a = wash strength
uniform vec4 uParams2; // x = blur radius px

// Shadow. Must stay inside the vertex stage's kPad or it gets clipped.
const vec2  kShadowOffset = vec2(0.0, 12.0);
const float kShadowSoft   = 28.0;
const float kShadowAlpha  = 0.30;
const float kContourAlpha = 0.12;

// How much of the rim's blur the middle of the pane gets. Not zero: the middle
// is where text sits, and a perfectly clear centre puts UI type straight onto
// whatever photo happens to be behind the window.
const float kCentreBlur = 0.35;

float sdRoundedBox(vec2 p, vec2 halfSz, float r) {
    vec2 q = abs(p) - halfSz + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// The mirrored screen arrives sRGB-encoded, and averaging encoded values is
// not averaging light: it pulls the mean towards the darker sample, so a blur
// over anything high-contrast comes out muddier than the scene it came from.
// Gamma 2.0 rather than the exact sRGB curve — invisible error, one multiply.
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
    vec3 acc = toLinear(texture(uScreenTex, px / screen).rgb);
    float wsum = 1.0;
    for (int i = 0; i < 6; ++i) {
        float a = float(i) * 1.0471975;             // 60 degrees
        vec2  d1 = vec2(cos(a), sin(a)) * radius;
        vec2  d2 = vec2(cos(a + 0.5236), sin(a + 0.5236)) * radius * 0.55;
        acc += toLinear(texture(uScreenTex, (px + d1) / screen).rgb) * 0.55;
        acc += toLinear(texture(uScreenTex, (px + d2) / screen).rgb) * 0.85;
        wsum += 1.40;
    }
    return acc / wsum;
}

void main() {
    vec2  size   = uRect.zw;
    vec2  halfSz = size * 0.5;
    float round_ = uParams.x;
    float edgeW  = max(uParams.y, 1.0);
    float bendK  = uParams.z;

    vec2 posPx = vPx;
    vec2 rel   = posPx - (uRect.xy + halfSz);

    float d = sdRoundedBox(rel, halfSz, round_);

    // Shadow, taken from the same distance field shifted down, plus a tight
    // dark contour hugging the rim. Without anything outside it the pane is a
    // hole cut in the screen rather than a sheet lying on it.
    float ds      = sdRoundedBox(rel - kShadowOffset, halfSz, round_);
    float shadowA = (1.0 - smoothstep(0.0, kShadowSoft, ds)) * kShadowAlpha
                  + (1.0 - smoothstep(0.0, 1.5, d)) * kContourAlpha;
    shadowA *= uParams.w;

    if (d > 0.0) {   // outside the pane: shadow only
        fragColor = vec4(0.0, 0.0, 0.0, shadowA);
        return;
    }

    const float e = 1.0;
    vec2 n = normalize(vec2(
        sdRoundedBox(rel + vec2(e, 0.0), halfSz, round_) - sdRoundedBox(rel - vec2(e, 0.0), halfSz, round_),
        sdRoundedBox(rel + vec2(0.0, e), halfSz, round_) - sdRoundedBox(rel - vec2(0.0, e), halfSz, round_)) + 1e-6);

    float t     = clamp(1.0 + d / edgeW, 0.0, 1.0);
    float bevel = t * t * (3.0 - 2.0 * t) * t;
    float bend  = bevel * edgeW * bendK;

    vec2 base = posPx + n * bend;

    // Blur only where the glass is doing something. A constant radius across
    // the whole pane is a frosted sheet with a nice edge; this material is
    // clear in the middle and turns over at the rim, so the softening has to
    // follow the same profile the lensing does. The ramp is much wider than
    // the bevel so it reads as a gradient rather than a ring.
    float rimness = smoothstep(-edgeW * 2.5, 0.0, d);
    float blurPx  = uParams2.x * mix(kCentreBlur, 1.0, rimness);

    // Dispersion rides on top of the blur: each channel is blurred about its
    // own bent position, so the colour split survives the softening.
    float disp = bevel * bend * 0.16;
    vec3 lin = vec3(
        sampleBlurred(base - n * disp, uScreen, blurPx).r,
        sampleBlurred(base,            uScreen, blurPx).g,
        sampleBlurred(base + n * disp, uScreen, blurPx).b);
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
    col = mix(col, uTint.rgb, uTint.a);

    // Kept well short of white: a rim that clips to full brightness stops
    // reading as light concentrated in glass and starts reading as a drawn
    // white border.
    float caustic = smoothstep(0.72, 0.97, bevel) * (1.0 - smoothstep(0.97, 1.0, bevel));
    col += caustic * 0.20;

    vec2  lightDir = normalize(vec2(-0.6, -0.8));
    float spec = max(dot(n, lightDir), 0.0);
    col += pow(spec, 3.0) * bevel * 0.18;

    // Feather the last pixel, then lay the glass over its own shadow.
    // Compositing the two here rather than letting the feather fade to nothing
    // is what keeps the border from opening a one-pixel gap through to the
    // desktop. The shadow is black, so it contributes no colour of its own.
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

} // namespace

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
    m_LocRect      = glGetUniformLocation(m_Prog, "uRect");
    m_LocScreen    = glGetUniformLocation(m_Prog, "uScreen");
    m_LocSurface   = glGetUniformLocation(m_Prog, "uSurface");
    m_LocParams    = glGetUniformLocation(m_Prog, "uParams");
    m_LocTint      = glGetUniformLocation(m_Prog, "uTint");
    m_LocParams2   = glGetUniformLocation(m_Prog, "uParams2");

    glGenVertexArrays(1, &m_VAO);   // ES 3 still wants one bound to draw
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
    // The quad now extends past the pane to carry the shadow, so a scissor
    // left over from a previous pass would clip it.
    glDisable(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTex);
    glUniform1i(m_LocScreenTex, 0);
    glUniform2f(m_LocScreen, (float)screenW, (float)screenH);
    glUniform2f(m_LocSurface, (float)surfaceW, (float)surfaceH);

    for (int i = 0; i < count; ++i) {
        const GlassRect& r = rects[i];
        if (r.w < 2.0f || r.h < 2.0f || r.alpha <= 0.001f) continue;
        glUniform4f(m_LocRect, r.x, r.y, r.w, r.h);
        glUniform4f(m_LocParams, r.rounding, r.edgeWidth, r.bend, r.alpha);
        glUniform4f(m_LocTint, r.tintR, r.tintG, r.tintB, r.tintA);
        glUniform4f(m_LocParams2, r.blur, 0.0f, 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace aimgui
