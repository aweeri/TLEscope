#pragma once

namespace CoverageShaders {

/* vertex shader for 3D coverage; rebuilds the per-satellite basis so one cached cap mesh is reused */
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

/* fragment shader for 3D coverage; slope-scaled depth bias keeps the cap in front of the Earth at any zoom */
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

/* fragment shader for 2D coverage; draws fill and border from the radial texcoord, no per-segment CPU lines */
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
