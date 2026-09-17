/**
 * @file coverage_shaders.h
 * @brief GLSL shaders for ground coverage rendering (3D globe and 2D map).
 */
#pragma once

namespace CoverageShaders {

/**
 * @brief Vertex shader for 3D coverage rendering.
 *
 * Rebuilds the per-satellite orthonormal basis from `satPosition` so every
 * satellite at a given altitude reuses one cached cap mesh (local +Y is the
 * sub-satellite direction).
 */
inline constexpr const char *vsCoverage3D = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform vec3 satPosition;

out vec2 fragTexCoord;
out vec3 fragWorldPos;
out vec3 fragNormal;

void main() {
    // footprint basis: s = sub-satellite dir, u/v span the perpendicular plane
    vec3 s = normalize(satPosition);
    vec3 up = (abs(s.y) > 0.99) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 u = normalize(cross(up, s));
    vec3 v = cross(s, u);
    
    vec3 worldPos = u * vertexPosition.x + s * vertexPosition.y + v * vertexPosition.z;
    
    fragWorldPos = worldPos;
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(u * vertexNormal.x + s * vertexNormal.y + v * vertexNormal.z);
    
    gl_Position = mvp * vec4(worldPos, 1.0);
}
)GLSL";

/**
 * @brief Fragment shader for 3D coverage rendering.
 *
 * Applies a slope-scaled depth bias so the cap stays in front of the Earth at
 * any zoom, plus edge anti-aliasing and a border ring.
 */
inline constexpr const char *fsCoverage3D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec3 fragWorldPos;
in vec3 fragNormal;

uniform vec4 coverageColor;
uniform vec4 borderColor;
uniform float depthBias;
uniform float edgeFalloff;
uniform vec3 cameraPos;

out vec4 finalColor;

void main() {
    float radialDist = fragTexCoord.x;

    // ~1px anti-aliased edge from the screen-space derivative
    float aa = max(fwidth(radialDist), 1e-5);
    float soft = max(edgeFalloff, aa);
    float outerMask = 1.0 - smoothstep(1.0 - soft, 1.0, radialDist);

    // border ring, matching the 2D shader
    float d = 1.0 - radialDist;
    float ring = 1.0 - smoothstep(1.0 * aa, 3.0 * aa, d);

    vec3 rgb = mix(coverageColor.rgb, borderColor.rgb, ring);
    float alpha = mix(coverageColor.a, borderColor.a, ring) * outerMask;

    finalColor = vec4(rgb, alpha);

    // slope-scaled bias: a constant NDC bias only works for one near/far range
    float slopeBias = clamp(fwidth(gl_FragCoord.z) * 2.0, 0.0, 2e-3);
    gl_FragDepth = gl_FragCoord.z - (depthBias + slopeBias);
}
)GLSL";

/**
 * @brief Fragment shader for 2D coverage rendering.
 *
 * Draws the fill and border ring from the radial texcoord, so no per-segment
 * CPU line submission is needed each frame.
 */
inline constexpr const char *fsCoverage2D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;

uniform vec4 coverageColor;
uniform vec4 borderColor;
uniform float edgeFalloff;

out vec4 finalColor;

void main() {
    float radialDist = fragTexCoord.x;

    // ~1px anti-aliased edge from the screen-space derivative
    float aa = max(fwidth(radialDist), 1e-5);
    float soft = max(edgeFalloff, aa);
    float outerMask = 1.0 - smoothstep(1.0 - soft, 1.0, radialDist);

    // border ring, replacing the old per-segment DrawLine calls
    float d = 1.0 - radialDist;
    float ring = 1.0 - smoothstep(1.0 * aa, 3.0 * aa, d);

    vec3 rgb = mix(coverageColor.rgb, borderColor.rgb, ring);
    float alpha = mix(coverageColor.a, borderColor.a, ring) * outerMask;

    finalColor = vec4(rgb, alpha) * fragColor;
}
)GLSL";

} // namespace CoverageShaders
