/**
 * @file shaders.h
 * @brief GLSL fragment shaders for the 3D scene.
 *
 * The shaders are embedded as raw string literals and passed to raylib's
 * LoadShaderFromMemory(). They live in their own header so main.cpp stays
 * focused on application logic. `inline constexpr` gives each shader a single
 * program-wide copy (C++17 inline variables) while keeping the file header-only.
 *
 * Note: `#version` must be the first token of each shader, so the raw string
 * literal starts on the same line as its opening delimiter.
 */
#pragma once

namespace Shaders {

/**
 * @brief day/night transition shader for the 3D earth.
 *
 * Uses the dot product between surface normal and sun direction.
 * For solar eclipses it casts a ray from the fragment towards the sun
 * and calculates its minimum distance to the Moon's center in local space.
 */
inline constexpr const char *fs3D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform sampler2D texture2;
uniform vec3 sunDir;
uniform vec3 moonPos;
uniform float moonRadius;
uniform float earthRadius;
uniform vec3 viewPos;
uniform float cloudUVOffset;
uniform int advancedScatter;
uniform int showClouds;
void main() {
    vec4 day = texture(texture0, fragTexCoord);
    vec4 night = texture(texture1, fragTexCoord);
    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;
    float phi = fragTexCoord.y * 3.14159265359;
    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));
    float intensity = dot(normal, sunDir);
    float blend = smoothstep(-0.15, 0.15, intensity);
    vec3 fragPos = normal * earthRadius;
    vec3 scatteredDay = day.rgb;
    
    if (advancedScatter == 1) {
        vec3 viewDir = normalize(viewPos - fragPos);
        vec3 halfDir = normalize(sunDir + viewDir);
        float NdotV = max(dot(normal, viewDir), 0.0);
        float fresnel = pow(1.0 - NdotV, 4.0);
        float specPower = mix(48.0, 12.0, fresnel);
        float spec = pow(max(dot(normal, halfDir), 0.0), specPower);
        float water = clamp((day.b - day.r) * 2.5, 0.0, 1.0);
        float glareBoost = mix(0.6, 4.0, fresnel);
        vec3 specular = vec3(1.0, 0.9, 0.8) * spec * water * glareBoost * max(intensity, 0.0);
        
        float cShadow = 1.0;
        if (showClouds == 1) {
            float cloudR = earthRadius * (1.0 + 25.0 / 6371.0);
            float b = 2.0 * dot(fragPos, sunDir);
            float c = earthRadius * earthRadius - cloudR * cloudR;
            float t = (-b + sqrt(b * b - 4.0 * c)) * 0.5;
            float cloudH = max(cloudR - earthRadius, 1e-5);
            float minSunSin = 0.14;
            float maxShadowLen = cloudH / minSunSin;
            t = min(t, maxShadowLen);
            vec3 cn = normalize(fragPos + t * sunDir);
            vec2 cUV = vec2(atan(-cn.z, cn.x) / 6.28318530718 + 0.5, cn.y);
            cUV.y = acos(clamp(cUV.y, -1.0, 1.0)) / 3.14159265359;
            cUV.x = fract(cUV.x + cloudUVOffset);
            float cAlpha = texture(texture2, cUV).a;
            float termFade = smoothstep(0.00, 0.25, intensity);
            cShadow = mix(1.0, 0.1, cAlpha * termFade);
        }
        
        scatteredDay = (scatteredDay * cShadow) + specular;
    }
    
    vec3 toMoon = moonPos - fragPos;
    float distSunward = dot(toMoon, sunDir);
    float shadow = 1.0;
    if (distSunward > 0.0) {
        vec3 proj = fragPos + sunDir * distSunward;
        float distSq = dot(proj - moonPos, proj - moonPos);
        float rSq = moonRadius * moonRadius;
        if (distSq < rSq * 4.0) {
            shadow = mix(0.03, 1.0, smoothstep(rSq * 0.1, rSq * 4.0, distSq));
        }
    }
    vec4 dayColor = mix(night, vec4(scatteredDay, day.a), shadow);
    finalColor = mix(night, dayColor, blend) * fragColor;
}
)GLSL";

/**
 * @brief day/night transition shader for the 2D earth (no scattering).
 */
inline constexpr const char *fs2D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec3 sunDir;
uniform vec3 moonPos;
uniform float moonRadius;
uniform float earthRadius;
void main() {
    vec4 day = texture(texture0, fragTexCoord);
    vec4 night = texture(texture1, fragTexCoord);
    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;
    float phi = fragTexCoord.y * 3.14159265359;
    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));
    float intensity = dot(normal, sunDir);
    float blend = smoothstep(-0.15, 0.15, intensity);
    vec3 fragPos = normal * earthRadius;
    vec3 toMoon = moonPos - fragPos;
    float distSunward = dot(toMoon, sunDir);
    float shadow = 1.0;
    if (distSunward > 0.0) {
        vec3 proj = fragPos + sunDir * distSunward;
        float distSq = dot(proj - moonPos, proj - moonPos);
        float rSq = moonRadius * moonRadius;
        if (distSq < rSq * 4.0) {
            shadow = mix(0.03, 1.0, smoothstep(rSq * 0.1, rSq * 4.0, distSq));
        }
    }
    vec4 shadowedDay = vec4(day.rgb * shadow, day.a);
    finalColor = mix(night, shadowedDay, blend) * fragColor;
}
)GLSL";

/**
 * @brief cloud shader handles transparency based on sun position.
 */
inline constexpr const char *fsCloud3D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec3 sunDir;
uniform vec3 moonPos;
uniform float moonRadius;
uniform float earthRadius;
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;
    float phi = fragTexCoord.y * 3.14159265359;
    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));
    float intensity = dot(normal, sunDir);
    float alpha = smoothstep(-0.15, 0.05, intensity);
    float scatterMult = min(smoothstep(-0.3, 0.15, intensity) * smoothstep(0.15, -0.15, intensity) * 4.0, 1.0);
    vec3 sunsetDeep = vec3(0.75, 0.08, 0.10);
    vec3 sunsetWarm = vec3(1.0, 0.82, 0.75);
    float gradPos = smoothstep(-0.1, 0.0, intensity);
    vec3 sunsetColor = mix(sunsetDeep, sunsetWarm, gradPos);
    vec3 cloudColor = mix(texel.rgb, sunsetColor, scatterMult * 0.7);
    vec3 fragPos = normal * earthRadius;
    vec3 toMoon = moonPos - fragPos;
    float distSunward = dot(toMoon, sunDir);
    float shadow = 1.0;
    if (distSunward > 0.0) {
        vec3 proj = fragPos + sunDir * distSunward;
        float distSq = dot(proj - moonPos, proj - moonPos);
        float rSq = moonRadius * moonRadius;
        if (distSq < rSq * 4.0) {
            shadow = mix(0.03, 1.0, smoothstep(rSq * 0.1, rSq * 4.0, distSq));
        }
    }
    finalColor = vec4(cloudColor * shadow, texel.a * alpha) * fragColor;
}
)GLSL";

/**
 * @brief shader to handle moon self-shadowing and earth's eclipse projection.
 */
inline constexpr const char *fsMoon3D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec3 sunDir;
uniform vec3 moonPos;
uniform mat4 moonRot;
uniform float moonRadius;
uniform float earthRadiusSq;
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;
    float phi = fragTexCoord.y * 3.14159265359;
    vec3 localNormal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));
    vec3 worldNormal = normalize(mat3(moonRot) * localNormal);
    vec3 worldPos = moonPos + worldNormal * moonRadius;
    float NdotL = dot(worldNormal, sunDir);
    float diffuse = smoothstep(-0.05, 0.05, NdotL);
    float b = dot(worldPos, sunDir);
    float c = dot(worldPos, worldPos) - earthRadiusSq;
    float discriminant = b * b - c;
    float shadow = 1.0;
    if (discriminant > 0.0 && b < 0.0) {
        float distSq = dot(worldPos, worldPos) - b * b;
        float umbraSq = earthRadiusSq * 0.6;
        float penumbraSq = earthRadiusSq * 1.2;
        if (distSq < umbraSq) shadow = 0.05;
        else if (distSq < penumbraSq) shadow = mix(0.05, 1.0, smoothstep(umbraSq, penumbraSq, distSq));
    }
    vec3 umbraColor = vec3(0.5, 0.1, 0.05);
    vec3 shadowColor = mix(umbraColor * texel.rgb, texel.rgb, shadow);
    float ambient = 0.01;
    float light = max(ambient, diffuse);
    finalColor = vec4(shadowColor * light, texel.a) * fragColor;
}
)GLSL";

/**
 * @brief atmospheric scattering glow shader.
 */
inline constexpr const char *fsAtmosphere3D = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform vec3 sunDir;
uniform vec3 viewPos;
uniform float atmRadius;
void main() {
    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;
    float phi = fragTexCoord.y * 3.14159265359;
    vec3 normal = normalize(vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi)));
    vec3 worldPos = normal * atmRadius;
    vec3 viewDir = normalize(viewPos - worldPos);
    
    float NdotV = max(dot(normal, viewDir), 0.001);
    float NdotL = dot(normal, sunDir);
    
    vec3 dayColor = vec3(0.25, 0.58, 1.0);
    vec3 sunsetColor = vec3(1.0, 0.5, 0.2); // realistic gold-orange
    
    // fresnel for the soft edge glow
    float fresnel = pow(1.0 - NdotV, 2.5);
    
    // sun brightness: 15% on the night side, 100% on the day side
    float sunBlend = smoothstep(-0.3, 0.3, NdotL);
    float brightness = mix(0.05, 1.0, sunBlend);
    
    // atmosphere base color with a sunset shift near the terminator
    float sunsetBlend = smoothstep(0.35, -0.15, NdotL);
    vec3 atmosColor = mix(dayColor, sunsetColor, sunsetBlend);
    
    // brighten the atmosphere where it is thickest
    atmosColor = mix(atmosColor, vec3(0.7, 0.85, 1.0), pow(fresnel, 1.5) * 0.7);
    
    // forward-scatter glow: brighter when looking toward the sun through the limb
    float VdotL = dot(viewDir, sunDir);
    float forwardGlow = pow(max(VdotL, 0.0), 8.0) * 0.15;
    atmosColor += vec3(1.0, 0.6, 0.3) * forwardGlow * sunBlend;
    
    // smooth fadeout into the vacuum at the very edge
    float vacuumFade = smoothstep(0.0, 0.35, NdotV);
    
    // combine into a smooth transparent atmospheric ring
    vec3 color = atmosColor * brightness;
    float alpha = fresnel * vacuumFade * brightness * 2.0;
    
    finalColor = vec4(color, clamp(alpha, 0.0, 1.0)) * fragColor;
}
)GLSL";

} // namespace Shaders