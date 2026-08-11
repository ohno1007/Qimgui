#include "glass_gl.h"

#include <android/log.h>

namespace aimgui {
namespace {

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AImGui", __VA_ARGS__)

// Four corners straight from gl_VertexID — no vertex buffer.
const char* kVS = R"(#version 300 es
precision highp float;
out vec2 vUV;
uniform vec4 uRect;    // xy = pane min px, zw = pane size px
uniform vec2 uSurface; // render target size px
void main() {
    vUV = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vec2 px  = uRect.xy + vUV * uRect.zw;
    // NDC is relative to the render target — the square surface — not the
    // visible display, which is what the fragment stage samples against.
    vec2 ndc = px / uSurface * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

// ES translation of shaders/glass.frag. See that file for what each term is
// for; the two must stay in step.
const char* kFS = R"(#version 300 es
precision highp float;
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uScreenTex;
uniform vec4 uRect;
uniform vec2 uScreen;
uniform vec4 uParams;  // x = rounding, y = edge width, z = bend, w = alpha
uniform vec4 uTint;    // rgb = wash colour, a = wash strength

float sdRoundedBox(vec2 p, vec2 halfSz, float r) {
    vec2 q = abs(p) - halfSz + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2  size   = uRect.zw;
    vec2  halfSz = size * 0.5;
    float round_ = uParams.x;
    float edgeW  = max(uParams.y, 1.0);
    float bendK  = uParams.z;

    vec2 posPx = uRect.xy + vUV * size;
    vec2 rel   = posPx - (uRect.xy + halfSz);

    float d = sdRoundedBox(rel, halfSz, round_);
    if (d > 0.0) { fragColor = vec4(0.0); return; }

    const float e = 1.0;
    vec2 n = normalize(vec2(
        sdRoundedBox(rel + vec2(e, 0.0), halfSz, round_) - sdRoundedBox(rel - vec2(e, 0.0), halfSz, round_),
        sdRoundedBox(rel + vec2(0.0, e), halfSz, round_) - sdRoundedBox(rel - vec2(0.0, e), halfSz, round_)) + 1e-6);

    float t     = clamp(1.0 + d / edgeW, 0.0, 1.0);
    float bevel = t * t * (3.0 - 2.0 * t) * t;
    float bend  = bevel * edgeW * bendK;

    vec2 base = posPx + n * bend;

    float disp = bevel * bend * 0.16;
    vec3 col = vec3(
        texture(uScreenTex, (base - n * disp) / uScreen).r,
        texture(uScreenTex,  base            / uScreen).g,
        texture(uScreenTex, (base + n * disp) / uScreen).b);

    // Legibility, per pixel — applied to the refracted background only, before
    // any of the glass's own light is added. Doing it afterwards crushed the
    // caustic and the specular along with everything else, which is what made
    // the rim look grey: the highlight is the material reflecting light, not
    // content to be read through, so it must not be dimmed.
    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col *= mix(1.0, 0.40, smoothstep(0.30, 0.80, luma));
    col = mix(col, uTint.rgb, uTint.a);

    float caustic = smoothstep(0.72, 0.97, bevel) * (1.0 - smoothstep(0.97, 1.0, bevel));
    col += caustic * 0.55;

    vec2  lightDir = normalize(vec2(-0.6, -0.8));
    float spec = max(dot(n, lightDir), 0.0);
    col += pow(spec, 3.0) * bevel * 0.42;


    float aa = 1.0 - smoothstep(-1.5, 0.0, d);
    fragColor = vec4(col, uParams.w * aa);
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
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace aimgui
