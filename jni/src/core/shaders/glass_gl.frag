#version 300 es
precision highp float;
in vec2 vPx;
out vec4 fragColor;
uniform sampler2D uScreenTex;
uniform vec2 uScreenSize;
uniform vec4 uParams;
uniform vec4 uTint;
uniform vec4 uParams2;
uniform vec4 uShapes[4];
uniform bool uFlipY;
#include "glass_common.glsl"
