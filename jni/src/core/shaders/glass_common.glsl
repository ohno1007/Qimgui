// Shared by Vulkan and GLES. Coordinates are display pixels, with Y down.
// One 9-tap blur, two rim-only dispersion taps and one reflection tap.
float sdRoundedBox(vec2 p, vec2 halfSize, float radius) {
    radius = clamp(radius, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float smoothUnion(float a, float b, float radius) {
    if (radius <= 0.0) return min(a, b);
    float h = clamp(0.5 + 0.5 * (b - a) / radius, 0.0, 1.0);
    return mix(b, a, h) - radius * h * (1.0 - h);
}

float sceneSDF(vec2 p) {
    float d = sdRoundedBox(p - uShapes[0].xy, abs(uShapes[0].zw), uParams.x);
    for (int i = 1; i < 4; ++i) {
        if (uShapes[i].z <= 0.0) continue;
        float next = sdRoundedBox(p - uShapes[i].xy, abs(uShapes[i].zw), uParams.x);
        d = uShapes[i].w < 0.0 ? min(d, next) : smoothUnion(d, next, uParams2.w);
    }
    return d;
}

vec3 linearSample(vec2 px) {
    vec2 uv = px / uScreenSize;
    if (uFlipY) uv.y = 1.0 - uv.y;
    // Explicit half-texel clamping also protects imported hardware buffers.
    vec2 halfTexel = 0.5 / vec2(textureSize(uScreenTex, 0));
    vec3 color = texture(uScreenTex, clamp(uv, halfTexel, 1.0 - halfTexel)).rgb;
    return color * color;
}

vec3 blurred(vec2 px, float radius) {
    vec3 color = linearSample(px);
    if (radius <= 0.5) return color;
    color *= 0.25;
    color += (linearSample(px + vec2(radius, 0.0)) +
              linearSample(px - vec2(radius, 0.0)) +
              linearSample(px + vec2(0.0, radius)) +
              linearSample(px - vec2(0.0, radius))) * 0.125;
    vec2 diagonal = vec2(radius * 0.70710678);
    color += (linearSample(px + diagonal) + linearSample(px - diagonal) +
              linearSample(px + vec2(diagonal.x, -diagonal.y)) +
              linearSample(px + vec2(-diagonal.x, diagonal.y))) * 0.0625;
    return color;
}

void main() {
    float edgeWidth = max(uParams.y, 1.0);
    float alpha = clamp(uParams.w, 0.0, 1.0);
    float d = sceneSDF(vPx);
    // Derivatives must be evaluated before divergent fragment branches.
    float aaWidth = max(fwidth(d), 0.75);
    float coverage = 1.0 - smoothstep(-aaWidth, aaWidth, d);
    float shadowSize = clamp(edgeWidth * 0.45, 4.0, 22.0);
    float ds = sceneSDF(vPx - vec2(0.0, shadowSize * 0.35));
    float shadow = (1.0 - smoothstep(0.0, shadowSize, ds)) * 0.20 * alpha;
    if (coverage <= 0.0) {
        fragColor = vec4(0.0, 0.0, 0.0, shadow);
        return;
    }

    float rim = clamp(1.0 + d / edgeWidth, 0.0, 1.0);
    float bevel = rim * rim * (3.0 - 2.0 * rim);
    vec2 normal = vec2(0.0);
    if (rim > 0.0) {
        vec2 gradient = vec2(
            sceneSDF(vPx + vec2(0.75, 0.0)) - sceneSDF(vPx - vec2(0.75, 0.0)),
            sceneSDF(vPx + vec2(0.0, 0.75)) - sceneSDF(vPx - vec2(0.0, 0.75)));
        normal = gradient * inversesqrt(max(dot(gradient, gradient), 1e-8));
    }
    // Keep the centre readable; concentrate refraction at the curved rim.
    float bend = bevel * bevel * edgeWidth * clamp(uParams.z, 0.0, 2.0);
    vec2 refracted = vPx + normal * bend;
    float blurRadius = max(uParams2.x, 0.0) * mix(0.20, 1.0, bevel);
    vec3 color = blurred(refracted, blurRadius);
    if (bevel > 0.05) {
        float dispersion = min(1.25, bend * 0.035);
        vec3 redSide = linearSample(refracted - normal * dispersion);
        vec3 blueSide = linearSample(refracted + normal * dispersion);
        color.rb = mix(color.rb, vec2(redSide.r, blueSide.b), bevel * 0.35);
    }
    color = sqrt(max(color, vec3(0.0)));
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color *= 1.0 - 0.18 * smoothstep(0.60, 1.0, luma);
    color = mix(color, uTint.rgb, clamp(uTint.a, 0.0, 1.0));

    vec2 light = uParams2.yz;
    light *= inversesqrt(max(dot(light, light), 1e-8));
    float facing = max(dot(normal, light), 0.0);
    float fresnel = bevel * bevel * bevel;
    float rimLine = exp(-abs(d + 0.75) * 1.25);
    color += vec3(0.90, 0.95, 1.0) * rimLine * (0.08 + facing * 0.25);
    color += vec3(1.0) * facing * facing * fresnel * 0.14;
    if (bevel > 0.05) {
        vec3 reflection = sqrt(max(linearSample(vPx + normal * edgeWidth), vec3(0.0)));
        color = mix(color, reflection, fresnel * 0.08);
    }

    float glassAlpha = alpha * coverage;
    float outAlpha = glassAlpha + shadow * (1.0 - glassAlpha);
    fragColor = vec4(clamp(color, 0.0, 1.0) * glassAlpha / max(outAlpha, 1e-5), outAlpha);
}
