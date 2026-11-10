#version 450 core
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec2 vPx;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D uScreenTex;
layout(push_constant) uniform Push {
    vec4 screen;
    vec4 params;
    vec4 tint;
    vec4 params2;
    vec4 shapes[4];
} pc;
#define uScreenSize pc.screen.xy
#define uParams pc.params
#define uTint pc.tint
#define uParams2 pc.params2
#define uShapes pc.shapes
#define uFlipY false
#include "glass_common.glsl"
