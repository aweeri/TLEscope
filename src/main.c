#define _GNU_SOURCE
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astro.h"
#include "config.h"

static const char* GetAssetPath(const char* theme, const char* filename) {
    static char path[256];
    snprintf(path, sizeof(path), "themes/%s/%s", theme, filename);
    if (FileExists(path)) return path;
    snprintf(path, sizeof(path), "themes/default/%s", filename);
    return path;
}
#include "types.h"
#include "ui.h"

/* * shaders for day/night transition
 * uses dot product between surface normal and sun direction
 * casting a ray from the fragment towards the sun and calculating its minimum distance to the Moon's center in local space for solar eclipses
 */
const char *fs3D = "#version 330\n"
                   "in vec2 fragTexCoord;\n"
                   "in vec4 fragColor;\n"
                   "out vec4 finalColor;\n"
                   "uniform sampler2D texture0;\n"
                   "uniform sampler2D texture1;\n"
                   "uniform vec3 sunDir;\n"
                   "uniform vec3 viewPos;\n"
                   "uniform vec3 moonPos;\n"
                   "uniform float moonRadius;\n"
                   "uniform float earthRadius;\n"
                   "void main() {\n"
                   "    vec4 day = texture(texture0, fragTexCoord);\n"
                   "    vec4 night = texture(texture1, fragTexCoord);\n"
                   "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
                   "    float phi = fragTexCoord.y * 3.14159265359;\n"
                   "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
                   "    float intensity = dot(normal, sunDir);\n"
                   "    float blend = smoothstep(-0.15, 0.15, intensity);\n"
                   "    vec3 fragPos = normal * earthRadius;\n"
                   "    vec3 viewDir = normalize(viewPos - fragPos);\n"
                   "    float NdotV = max(dot(normal, viewDir), 0.0);\n"
                   "    float scatterMult = smoothstep(-0.2, 0.2, intensity) * smoothstep(0.2, -0.2, intensity);\n"
                   "    vec3 sunsetColor = vec3(1.0, 0.5, 0.2);\n"
                   "    vec3 scatteredDay = mix(day.rgb, day.rgb * sunsetColor * 2.0, scatterMult * (1.0 - NdotV));\n"
                   "    vec3 hazeColor = mix(vec3(0.15, 0.35, 0.75), sunsetColor, scatterMult);\n"
                   "    float surfaceHaze = pow(1.0 - NdotV, 2.5) * smoothstep(-0.1, 0.2, intensity) * 0.55;\n"
                   "    scatteredDay = mix(scatteredDay, hazeColor, surfaceHaze);\n"
                   "    vec3 toMoon = moonPos - fragPos;\n"
                   "    float distSunward = dot(toMoon, sunDir);\n"
                   "    float shadow = 1.0;\n"
                   "    if (distSunward > 0.0) {\n"
                   "        vec3 proj = fragPos + sunDir * distSunward;\n"
                   "        float distSq = dot(proj - moonPos, proj - moonPos);\n"
                   "        float rSq = moonRadius * moonRadius;\n"
                   "        if (distSq < rSq * 4.0) {\n"
                   "            shadow = mix(0.03, 1.0, smoothstep(rSq * 0.1, rSq * 4.0, distSq));\n"
                   "        }\n"
                   "    }\n"
                   "    vec4 shadowedDay = vec4(scatteredDay * shadow, day.a);\n"
                   "    finalColor = mix(night, shadowedDay, blend) * fragColor;\n"
                   "}\n";

const char *fs2D = "#version 330\n"
                   "in vec2 fragTexCoord;\n"
                   "in vec4 fragColor;\n"
                   "out vec4 finalColor;\n"
                   "uniform sampler2D texture0;\n"
                   "uniform sampler2D texture1;\n"
                   "uniform vec3 sunDir;\n"
                   "uniform vec3 moonPos;\n"
                   "uniform float moonRadius;\n"
                   "uniform float earthRadius;\n"
                   "void main() {\n"
                   "    vec4 day = texture(texture0, fragTexCoord);\n"
                   "    vec4 night = texture(texture1, fragTexCoord);\n"
                   "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
                   "    float phi = fragTexCoord.y * 3.14159265359;\n"
                   "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
                   "    float intensity = dot(normal, sunDir);\n"
                   "    float blend = smoothstep(-0.15, 0.15, intensity);\n"
                   "    vec3 fragPos = normal * earthRadius;\n"
                   "    vec3 toMoon = moonPos - fragPos;\n"
                   "    float distSunward = dot(toMoon, sunDir);\n"
                   "    float shadow = 1.0;\n"
                   "    if (distSunward > 0.0) {\n"
                   "        vec3 proj = fragPos + sunDir * distSunward;\n"
                   "        float distSq = dot(proj - moonPos, proj - moonPos);\n"
                   "        float rSq = moonRadius * moonRadius;\n"
                   "        if (distSq < rSq * 4.0) {\n"
                   "            shadow = mix(0.03, 1.0, smoothstep(rSq * 0.1, rSq * 4.0, distSq));\n"
                   "        }\n"
                   "    }\n"
                   "    vec4 shadowedDay = vec4(day.rgb * shadow, day.a);\n"
                   "    finalColor = mix(night, shadowedDay, blend) * fragColor;\n"
                   "}\n";

/* cloud shader handles transparency based on sun position */
const char *fsCloud3D = "#version 330\n"
                        "in vec2 fragTexCoord;\n"
                        "in vec4 fragColor;\n"
                        "out vec4 finalColor;\n"
                        "uniform sampler2D texture0;\n"
                        "uniform vec3 sunDir;\n"
                        "uniform vec3 moonPos;\n"
                        "uniform float moonRadius;\n"
                        "uniform float earthRadius;\n"
                        "void main() {\n"
                        "    vec4 texel = texture(texture0, fragTexCoord);\n"
                        "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
                        "    float phi = fragTexCoord.y * 3.14159265359;\n"
                        "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
                        "    float intensity = dot(normal, sunDir);\n"
                        "    float alpha = smoothstep(-0.15, 0.05, intensity);\n"
                        "    vec3 fragPos = normal * earthRadius;\n"
                        "    vec3 toMoon = moonPos - fragPos;\n"
                        "    float distSunward = dot(toMoon, sunDir);\n"
                        "    float shadow = 1.0;\n"
                        "    if (distSunward > 0.0) {\n"
                        "        vec3 proj = fragPos + sunDir * distSunward;\n"
                        "        float distSq = dot(proj - moonPos, proj - moonPos);\n"
                        "        float rSq = moonRadius * moonRadius;\n"
                        "        if (distSq < rSq * 4.0) {\n"
                        "            shadow = mix(0.03, 1.0, smoothstep(rSq * 0.1, rSq * 4.0, distSq));\n"
                        "        }\n"
                        "    }\n"
                        "    finalColor = vec4(texel.rgb * shadow, texel.a * alpha) * fragColor;\n"
                        "}\n";

/* shader to handle moon self-shadowing and earth's eclipse projection */
const char *fsMoon3D = "#version 330\n"
                       "in vec2 fragTexCoord;\n"
                       "in vec4 fragColor;\n"
                       "out vec4 finalColor;\n"
                       "uniform sampler2D texture0;\n"
                       "uniform vec3 sunDir;\n"
                       "uniform vec3 moonPos;\n"
                       "uniform mat4 moonRot;\n"
                       "uniform float moonRadius;\n"
                       "uniform float earthRadiusSq;\n"
                       "void main() {\n"
                       "    vec4 texel = texture(texture0, fragTexCoord);\n"
                       "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
                       "    float phi = fragTexCoord.y * 3.14159265359;\n"
                       "    vec3 localNormal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
                       "    vec3 worldNormal = normalize(mat3(moonRot) * localNormal);\n"
                       "    vec3 worldPos = moonPos + worldNormal * moonRadius;\n"
                       "    float NdotL = dot(worldNormal, sunDir);\n"
                       "    float diffuse = smoothstep(-0.05, 0.05, NdotL);\n"
                       "    float b = dot(worldPos, sunDir);\n"
                       "    float c = dot(worldPos, worldPos) - earthRadiusSq;\n"
                       "    float discriminant = b * b - c;\n"
                       "    float shadow = 1.0;\n"
                       "    if (discriminant > 0.0 && b < 0.0) {\n"
                       "        float distSq = dot(worldPos, worldPos) - b * b;\n"
                       "        float umbraSq = earthRadiusSq * 0.6;\n"
                       "        float penumbraSq = earthRadiusSq * 1.2;\n"
                       "        if (distSq < umbraSq) shadow = 0.05;\n"
                       "        else if (distSq < penumbraSq) shadow = mix(0.05, 1.0, smoothstep(umbraSq, penumbraSq, distSq));\n"
                       "    }\n"
                       "    vec3 umbraColor = vec3(0.5, 0.1, 0.05);\n"
                       "    vec3 shadowColor = mix(umbraColor * texel.rgb, texel.rgb, shadow);\n"
                       "    float ambient = 0.01;\n"
                       "    float light = max(ambient, diffuse);\n"
                       "    finalColor = vec4(shadowColor * light, texel.a) * fragColor;\n"
                       "}\n";

/* atmospheric scattering glow shader */
const char *fsAtmosphere3D = "#version 330\n"
                       "in vec2 fragTexCoord;\n"
                       "in vec4 fragColor;\n"
                       "out vec4 finalColor;\n"
                       "uniform vec3 sunDir;\n"
                       "uniform vec3 viewPos;\n"
                       "uniform float atmRadius;\n"
                       "void main() {\n"
                       "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
                       "    float phi = fragTexCoord.y * 3.14159265359;\n"
                       "    vec3 normal = normalize(vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi)));\n"
                       "    vec3 worldPos = normal * atmRadius;\n"
                       "    vec3 viewDir = normalize(viewPos - worldPos);\n"
                       "    \n"
                       "    float NdotV = max(dot(normal, viewDir), 0.001);\n"
                       "    float NdotL = dot(normal, sunDir);\n"
                       "    \n"
                       "    vec3 dayColor = vec3(0.3, 0.6, 1.0);\n"
                       "    vec3 sunsetColor = vec3(1.0, 0.5, 0.2); // Realistic gold-orange\n"
                       "    \n"
                       "    // shift to sunset colors near the terminator\n"
                       "    float sunsetBlend = smoothstep(0.6, -0.15, NdotL);\n"
                       "    vec3 atmosColor = mix(dayColor, sunsetColor, sunsetBlend);\n"
                       "    \n"
                       "    // fresnel for soft edge glow\n"
                       "    float fresnel = pow(1.0 - NdotV, 3.5);\n"
                       "    \n"
                       "    // shiten the atmosphere where it is thickest\n"
                       "    atmosColor = mix(atmosColor, vec3(0.85, 0.9, 1.0), fresnel * 0.6);\n"
                       "    \n"
                       "    // smooth fadeout into the vacuum at the absolute edge\n"
                       "    float vacuumFade = smoothstep(0.0, 0.25, NdotV);\n"
                       "    \n"
                       "    // mask out the night side\n"
                       "    float dayMask = smoothstep(-0.15, 0.1, NdotL);\n"
                       "    \n"
                       "    // combine for a smoof transparent atmospheric ring\n"
                       "    float alpha = fresnel * vacuumFade * dayMask * 2.5;\n"
                       "    \n"
                       "    finalColor = vec4(atmosColor, clamp(alpha, 0.0, 1.0)) * fragColor;\n"
                       "}\n";

/* application state and resources */
static AppConfig cfg = {
    .window_width = 1280,
    .window_height = 720,
    .target_fps = 60,
    .ui_scale = 1.0f,
    .show_clouds = false,
    .show_night_lights = true,
    .show_markers = true,
    .show_statistics = false,
    .highlight_sunlit = false,
    .show_slant_range = false,
    .show_scattering = false,
    .hint_vsync = false,
    .bg_color = {0, 0, 0, 255},
    .text_main = {255, 255, 255, 255},
    .theme = "default",
    .ui_primary = {32, 32, 32, 255},
    .ui_secondary = {64, 64, 64, 255},
    .ui_accent = {0, 255, 0, 255},
    .window_border = {110, 110, 110, 255},
    .window_border_focus = {0, 255, 0, 255},
    .scope_bg = {10, 15, 25, 255},
    .scope_horizon = {45, 30, 20, 255},
    .overlay_dim = {0, 0, 0, 180}
};

static Font customFont;
static Texture2D satIcon, markerIcon, earthTexture, moonTexture, cloudTexture, earthNightTexture, skyboxTexture;
static Texture2D periMark, apoMark;
static Model earthModel, moonModel, cloudModel, atmosphereModel, skyboxModel;

/* manual mesh generation for the planetary spheres */
static Mesh GenEarthMesh(float radius, int slices, int rings)
{
    Mesh mesh = {0};
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices * 2;

    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;
    mesh.vertices = (float *)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.indices = (unsigned short *)MemAlloc(triangleCount * 3 * sizeof(unsigned short));

    int vIndex = 0;
    for (int i = 0; i <= rings; i++)
    {
        float v = (float)i / (float)rings;
        float phi = v * PI;
        for (int j = 0; j <= slices; j++)
        {
            float u = (float)j / (float)slices;
            float theta = (u - 0.5f) * 2.0f * PI;
            float x = cosf(theta) * sinf(phi);
            float y = cosf(phi);
            float z = -sinf(theta) * sinf(phi);

            mesh.vertices[vIndex * 3 + 0] = x * radius;
            mesh.vertices[vIndex * 3 + 1] = y * radius;
            mesh.vertices[vIndex * 3 + 2] = z * radius;
            mesh.normals[vIndex * 3 + 0] = x;
            mesh.normals[vIndex * 3 + 1] = y;
            mesh.normals[vIndex * 3 + 2] = z;
            mesh.texcoords[vIndex * 2 + 0] = u;
            mesh.texcoords[vIndex * 2 + 1] = v;
            vIndex++;
        }
    }

    int iIndex = 0;
    for (int i = 0; i < rings; i++)
    {
        for (int j = 0; j < slices; j++)
        {
            int first = (i * (slices + 1)) + j;
            int second = first + slices + 1;
            mesh.indices[iIndex++] = first;
            mesh.indices[iIndex++] = second;
            mesh.indices[iIndex++] = first + 1;
            mesh.indices[iIndex++] = second;
            mesh.indices[iIndex++] = second + 1;
            mesh.indices[iIndex++] = first + 1;
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

/* render orbit lines in 3d space */
static void draw_orbit_3d(Satellite *sat, double current_epoch, bool is_highlighted, float alpha, int step)
{
    Color orbitColor = ApplyAlpha(is_highlighted ? cfg.orbit_highlighted : cfg.orbit_normal, alpha);

    if (is_highlighted)
    {
        Vector3 prev_pos = {0};
        double orbits_count = 1.0;
        int segments = fmin(4000, fmax(90, (int)(400 * orbits_count)));
        double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
        double time_step = (period_days * orbits_count) / segments;

        Vector3 base_sun_dir = {0};
        if (cfg.highlight_sunlit)
        {
            base_sun_dir = Vector3Normalize(calculate_sun_position(current_epoch));
        }

        for (int i = 0; i <= segments; i++)
        {
            double t = (i == 0) ? current_epoch : (current_epoch - fmod(current_epoch, time_step) + (i * time_step));
            double t_unix = get_unix_from_epoch(t);
            Vector3 raw_pos = calculate_position(sat, t_unix);
            Vector3 pos = Vector3Scale(raw_pos, 1.0f / DRAW_SCALE);

            if (i > 0)
            {
                Color drawCol = orbitColor;
                if (cfg.highlight_sunlit)
                {
                    if (!is_sat_eclipsed(raw_pos, base_sun_dir))
                        drawCol = ApplyAlpha(cfg.sat_highlighted, alpha);
                    else
                        drawCol = ApplyAlpha(cfg.orbit_normal, alpha);
                }
                DrawLine3D(prev_pos, pos, drawCol);
            }
            prev_pos = pos;
        }
    }
    else
    {
        if (!sat->orbit_cached)
            return;
        Vector3 prev_pos = sat->orbit_cache[0];
        for (int i = step; i < ORBIT_CACHE_SIZE; i += step)
        {
            Vector3 pos = sat->orbit_cache[i];
            DrawLine3D(prev_pos, pos, orbitColor);
            prev_pos = pos;
        }
        if ((ORBIT_CACHE_SIZE - 1) % step != 0)
        {
            DrawLine3D(prev_pos, sat->orbit_cache[ORBIT_CACHE_SIZE - 1], orbitColor);
        }
    }
}

/* simple progress bar during init */
static void DrawLoadingScreen(float progress, const char *message, Texture2D logoTex)
{
    BeginDrawing();
    ClearBackground(cfg.bg_color);

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float barW = 400 * cfg.ui_scale;
    float barH = 16 * cfg.ui_scale;

    float startY = screenH / 2.0f;

    if (logoTex.id != 0)
    {
        float logoScale = (120.0f * cfg.ui_scale) / logoTex.height;
        Vector2 logoPos = {(screenW - logoTex.width * logoScale) / 2.0f, startY - (logoTex.height * logoScale) - 40 * cfg.ui_scale};
        DrawTextureEx(logoTex, logoPos, 0.0f, logoScale, WHITE);
    }

    Rectangle barOutline = {(screenW - barW) / 2, startY, barW, barH};
    Rectangle barProgress = {barOutline.x + 3, barOutline.y + 3, (barW - 6) * progress, barH - 6};

#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
    DrawRectangleRoundedLines(barOutline, 0.5f, 16, 2.0f, cfg.text_main);
#else
    DrawRectangleRoundedLines(barOutline, 0.5f, 16, cfg.text_main);
#endif
    if (progress > 0.0f)
        DrawRectangleRounded(barProgress, 0.5f, 16, cfg.text_secondary);

    Vector2 msgSize = MeasureTextEx(customFont, message, 18 * cfg.ui_scale, 1.0f);
    DrawUIText(customFont, message, (screenW - msgSize.x) / 2, barOutline.y + barH + 20 * cfg.ui_scale, 18 * cfg.ui_scale, cfg.text_main);

    EndDrawing();
}

static bool GetMouseEarthIntersection(Vector2 mouse, bool is_2d, Camera2D cam2d, Camera3D cam3d, double gmst_deg, float earth_offset, float map_w, float map_h, float *out_lat, float *out_lon)
{
    if (is_2d)
    {
        Vector2 world = GetScreenToWorld2D(mouse, cam2d);
        float mx = world.x;
        float my = world.y;
        // Wrap x to [-map_w/2, map_w/2)
        mx = fmodf(mx + map_w / 2, map_w);
        if (mx < 0)
            mx += map_w;
        mx -= map_w / 2;
        if (my < -map_h / 2 || my > map_h / 2)
            return false;
        *out_lat = -(my / map_h) * 180.0f;
        *out_lon = (mx / map_w) * 360.0f;
        if (*out_lon > 180)
            *out_lon -= 360;
        else if (*out_lon < -180)
            *out_lon += 360;
        return true;
    }
    else
    {
        Ray ray = GetMouseRay(mouse, cam3d);
        float earthRadius = EARTH_RADIUS_KM / DRAW_SCALE;
        RayCollision col = GetRayCollisionSphere(ray, (Vector3){0, 0, 0}, earthRadius);
        if (col.hit)
        {
            Vector3 point = col.point;
            float r = Vector3Length(point);
            if (r < 0.001f)
                return false;
            point = Vector3Scale(point, 1.0f / r);
            float lat = asinf(point.y) * RAD2DEG;
            float lon_ecef = atan2f(-point.z, point.x) * RAD2DEG;
            float lon = lon_ecef - (gmst_deg + earth_offset);
            while (lon > 180)
                lon -= 360;
            while (lon < -180)
                lon += 360;
            *out_lat = lat;
            *out_lon = lon;
            return true;
        }
        return false;
    }
}

int main(void)
{
    LoadAppConfig("settings.json", &cfg);

    /* window setup and msaa */
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(cfg.window_width, cfg.window_height, "TLEscope");

    int monitor = GetCurrentMonitor();
    int max_w = GetMonitorWidth(monitor);
    int max_h = GetMonitorHeight(monitor);
    int current_w = GetScreenWidth();
    int current_h = GetScreenHeight();

    if (current_w >= max_w || current_h >= max_h)
    {
        SetWindowState(FLAG_WINDOW_MAXIMIZED);
    }
    else
    {
        Vector2 monitorPos = GetMonitorPosition(monitor);
        SetWindowPosition((int)monitorPos.x + (max_w - current_w) / 2, (int)monitorPos.y + (max_h - current_h) / 2);
    }

    SetExitKey(0);

    /* logo and icon loading */
    Image logoImg = LoadImage("logo.png");
    Texture2D logoTex = {0};
    if (logoImg.data != NULL)
    {
        SetWindowIcon(logoImg);
        logoTex = LoadTextureFromImage(logoImg);
        UnloadImage(logoImg);
    }
    Image logoLImg = LoadImage("logo.png");
    Texture2D logoLTex = {0};
    if (logoLImg.data != NULL)
    {
        logoLTex = LoadTextureFromImage(logoLImg);
        UnloadImage(logoLImg);
    }

    /* font loading with specific glyph range */
    int glyphsCount = 0;
    int *glyphs = LoadCodepoints(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", &glyphsCount);
    customFont = LoadFontEx(GetAssetPath(cfg.theme, "font.ttf"), 64, glyphs, glyphsCount);
    GenTextureMipmaps(&customFont.texture);
    SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
    UnloadCodepoints(glyphs);

    /* resource loading phase */
    DrawLoadingScreen(0.1f, "Fetching TLE Data...", logoLTex);
    load_tle_data("data.tle");
    load_manual_tles(&cfg);
    LoadSatSelection(); // restore active satellites

    DrawLoadingScreen(0.25f, "Initializing Textures...", logoTex);
    earthTexture = LoadTexture(GetAssetPath(cfg.theme, "earth.png"));
    earthNightTexture = LoadTexture(GetAssetPath(cfg.theme, "earth_night.png"));
    skyboxTexture = LoadTexture(GetAssetPath(cfg.theme, "skybox.png"));

    /* make textures not blocky when zoomed in on*/
    SetTextureFilter(earthTexture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(earthNightTexture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(skyboxTexture, TEXTURE_FILTER_BILINEAR);

    DrawLoadingScreen(0.4f, "Compiling Shaders...", logoTex);
    Shader shader3D = LoadShaderFromMemory(NULL, fs3D);
    int sunDirLoc3D = GetShaderLocation(shader3D, "sunDir");
    int viewPosLoc3D = GetShaderLocation(shader3D, "viewPos");
    shader3D.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader3D, "texture1");

    Shader shader2D = LoadShaderFromMemory(NULL, fs2D);
    int sunDirLoc2D = GetShaderLocation(shader2D, "sunDir");
    int nightTexLoc2D = GetShaderLocation(shader2D, "texture1");

    Shader shaderCloud = LoadShaderFromMemory(NULL, fsCloud3D);
    int sunDirLocCloud = GetShaderLocation(shaderCloud, "sunDir");

    Shader shaderMoon = LoadShaderFromMemory(NULL, fsMoon3D);
    int sunDirLocMoon = GetShaderLocation(shaderMoon, "sunDir");
    int moonPosLocMoon = GetShaderLocation(shaderMoon, "moonPos");
    int moonRotLocMoon = GetShaderLocation(shaderMoon, "moonRot");
    int moonRadiusLocMoon = GetShaderLocation(shaderMoon, "moonRadius");
    int earthRadiusSqLocMoon = GetShaderLocation(shaderMoon, "earthRadiusSq");

    Shader shaderAtmosphere = LoadShaderFromMemory(NULL, fsAtmosphere3D);
    int sunDirLocAtmosphere = GetShaderLocation(shaderAtmosphere, "sunDir");
    int viewPosLocAtmosphere = GetShaderLocation(shaderAtmosphere, "viewPos");

    DrawLoadingScreen(0.6f, "Generating Meshes...", logoTex);
    float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
    Mesh sphereMesh = GenEarthMesh(draw_earth_radius, 64, 64);
    earthModel = LoadModelFromMesh(sphereMesh);
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
    earthModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = earthNightTexture;
    Shader defaultEarthShader = earthModel.materials[0].shader;

    DrawLoadingScreen(0.8f, "Loading Celestial Bodies...", logoTex);

    Mesh skyboxMesh = GenEarthMesh(-500.0f, 32, 32); /* negative radius flips normals inward */
    skyboxModel = LoadModelFromMesh(skyboxMesh);
    skyboxModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skyboxTexture;

    float draw_cloud_radius = (EARTH_RADIUS_KM + 25.0f) / DRAW_SCALE;
    Mesh cloudMesh = GenEarthMesh(draw_cloud_radius, 64, 64);
    cloudModel = LoadModelFromMesh(cloudMesh);
    cloudTexture = LoadTexture(GetAssetPath(cfg.theme, "clouds.png"));
    SetTextureFilter(cloudTexture, TEXTURE_FILTER_BILINEAR);
    cloudModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudTexture;
    Shader defaultCloudShader = cloudModel.materials[0].shader;

    float draw_atmosphere_radius = (EARTH_RADIUS_KM + 60.0f) / DRAW_SCALE;
    Mesh atmosphereMesh = GenEarthMesh(draw_atmosphere_radius, 64, 64);
    atmosphereModel = LoadModelFromMesh(atmosphereMesh);
    atmosphereModel.materials[0].shader = shaderAtmosphere;
    SetShaderValue(shaderAtmosphere, GetShaderLocation(shaderAtmosphere, "atmRadius"), &draw_atmosphere_radius, SHADER_UNIFORM_FLOAT);

    float draw_moon_radius = MOON_RADIUS_KM / DRAW_SCALE;
    Mesh moonMesh = GenEarthMesh(draw_moon_radius, 32, 32);
    moonModel = LoadModelFromMesh(moonMesh);
    moonTexture = LoadTexture(GetAssetPath(cfg.theme, "moon.png"));
    SetTextureFilter(moonTexture, TEXTURE_FILTER_BILINEAR);
    moonModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture;
    moonModel.materials[0].shader = shaderMoon;
    float earthRadSq = (EARTH_RADIUS_KM / DRAW_SCALE) * (EARTH_RADIUS_KM / DRAW_SCALE);
    SetShaderValue(shaderMoon, moonRadiusLocMoon, &draw_moon_radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderMoon, earthRadiusSqLocMoon, &earthRadSq, SHADER_UNIFORM_FLOAT);

    int moonPosLoc3D = GetShaderLocation(shader3D, "moonPos");
    int moonPosLoc2D = GetShaderLocation(shader2D, "moonPos");
    int moonPosLocCloud = GetShaderLocation(shaderCloud, "moonPos");
    SetShaderValue(shader3D, GetShaderLocation(shader3D, "earthRadius"), &draw_earth_radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader3D, GetShaderLocation(shader3D, "moonRadius"), &draw_moon_radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader2D, GetShaderLocation(shader2D, "earthRadius"), &draw_earth_radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader2D, GetShaderLocation(shader2D, "moonRadius"), &draw_moon_radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderCloud, GetShaderLocation(shaderCloud, "earthRadius"), &draw_cloud_radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderCloud, GetShaderLocation(shaderCloud, "moonRadius"), &draw_moon_radius, SHADER_UNIFORM_FLOAT);

    DrawLoadingScreen(0.95f, "Finalizing UI...", logoTex);
    satIcon = LoadTexture(GetAssetPath(cfg.theme, "sat_icon.png"));
    markerIcon = LoadTexture(GetAssetPath(cfg.theme, "marker_icon.png"));
    periMark = LoadTexture(GetAssetPath(cfg.theme, "smallmark.png"));
    apoMark = LoadTexture(GetAssetPath(cfg.theme, "smallmark.png"));

    SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(periMark, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(apoMark, TEXTURE_FILTER_BILINEAR);

    DrawLoadingScreen(1.0f, "Ready!", logoTex);

    /* camera defaults */
    Camera Camera3DParams = {0};
    Camera3DParams.target = (Vector3){0.0f, 0.0f, 0.0f};
    Camera3DParams.up = (Vector3){0.0f, 1.0f, 0.0f};
    Camera3DParams.fovy = 45.0f;
    Camera3DParams.projection = CAMERA_PERSPECTIVE;

    Camera2D Camera2DParams = {0};
    Camera2DParams.zoom = 1.0f;
    Camera2DParams.offset = (Vector2){GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
    Camera2DParams.target = (Vector2){0.0f, 0.0f};

    float target_camera2d_zoom = Camera2DParams.zoom;
    Vector2 target_camera2d_target = Camera2DParams.target;

    float map_w = 2048.0f, map_h = 1024.0f;
    float camDistance = 10.0f, camAngleX = 0.785f, camAngleY = 0.5f;

    float target_camDistance = camDistance;
    float target_camAngleX = camAngleX;
    float target_camAngleY = camAngleY;
    Vector3 target_camera3d_target = Camera3DParams.target;

    double current_epoch = get_current_real_time_epoch();
    double time_multiplier = 1.0;
    double saved_multiplier = 1.0;
    bool is_2d_view = false;

    bool hide_unselected = false;
    float unselected_fade = 1.0f;

    bool picking_home = false;
    bool is_auto_warping = false;
    double auto_warp_target = 0.0;
    double auto_warp_initial_diff = 0.0;
    bool exit_app = false;
    bool is_ecliptic_frame = false;
    float current_ecliptic_angle = 0.0f;
    bool is_pov_mode = false;

    bool show_scope = false;
    float scope_az = 180.0f;
    float scope_el = 45.0f;
    float scope_beam = 30.0f;

    Satellite *hovered_sat = NULL;
    Satellite *selected_sat = NULL;
    TargetLock active_lock = LOCK_EARTH;
    double last_left_click_time = 0.0;

    if (!cfg.hint_vsync) SetTargetFPS(cfg.target_fps);
    else SetTargetFPS(0);
    
    int current_update_idx = 0;

    /* main loop */
    while (!WindowShouldClose() && !exit_app)
    {
        if (cfg.reload_theme)
        {
            cfg.reload_theme = false;
            
            UnloadTexture(earthTexture);
            UnloadTexture(earthNightTexture);
            UnloadTexture(cloudTexture);
            UnloadTexture(moonTexture);
            UnloadTexture(skyboxTexture);
            UnloadTexture(satIcon);
            UnloadTexture(markerIcon);
            UnloadTexture(periMark);
            UnloadTexture(apoMark);
            UnloadFont(customFont);
            
            LoadAppConfig("settings.json", &cfg);
            
            int glyphsCount = 0;
            int *glyphs = LoadCodepoints(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", &glyphsCount);
            customFont = LoadFontEx(GetAssetPath(cfg.theme, "font.ttf"), 64, glyphs, glyphsCount);
            GenTextureMipmaps(&customFont.texture);
            SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
            UnloadCodepoints(glyphs);

            earthTexture = LoadTexture(GetAssetPath(cfg.theme, "earth.png"));
            earthNightTexture = LoadTexture(GetAssetPath(cfg.theme, "earth_night.png"));
            SetTextureFilter(earthTexture, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(earthNightTexture, TEXTURE_FILTER_BILINEAR);
            earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
            earthModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = earthNightTexture;

            cloudTexture = LoadTexture(GetAssetPath(cfg.theme, "clouds.png"));
            SetTextureFilter(cloudTexture, TEXTURE_FILTER_BILINEAR);
            cloudModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudTexture;

            skyboxTexture = LoadTexture(GetAssetPath(cfg.theme, "skybox.png"));
            SetTextureFilter(skyboxTexture, TEXTURE_FILTER_BILINEAR);
            skyboxModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skyboxTexture;

            moonTexture = LoadTexture(GetAssetPath(cfg.theme, "moon.png"));
            SetTextureFilter(moonTexture, TEXTURE_FILTER_BILINEAR);
            moonModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture;

            satIcon = LoadTexture(GetAssetPath(cfg.theme, "sat_icon.png"));
            markerIcon = LoadTexture(GetAssetPath(cfg.theme, "marker_icon.png"));
            periMark = LoadTexture(GetAssetPath(cfg.theme, "smallmark.png"));
            apoMark = LoadTexture(GetAssetPath(cfg.theme, "smallmark.png"));

            SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(periMark, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(apoMark, TEXTURE_FILTER_BILINEAR);
        }

        bool is_typing = IsUITyping();
        bool over_ui = IsMouseOverUI(&cfg);

        if (is_2d_view && !is_typing)
        {
            Camera2DParams.offset = (Vector2){GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f};
        }

        /* input handling */
        if (!is_typing)
        {
            if (IsKeyPressed(KEY_SPACE))
            {
                is_auto_warping = false;
                if (time_multiplier != 0.0)
                {
                    saved_multiplier = time_multiplier;
                    time_multiplier = 0.0;
                }
                else
                {
                    time_multiplier = saved_multiplier != 0.0 ? saved_multiplier : 1.0;
                }
            }
            
            static double warp_hold_start = 0.0;
            static double last_warp_step = 0.0;
            bool step_fwd = false;
            bool step_bwd = false;

            if (IsKeyPressed(KEY_PERIOD))
            {
                step_fwd = true;
                warp_hold_start = GetTime();
                last_warp_step = GetTime();
            }
            else if (IsKeyDown(KEY_PERIOD))
            {
                if (GetTime() - warp_hold_start > 0.4 && GetTime() - last_warp_step > 0.08)
                {
                    step_fwd = true;
                    last_warp_step = GetTime();
                }
            }

            if (IsKeyPressed(KEY_COMMA))
            {
                step_bwd = true;
                warp_hold_start = GetTime();
                last_warp_step = GetTime();
            }
            else if (IsKeyDown(KEY_COMMA))
            {
                if (GetTime() - warp_hold_start > 0.4 && GetTime() - last_warp_step > 0.08)
                {
                    step_bwd = true;
                    last_warp_step = GetTime();
                }
            }

            if (step_fwd)
            {
                is_auto_warping = false;
                time_multiplier = StepTimeMultiplier(time_multiplier, true);
            }
            if (step_bwd)
            {
                is_auto_warping = false;
                time_multiplier = StepTimeMultiplier(time_multiplier, false);
            }
            if (IsKeyPressed(KEY_M))
                is_2d_view = !is_2d_view;
            if (IsKeyPressed(KEY_RIGHT_BRACKET))
                ToggleTLEWarning();

            if (IsKeyPressed(KEY_C))
                cfg.show_clouds = !cfg.show_clouds;
            if (IsKeyPressed(KEY_N))
                cfg.show_night_lights = !cfg.show_night_lights;
            if (IsKeyPressed(KEY_L))
                cfg.show_markers = !cfg.show_markers;

            if (IsKeyPressed(KEY_HOME))
            {
                active_lock = LOCK_EARTH;
                target_camDistance = 10.0f;
                target_camAngleX = 0.785f;
                target_camAngleY = 0.5f;
                target_camera2d_zoom = 1.0f;
                target_camera2d_target = (Vector2){0.0f, 0.0f};
                Camera3DParams.fovy = 45.0f;
            }

            if (IsKeyPressed(KEY_SLASH))
            {
                is_auto_warping = false;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                    current_epoch = get_current_real_time_epoch();
                else
                {
                    time_multiplier = 1.0;
                    saved_multiplier = 1.0;
                }
            }

            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
                cfg.ui_scale += 0.1f;
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
                cfg.ui_scale -= 0.1f;
        }

        if (cfg.ui_scale < 0.5f)
            cfg.ui_scale = 0.5f;
        if (cfg.ui_scale > 4.0f)
            cfg.ui_scale = 4.0f;

        /* time warp logic for jumping to specific dates */
        if (is_auto_warping)
        {
            double diff_sec = (auto_warp_target - current_epoch) * 86400.0;
            double warp_dir = (auto_warp_initial_diff >= 0.0) ? 1.0 : -1.0;

            if (diff_sec * warp_dir <= 0.0)
            {
                current_epoch = auto_warp_target;
                time_multiplier = 1.0;
                saved_multiplier = 1.0;
                is_auto_warping = false;
            }
            else
            {
                double base_speed = fabs(auto_warp_initial_diff) / 2.0;
                double eased_speed = fmin(base_speed, fabs(diff_sec) * 3.0);
                if (eased_speed < 1.0)
                    eased_speed = 1.0;
                time_multiplier = eased_speed * warp_dir;

                if (fabs(diff_sec) <= fabs(time_multiplier) * GetFrameTime())
                {
                    current_epoch = auto_warp_target;
                    time_multiplier = 1.0;
                    saved_multiplier = 1.0;
                    is_auto_warping = false;
                }
            }
        }

        /* update time continuously for smooth visual interpolation */
        current_epoch += (GetFrameTime() * time_multiplier) / 86400.0;
        current_epoch = normalize_epoch(current_epoch);

        /* asynchronous orbit path updates to keep fps high */
        if (sat_count > 0)
        {
            int updates_per_frame = 50;
            for (int i = 0; i < updates_per_frame; i++)
            {
                if (satellites[current_update_idx].is_active)
                {
                    update_orbit_cache(&satellites[current_update_idx], current_epoch);
                }
                current_update_idx = (current_update_idx + 1) % sat_count;
            }
        }

        double current_unix = get_unix_from_epoch(current_epoch);

        /* update current positions of all active sats */
        int active_render_count = 0;
        for (int i = 0; i < sat_count; i++)
        {
            if (!satellites[i].is_active)
                continue;
            if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat)
                continue;
            satellites[i].current_pos = calculate_position(&satellites[i], current_unix);

            
            /* spaghetti is good, but orbital spaghetti isn't.
            sooooo if an orbital body ends up below 80% of earths radius, disable it because it's about to meet earth's theoritical singularity and get ejected at speeds higher than light speed. yeeeeeeeet*/
            if (Vector3Length(satellites[i].current_pos) < EARTH_RADIUS_KM * 0.8f)
            {
                satellites[i].is_active = false;
                if (selected_sat == &satellites[i])
                    selected_sat = NULL;
                continue;
            }

            active_render_count++;
        }

        int global_orbit_step = 1;
        if (active_render_count > 13000)
            global_orbit_step = 54;
        else if (active_render_count > 5000)
            global_orbit_step = 24;
        else if (active_render_count > 2000)
            global_orbit_step = 8;
        else if (active_render_count > 500)
            global_orbit_step = 4;
        else if (active_render_count > 200)
            global_orbit_step = 2;

        /* fading logic for selection isolation */
        bool should_hide = (hide_unselected && selected_sat != NULL);
        if (should_hide)
        {
            unselected_fade -= 3.0f * GetFrameTime();
            if (unselected_fade < 0.0f)
                unselected_fade = 0.0f;
        }
        else
        {
            unselected_fade += 3.0f * GetFrameTime();
            if (unselected_fade > 1.0f)
                unselected_fade = 1.0f;
        }

        char datetime_str[64];
        epoch_to_datetime_str(current_epoch, datetime_str);
        double gmst_deg = epoch_to_gmst(current_epoch);

        /* calculate moon orientation and position */
        Vector3 moon_pos_km = calculate_moon_position(current_epoch);
        Vector3 draw_moon_pos = Vector3Scale(moon_pos_km, 1.0f / DRAW_SCALE);
        float moon_mx, moon_my;
        get_map_coordinates(moon_pos_km, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &moon_mx, &moon_my);

        Vector3 dirToEarth = Vector3Normalize(Vector3Negate(draw_moon_pos));
        float moon_yaw = atan2f(-dirToEarth.z, dirToEarth.x);
        float moon_pitch = asinf(dirToEarth.y);
        moonModel.transform = MatrixMultiply(MatrixRotateZ(moon_pitch), MatrixRotateY(moon_yaw));

        Vector2 mouseDelta = GetMouseDelta();
        hovered_sat = NULL;

        /* vsync config check */
        if (cfg.hint_vsync != IsWindowState(FLAG_VSYNC_HINT))
        {
            if (cfg.hint_vsync) 
            {
                SetWindowState(FLAG_VSYNC_HINT);
                SetTargetFPS(0);
            }
            else 
            {
                ClearWindowState(FLAG_VSYNC_HINT);
                SetTargetFPS(cfg.target_fps);
            }
        }
        
        /* handle picking and camera in 2d mode */
        if (is_2d_view)
        {
            if (!over_ui)
            {
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && IsKeyDown(KEY_LEFT_SHIFT)))
                {
                    target_camera2d_target = Vector2Add(target_camera2d_target, Vector2Scale(mouseDelta, -1.0f / target_camera2d_zoom));
                    active_lock = LOCK_NONE;
                }
                float wheel = GetMouseWheelMove();
                if (wheel != 0 && !is_typing)
                {
                    target_camera2d_zoom += wheel * 0.1f * target_camera2d_zoom;
                    if (target_camera2d_zoom < 0.1f)
                        target_camera2d_zoom = 0.1f;
                    active_lock = LOCK_NONE;
                }
            }

            if (!is_typing)
            {
                float pan_speed = 800.0f * GetFrameTime() / target_camera2d_zoom;
                bool moved = false;
                if (IsKeyDown(KEY_RIGHT)) { target_camera2d_target.x += pan_speed; moved = true; }
                if (IsKeyDown(KEY_LEFT)) { target_camera2d_target.x -= pan_speed; moved = true; }
                if (IsKeyDown(KEY_DOWN)) { target_camera2d_target.y += pan_speed; moved = true; }
                if (IsKeyDown(KEY_UP)) { target_camera2d_target.y -= pan_speed; moved = true; }
                if (moved) active_lock = LOCK_NONE;
            }

            if (!over_ui)
            {
                Vector2 mousePos = GetMousePosition();
                float closest_dist = 9999.0f;
                float hit_radius_pixels = 12.0f * cfg.ui_scale;

                for (int i = 0; i < sat_count; i++)
                {
                    if (!satellites[i].is_active)
                        continue;
                    if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat)
                        continue;

                    float mx, my;
                    get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &mx, &my);

                    Vector2 screenPos = GetWorldToScreen2D((Vector2){mx, my}, Camera2DParams);
                    float dist = Vector2Distance(mousePos, screenPos);

                    if (dist < hit_radius_pixels && dist < closest_dist)
                    {
                        closest_dist = dist;
                        hovered_sat = &satellites[i];
                    }
                }
            }
        }
        else
        {
            /* handle picking and camera in 3d mode */
            if (!over_ui)
            {
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
                {
                    if (IsKeyDown(KEY_LEFT_SHIFT))
                    {
                        Vector3 forward = Vector3Normalize(Vector3Subtract(Camera3DParams.target, Camera3DParams.position));
                        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, Camera3DParams.up));
                        Vector3 upVector = Vector3Normalize(Vector3CrossProduct(right, forward));
                        float panSpeed = target_camDistance * 0.001f;
                        target_camera3d_target = Vector3Add(target_camera3d_target, Vector3Scale(right, -mouseDelta.x * panSpeed));
                        target_camera3d_target = Vector3Add(target_camera3d_target, Vector3Scale(upVector, mouseDelta.y * panSpeed));
                        active_lock = LOCK_NONE;
                    }
                    else
                    {
                        target_camAngleX -= mouseDelta.x * 0.005f;
                        target_camAngleY += mouseDelta.y * 0.005f;
                        if (target_camAngleY > 1.57f)
                            target_camAngleY = 1.57f;
                        if (target_camAngleY < -1.57f)
                            target_camAngleY = -1.57f;
                    }
                }
                if (!is_typing)
                {
                    float wheel = GetMouseWheelMove();
                    if (wheel != 0)
                    {
                        if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT) || (is_pov_mode && selected_sat && selected_sat->is_active))
                        {
                            Camera3DParams.fovy -= wheel * 5.0f;
                            if (Camera3DParams.fovy < 10.0f) Camera3DParams.fovy = 10.0f;
                            if (Camera3DParams.fovy > 120.0f) Camera3DParams.fovy = 120.0f;
                        }
                        else
                        {
                            target_camDistance -= wheel * (target_camDistance * 0.1f);
                            if (target_camDistance < draw_earth_radius + 1.0f)
                                target_camDistance = draw_earth_radius + 1.0f;
                        }
                    }
                }
            }

            if (!is_typing)
            {
                float rot_speed = 1.5f * GetFrameTime();
                bool moved = false;
                
                if (is_pov_mode && selected_sat && selected_sat->is_active)
                {
                    if (IsKeyDown(KEY_RIGHT)) { target_camAngleX -= rot_speed; moved = true; }
                    if (IsKeyDown(KEY_LEFT)) { target_camAngleX += rot_speed; moved = true; }
                }
                else
                {
                    if (IsKeyDown(KEY_RIGHT)) { target_camAngleX += rot_speed; moved = true; }
                    if (IsKeyDown(KEY_LEFT)) { target_camAngleX -= rot_speed; moved = true; }
                }

                if (IsKeyDown(KEY_UP)) { target_camAngleY += rot_speed; moved = true; }
                if (IsKeyDown(KEY_DOWN)) { target_camAngleY -= rot_speed; moved = true; }
                if (moved)
                {
                    if (target_camAngleY > 1.57f) target_camAngleY = 1.57f;
                    if (target_camAngleY < -1.57f) target_camAngleY = -1.57f;
                    active_lock = LOCK_NONE;
                }
            }

            if (!over_ui)
            {
                Ray mouseRay = GetMouseRay(GetMousePosition(), Camera3DParams);
                float closest_dist = 9999.0f;

                for (int i = 0; i < sat_count; i++)
                {
                    if (!satellites[i].is_active)
                        continue;
                    if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat)
                        continue;

                    Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                    if (Vector3DistanceSqr(Camera3DParams.target, draw_pos) > (camDistance * camDistance * 16.0f))
                        continue;

                    Vector3 to_sat = Vector3Subtract(draw_pos, Camera3DParams.position);
                    float distToCamSqr = Vector3LengthSqr(to_sat);

                    if (distToCamSqr > 0.00001f)
                    {
                        float proj = Vector3DotProduct(to_sat, mouseRay.direction);
                        if (proj > 0.0f) // Only check if in front of camera
                        {
                            Vector3 closest_on_ray = Vector3Scale(mouseRay.direction, proj);
                            float distToRaySqr = Vector3DistanceSqr(to_sat, closest_on_ray);
                            
                            float hit_radius_sqr = 0.000225f * distToCamSqr * (cfg.ui_scale * cfg.ui_scale);

                            if (distToRaySqr < hit_radius_sqr && proj < closest_dist)
                            {
                                closest_dist = proj;
                                hovered_sat = &satellites[i];
                            }
                        }
                    }
                }
            }
        }

        /* selection and double click for planet locking */
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (!over_ui)
            {
                if (picking_home)
                {
                    float lat, lon;
                    if (GetMouseEarthIntersection(GetMousePosition(), is_2d_view, Camera2DParams, Camera3DParams, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &lat, &lon))
                    {
                        home_location.lat = lat;
                        home_location.lon = lon;
                        home_location.alt = 0.0f;
                        picking_home = false; // exit pick mode after successful set
                    }
                    // if click is not on earth, do nothing
                }
                else
                {
                    // normal satellite selection
                    selected_sat = hovered_sat;
                    double current_time = GetTime();
                    if (current_time - last_left_click_time < 0.3)
                    {
                        // double‑click lock logic (unchanged)
                        if (is_2d_view)
                        {
                            Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), Camera2DParams);
                            bool hit_moon = false;
                            for (int offset_i = -1; offset_i <= 1; offset_i++)
                            {
                                float x_off = offset_i * map_w;
                                if (Vector2Distance(mouseWorld, (Vector2){moon_mx + x_off, moon_my}) < (15.0f * cfg.ui_scale / Camera2DParams.zoom))
                                {
                                    hit_moon = true;
                                    break;
                                }
                            }
                            active_lock = hit_moon ? LOCK_MOON : LOCK_EARTH;
                        }
                        else
                        {
                            Ray mouseRay = GetMouseRay(GetMousePosition(), Camera3DParams);
                            RayCollision earthCol = GetRayCollisionSphere(mouseRay, Vector3Zero(), draw_earth_radius);
                            RayCollision moonCol = GetRayCollisionSphere(mouseRay, draw_moon_pos, draw_moon_radius);
                            if (moonCol.hit && (!earthCol.hit || moonCol.distance < earthCol.distance))
                            {
                                active_lock = LOCK_MOON;
                            }
                            else if (earthCol.hit)
                            {
                                active_lock = LOCK_EARTH;
                            }
                        }
                    }
                    last_left_click_time = current_time;
                }
            }
        }

        /* update camera interpolation targets */
        if (active_lock == LOCK_EARTH)
        {
            if (is_2d_view)
                target_camera2d_target = Vector2Zero();
            else
                target_camera3d_target = Vector3Zero();
        }
        else if (active_lock == LOCK_MOON)
        {
            if (is_2d_view)
                target_camera2d_target = (Vector2){moon_mx, moon_my};
            else
                target_camera3d_target = draw_moon_pos;
        }

        float smooth_speed = 10.0f * GetFrameTime();
        if (smooth_speed > 1.0f) smooth_speed = 1.0f; // clamp that thang to prevent spinny explosions when alt tabbed

        Camera2DParams.zoom = Lerp(Camera2DParams.zoom, target_camera2d_zoom, smooth_speed);
        Camera2DParams.target = Vector2Lerp(Camera2DParams.target, target_camera2d_target, smooth_speed);

        camAngleX = Lerp(camAngleX, target_camAngleX, smooth_speed);
        camAngleY = Lerp(camAngleY, target_camAngleY, smooth_speed);
        camDistance = Lerp(camDistance, target_camDistance, smooth_speed);
        Camera3DParams.target = Vector3Lerp(Camera3DParams.target, target_camera3d_target, smooth_speed);

        float target_ecliptic_angle = is_ecliptic_frame ? (23.439f * DEG2RAD) : 0.0f;
        current_ecliptic_angle = Lerp(current_ecliptic_angle, target_ecliptic_angle, smooth_speed);

        if (!is_2d_view)
        {
            Vector3 offset = {
                camDistance * cosf(camAngleY) * sinf(camAngleX),
                camDistance * sinf(camAngleY),
                camDistance * cosf(camAngleY) * cosf(camAngleX)
            };
            Vector3 upVec = {0.0f, 1.0f, 0.0f};

            if (current_ecliptic_angle > 0.0001f)
            {
                Matrix rot = MatrixRotateX(current_ecliptic_angle);
                offset = Vector3Transform(offset, rot);
                upVec = Vector3Transform(upVec, rot);
            }

            Camera3DParams.position = Vector3Add(Camera3DParams.target, offset);
            Camera3DParams.up = upVec;
        }

        Satellite *active_sat = hovered_sat ? hovered_sat : selected_sat;

        if (!is_2d_view && is_pov_mode && selected_sat && selected_sat->is_active)
        {
            Vector3 sat_pos_3d = Vector3Scale(selected_sat->current_pos, 1.0f / DRAW_SCALE);
            Camera3DParams.position = sat_pos_3d;
            
            /* create an LVLH local coordinate frame */
            double t_unix = get_unix_from_epoch(current_epoch);
            Vector3 pos_next_3d = Vector3Scale(calculate_position(selected_sat, t_unix + 1.0), 1.0f / DRAW_SCALE);
            
            Vector3 nadir = Vector3Normalize(Vector3Negate(sat_pos_3d));
            Vector3 vel = Vector3Normalize(Vector3Subtract(pos_next_3d, sat_pos_3d));
            
            Vector3 right = Vector3Normalize(Vector3CrossProduct(vel, nadir));
            Vector3 fwd = Vector3Normalize(Vector3CrossProduct(nadir, right));
            Vector3 up = Vector3Negate(nadir);

            /* map user camera angles onto the local orbital frame */
            float cy = cosf(camAngleX);
            float sy = sinf(camAngleX);
            float cp = cosf(-camAngleY);
            float sp = sinf(-camAngleY);
            
            Vector3 local_look = { cp * sy, sp, cp * cy };
            Vector3 look_dir = Vector3Add(
                Vector3Add(Vector3Scale(right, local_look.x), Vector3Scale(up, local_look.y)),
                Vector3Scale(fwd, local_look.z)
            );
            
            Vector3 local_right = { cy, 0.0f, -sy };
            Vector3 world_right = Vector3Add(
                Vector3Scale(right, local_right.x),
                Vector3Scale(fwd, local_right.z)
            );
            
            /* up is down, down is up */
            Vector3 upVec = Vector3Normalize(Vector3CrossProduct(look_dir, world_right));

            if (current_ecliptic_angle > 0.0001f)
            {
                Matrix rot = MatrixRotateX(current_ecliptic_angle);
                look_dir = Vector3Transform(look_dir, rot);
                upVec = Vector3Transform(upVec, rot);
            }

            Camera3DParams.target = Vector3Add(sat_pos_3d, look_dir);
            Camera3DParams.up = upVec;
        }

/* calculate radio footprint (visibility cone) */
#define FP_RINGS 12
#define FP_PTS 120
        Vector3 fp_grid[FP_RINGS + 1][FP_PTS];
        bool has_footprint = false;

        if (active_sat && active_sat->is_active)
        {
            float r = Vector3Length(active_sat->current_pos);
            if (r > EARTH_RADIUS_KM)
            {
                has_footprint = true;
                float theta = acosf(EARTH_RADIUS_KM / r);
                Vector3 s_norm = Vector3Normalize(active_sat->current_pos);
                Vector3 up = fabsf(s_norm.y) > 0.99f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
                Vector3 u = Vector3Normalize(Vector3CrossProduct(up, s_norm));
                Vector3 v = Vector3CrossProduct(s_norm, u);

                for (int i = 0; i <= FP_RINGS; i++)
                {
                    float a = theta * ((float)i / FP_RINGS);
                    float d_plane = EARTH_RADIUS_KM * cosf(a), r_circle = EARTH_RADIUS_KM * sinf(a);
                    for (int k = 0; k < FP_PTS; k++)
                    {
                        float alpha = (2.0f * PI * k) / FP_PTS;
                        fp_grid[i][k] = Vector3Add(Vector3Scale(s_norm, d_plane), Vector3Add(Vector3Scale(u, cosf(alpha) * r_circle), Vector3Scale(v, sinf(alpha) * r_circle)));
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground(cfg.bg_color);

        float m_size_2d = 24.0f * cfg.ui_scale / Camera2DParams.zoom;
        float m_text_2d = 16.0f * cfg.ui_scale / Camera2DParams.zoom;
        float mark_size_2d = 32.0f * cfg.ui_scale / Camera2DParams.zoom;

        /* 2d projection rendering */
        if (is_2d_view)
        {
            BeginMode2D(Camera2DParams);
            if (cfg.show_night_lights)
            {
                BeginShaderMode(shader2D);
                SetShaderValueTexture(shader2D, nightTexLoc2D, earthNightTexture);

                Vector3 sunEci = calculate_sun_position(current_epoch);
                float earth_rot_rad = (gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                Vector3 sunEcef = Vector3Transform(sunEci, MatrixRotateY(-earth_rot_rad));
                Vector3 moonEcef = Vector3Transform(draw_moon_pos, MatrixRotateY(-earth_rot_rad));

                SetShaderValue(shader2D, sunDirLoc2D, &sunEcef, SHADER_UNIFORM_VEC3);
                SetShaderValue(shader2D, moonPosLoc2D, &moonEcef, SHADER_UNIFORM_VEC3);
            }

            DrawTexturePro(earthTexture, (Rectangle){0, 0, earthTexture.width, earthTexture.height}, (Rectangle){-map_w / 2, -map_h / 2, map_w, map_h}, (Vector2){0, 0}, 0.0f, WHITE);

            if (cfg.show_night_lights)
                EndShaderMode();

            /* scissor mode for map boundaries */
            Vector2 mapMin = GetWorldToScreen2D((Vector2){-map_w / 2.0f, -map_h / 2.0f}, Camera2DParams);
            Vector2 mapMax = GetWorldToScreen2D((Vector2){map_w / 2.0f, map_h / 2.0f}, Camera2DParams);

            int sc_x = (int)mapMin.x, sc_y = (int)mapMin.y;
            int sc_w = (int)(mapMax.x - mapMin.x), sc_h = (int)(mapMax.y - mapMin.y);

            if (sc_x < 0)
            {
                sc_w += sc_x;
                sc_x = 0;
            }
            if (sc_y < 0)
            {
                sc_h += sc_y;
                sc_y = 0;
            }
            if (sc_x + sc_w > GetScreenWidth())
                sc_w = GetScreenWidth() - sc_x;
            if (sc_y + sc_h > GetScreenHeight())
                sc_h = GetScreenHeight() - sc_y;

            if (sc_w > 0 && sc_h > 0)
            {
                BeginScissorMode(sc_x, sc_y, sc_w, sc_h);

                /* draw 2d footprint */
                if (active_sat && has_footprint && active_sat->is_active && !(is_pov_mode && selected_sat != NULL))
                {
                    for (int i = 0; i < FP_RINGS; i++)
                    {
                        for (int k = 0; k < FP_PTS; k++)
                        {
                            int next = (k + 1) % FP_PTS;
                            float x1, y1, x2, y2, x3, y3, x4, y4;
                            get_map_coordinates(fp_grid[i][k], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x1, &y1);
                            get_map_coordinates(fp_grid[i][next], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x2, &y2);
                            get_map_coordinates(fp_grid[i + 1][k], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x3, &y3);
                            get_map_coordinates(fp_grid[i + 1][next], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x4, &y4);

                            if (x2 - x1 > map_w * 0.6f)
                                x2 -= map_w;
                            else if (x2 - x1 < -map_w * 0.6f)
                                x2 += map_w;
                            if (x3 - x1 > map_w * 0.6f)
                                x3 -= map_w;
                            else if (x3 - x1 < -map_w * 0.6f)
                                x3 += map_w;
                            if (x4 - x1 > map_w * 0.6f)
                                x4 -= map_w;
                            else if (x4 - x1 < -map_w * 0.6f)
                                x4 += map_w;

                            for (int offset_i = -1; offset_i <= 1; offset_i++)
                            {
                                float x_off = offset_i * map_w;
                                DrawTriangle((Vector2){x1 + x_off, y1}, (Vector2){x3 + x_off, y3}, (Vector2){x2 + x_off, y2}, cfg.footprint_bg);
                                DrawTriangle((Vector2){x2 + x_off, y2}, (Vector2){x3 + x_off, y3}, (Vector2){x4 + x_off, y4}, cfg.footprint_bg);
                            }
                        }
                    }
                    for (int k = 0; k < FP_PTS; k++)
                    {
                        int next = (k + 1) % FP_PTS;
                        float x1, y1, x2, y2;
                        get_map_coordinates(fp_grid[FP_RINGS][k], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x1, &y1);
                        get_map_coordinates(fp_grid[FP_RINGS][next], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x2, &y2);
                        if (x2 - x1 > map_w * 0.6f)
                            x2 -= map_w;
                        else if (x2 - x1 < -map_w * 0.6f)
                            x2 += map_w;
                        for (int offset_i = -1; offset_i <= 1; offset_i++)
                        {
                            if (fabs(x2 - x1) < map_w * 0.6f)
                            {
                                DrawLineEx((Vector2){x1 + offset_i * map_w, y1}, (Vector2){x2 + offset_i * map_w, y2}, 2.0f / Camera2DParams.zoom, cfg.footprint_border);
                            }
                        }
                    }
                }

                /* render all sats on 2d map */
                for (int i = 0; i < sat_count; i++)
                {
                    if (!satellites[i].is_active)
                        continue;
                    bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                    float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                    if (sat_alpha <= 0.0f)
                        continue;

                    bool is_hl = (active_sat == &satellites[i]);
                    Color sCol = (selected_sat == &satellites[i]) ? cfg.sat_selected : (hovered_sat == &satellites[i]) ? cfg.sat_highlighted : cfg.sat_normal;
                    sCol = ApplyAlpha(sCol, sat_alpha);

                    if (is_hl && !(is_pov_mode && &satellites[i] == selected_sat))
                    {
                        int segments = fmin(4000, fmax(50, (int)(400 * cfg.orbits_to_draw)));
                        Vector2 track_pts[4001];
                        bool is_sunlit_arr[4001];

                        double period_days = (2.0 * PI / satellites[i].mean_motion) / 86400.0;
                        double time_step = (period_days * cfg.orbits_to_draw) / segments;

                        Vector3 base_sun_dir = {0};
                        if (cfg.highlight_sunlit)
                        {
                            base_sun_dir = Vector3Normalize(calculate_sun_position(current_epoch));
                        }

                        for (int j = 0; j <= segments; j++)
                        {
                            double t = (j == 0) ? current_epoch : (current_epoch - fmod(current_epoch, time_step) + (j * time_step));
                            double t_unix = get_unix_from_epoch(t);
                            Vector3 raw_pos = calculate_position(&satellites[i], t_unix);
                            get_map_coordinates(raw_pos, epoch_to_gmst(t), cfg.earth_rotation_offset, map_w, map_h, &track_pts[j].x, &track_pts[j].y);

                            if (cfg.highlight_sunlit)
                            {
                                is_sunlit_arr[j] = !is_sat_eclipsed(raw_pos, base_sun_dir);
                            }
                        }

                        for (int offset_i = -1; offset_i <= 1; offset_i++)
                        {
                            float x_off = offset_i * map_w;
                            for (int j = 1; j <= segments; j++)
                            {
                                if (fabs(track_pts[j].x - track_pts[j - 1].x) < map_w * 0.6f)
                                {
                                    Color drawCol = ApplyAlpha(cfg.orbit_highlighted, sat_alpha);
                                    if (cfg.highlight_sunlit)
                                    {
                                        if (is_sunlit_arr[j])
                                            drawCol = ApplyAlpha(cfg.sat_highlighted, sat_alpha);
                                        else
                                            drawCol = ApplyAlpha(cfg.orbit_normal, sat_alpha);
                                    }
                                    DrawLineEx((Vector2){track_pts[j - 1].x + x_off, track_pts[j - 1].y}, (Vector2){track_pts[j].x + x_off, track_pts[j].y}, 2.0f / Camera2DParams.zoom, drawCol);
                                }
                            }

                            Vector2 peri2d, apo2d;
                            get_apsis_2d(&satellites[i], current_epoch, false, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &peri2d);
                            get_apsis_2d(&satellites[i], current_epoch, true, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &apo2d);

                            DrawTexturePro(
                                periMark, (Rectangle){0, 0, periMark.width, periMark.height}, (Rectangle){peri2d.x + x_off, peri2d.y, mark_size_2d, mark_size_2d},
                                (Vector2){mark_size_2d / 2.f, mark_size_2d / 2.f}, 0.0f, ApplyAlpha(cfg.periapsis, sat_alpha)
                            );
                            DrawTexturePro(
                                apoMark, (Rectangle){0, 0, apoMark.width, apoMark.height}, (Rectangle){apo2d.x + x_off, apo2d.y, mark_size_2d, mark_size_2d},
                                (Vector2){mark_size_2d / 2.f, mark_size_2d / 2.f}, 0.0f, ApplyAlpha(cfg.apoapsis, sat_alpha)
                            );
                        }
                    }

                    float sat_mx, sat_my;
                    get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sat_mx, &sat_my);
                    if (!(is_pov_mode && &satellites[i] == selected_sat))
                    {
                        for (int offset_i = -1; offset_i <= 1; offset_i++)
                        {
                            DrawTexturePro(
                                satIcon, (Rectangle){0, 0, satIcon.width, satIcon.height}, (Rectangle){sat_mx + (offset_i * map_w), sat_my, m_size_2d, m_size_2d},
                                (Vector2){m_size_2d / 2.f, m_size_2d / 2.f}, 0.0f, sCol
                            );

                            if (is_hl && Camera2DParams.zoom > 0.1f)
                            {
                                DrawUIText(customFont, satellites[i].name, sat_mx + (offset_i * map_w) + (m_size_2d / 2.f) + 4.f, sat_my - (m_size_2d / 2.f), m_text_2d, sCol);
                            }
                        }
                    }
                }

                /* ground station markers */
                float hx = (home_location.lon / 360.0f) * map_w;
                float hy = -(home_location.lat / 180.0f) * map_h;
                for (int offset_i = -1; offset_i <= 1; offset_i++)
                {
                    float x_off = offset_i * map_w;
                    DrawTexturePro(
                        markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){hx + x_off, hy, m_size_2d, m_size_2d}, (Vector2){m_size_2d / 2.f, m_size_2d / 2.f}, 0.0f, WHITE
                    );

                    if (Camera2DParams.zoom > 0.1f)
                    {
                        DrawUIText(customFont, home_location.name, hx + x_off + (m_size_2d / 2.f) + 4.f, hy - (m_size_2d / 2.f), m_text_2d, WHITE);
                    }
                }

                /* slant range overlay 2d */
                if (cfg.show_slant_range && active_sat && active_sat->is_active)
                {
                    float sx, sy;
                    get_map_coordinates(active_sat->current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sx, &sy);

                    if (sx - hx > map_w / 2.0f)
                        sx -= map_w;
                    else if (hx - sx > map_w / 2.0f)
                        sx += map_w;

                    double range = get_sat_range(active_sat, current_epoch, home_location);

                    for (int offset_i = -1; offset_i <= 1; offset_i++)
                    {
                        float x_off = offset_i * map_w;
                        Vector2 p1 = {hx + x_off, hy};
                        Vector2 p2 = {sx + x_off, sy};
                        DrawLineEx(p1, p2, 2.0f / Camera2DParams.zoom, ApplyAlpha(cfg.ui_accent, 0.8f));

                        if (Camera2DParams.zoom > 0.1f)
                        {
                            Vector2 mid = {(p1.x + p2.x) / 2.0f, (p1.y + p2.y) / 2.0f};
                            char rng_str[32];
                            TextCopy(rng_str, TextFormat("%.1f km", range));
                            Vector2 tSize = MeasureTextEx(customFont, rng_str, m_text_2d, 1.0f);

                            DrawRectangle(
                                mid.x - tSize.x / 2.0f - 2.0f / Camera2DParams.zoom, mid.y - tSize.y / 2.0f - 2.0f / Camera2DParams.zoom, tSize.x + 4.0f / Camera2DParams.zoom,
                                tSize.y + 4.0f / Camera2DParams.zoom, ApplyAlpha(cfg.ui_bg, 0.7f)
                            );
                            DrawUIText(customFont, rng_str, mid.x - tSize.x / 2.0f, mid.y - tSize.y / 2.0f, m_text_2d, cfg.ui_accent);
                        }
                    }
                }

                if (cfg.show_markers)
                {
                    for (int m = 0; m < marker_count; m++)
                    {
                        float mx = (markers[m].lon / 360.0f) * map_w;
                        float my = -(markers[m].lat / 180.0f) * map_h;
                        for (int offset_i = -1; offset_i <= 1; offset_i++)
                        {
                            float x_off = offset_i * map_w;
                            DrawTexturePro(
                                markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){mx + x_off, my, m_size_2d, m_size_2d}, (Vector2){m_size_2d / 2.f, m_size_2d / 2.f},
                                0.0f, WHITE
                            );

                            if (Camera2DParams.zoom > 0.1f)
                            {
                                DrawUIText(customFont, markers[m].name, mx + x_off + (m_size_2d / 2.f) + 4.f, my - (m_size_2d / 2.f), m_text_2d, WHITE);
                            }
                        }
                    }
                }

                EndScissorMode();
            }

            if (picking_home)
            {
                float lat, lon;
                if (GetMouseEarthIntersection(GetMousePosition(), true, Camera2DParams, Camera3DParams, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &lat, &lon))
                {
                    float mx = (lon / 360.0f) * map_w;
                    float my = -(lat / 180.0f) * map_h;
                    for (int offset_i = -1; offset_i <= 1; offset_i++)
                    {
                        float x_off = offset_i * map_w;
                        DrawTexturePro(
                            markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){mx + x_off, my, m_size_2d, m_size_2d}, (Vector2){m_size_2d / 2.f, m_size_2d / 2.f}, 0.0f,
                            (Color){0, 255, 255, 255}
                        );
                    }
                }
            }

            EndMode2D();
        }
        else
        {
            /* 3d globe rendering */
        BeginMode3D(Camera3DParams);
        
        if (cfg.show_skybox)
        {
            DrawModel(skyboxModel, Camera3DParams.position, 1.0f, WHITE);
        }

        float earth_rot_rad = (gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
        Vector3 sunEci = calculate_sun_position(current_epoch);
        Vector3 sunEcef = Vector3Transform(sunEci, MatrixRotateY(-earth_rot_rad));
        Vector3 moonEcef = Vector3Transform(draw_moon_pos, MatrixRotateY(-earth_rot_rad));
        Vector3 viewEcef = Vector3Transform(Camera3DParams.position, MatrixRotateY(-earth_rot_rad));
        
        earthModel.transform = MatrixRotateY(earth_rot_rad);

        if (cfg.show_night_lights)
        {
            earthModel.materials[0].shader = shader3D;
            SetShaderValue(shader3D, sunDirLoc3D, &sunEcef, SHADER_UNIFORM_VEC3);
            SetShaderValue(shader3D, moonPosLoc3D, &moonEcef, SHADER_UNIFORM_VEC3);
            SetShaderValue(shader3D, viewPosLoc3D, &viewEcef, SHADER_UNIFORM_VEC3);
        }
        else
        {
            earthModel.materials[0].shader = defaultEarthShader;
        }

        DrawModel(earthModel, Vector3Zero(), 1.0f, WHITE);

        /* atmosphere/cloud layer */
        if (cfg.show_clouds)
        {
            double continuous_cloud_angle = fmod(gmst_deg + cfg.earth_rotation_offset + (current_epoch * 360.0 * 0.04), 360.0);
            float cloud_rot_rad = (float)(continuous_cloud_angle * DEG2RAD);
            cloudModel.transform = MatrixRotateY(cloud_rot_rad);

            if (cfg.show_night_lights)
            {
                cloudModel.materials[0].shader = shaderCloud;
                Vector3 sunEci = calculate_sun_position(current_epoch);
                Vector3 sunCloudSpace = Vector3Transform(sunEci, MatrixRotateY(-cloud_rot_rad));
                Vector3 moonCloudSpace = Vector3Transform(draw_moon_pos, MatrixRotateY(-cloud_rot_rad));

                SetShaderValue(shaderCloud, sunDirLocCloud, &sunCloudSpace, SHADER_UNIFORM_VEC3);
                SetShaderValue(shaderCloud, moonPosLocCloud, &moonCloudSpace, SHADER_UNIFORM_VEC3);
            }
            else
            {
                cloudModel.materials[0].shader = defaultCloudShader;
            }

            DrawModel(cloudModel, Vector3Zero(), 1.0f, WHITE);
        }

        /* atmospheric layer rendering */
        if (cfg.show_scattering)
        {
            atmosphereModel.transform = MatrixRotateY(earth_rot_rad);
            SetShaderValue(shaderAtmosphere, sunDirLocAtmosphere, &sunEcef, SHADER_UNIFORM_VEC3);
            SetShaderValue(shaderAtmosphere, viewPosLocAtmosphere, &viewEcef, SHADER_UNIFORM_VEC3);
            DrawModel(atmosphereModel, Vector3Zero(), 1.0f, WHITE);
        }

        Vector3 sunDirWorld = Vector3Normalize(calculate_sun_position(current_epoch));
            SetShaderValue(shaderMoon, sunDirLocMoon, &sunDirWorld, SHADER_UNIFORM_VEC3);
            SetShaderValue(shaderMoon, moonPosLocMoon, &draw_moon_pos, SHADER_UNIFORM_VEC3);
            SetShaderValueMatrix(shaderMoon, moonRotLocMoon, moonModel.transform);

            DrawModel(moonModel, draw_moon_pos, 1.0f, WHITE);

            /* draw the sun on the skybox */
            float sun_dist = 800.0f;
            float sun_radius = sun_dist * tanf((0.15f / 2.0f) * DEG2RAD);
            Vector3 sun_pos_3d = Vector3Add(Camera3DParams.position, Vector3Scale(sunDirWorld, sun_dist));
            
            DrawSphere(sun_pos_3d, sun_radius * 2.5f, ApplyAlpha((Color){ 255, 230, 100, 255 }, 0.15f));
            DrawSphere(sun_pos_3d, sun_radius, (Color){ 255, 250, 180, 255 });

            /* 3d footprint triangles */
            if (active_sat && has_footprint && active_sat->is_active && !(is_pov_mode && selected_sat != NULL))
            {
                for (int i = 0; i < FP_RINGS; i++)
                {
                    for (int k = 0; k < FP_PTS; k++)
                    {
                        int next = (k + 1) % FP_PTS;
                        Vector3 p1 = Vector3Scale(fp_grid[i][k], 1.02f / DRAW_SCALE), p2 = Vector3Scale(fp_grid[i][next], 1.02f / DRAW_SCALE);
                        Vector3 p3 = Vector3Scale(fp_grid[i + 1][k], 1.02f / DRAW_SCALE), p4 = Vector3Scale(fp_grid[i + 1][next], 1.02f / DRAW_SCALE);
                        DrawTriangle3D(p1, p3, p2, cfg.footprint_bg);
                        DrawTriangle3D(p2, p3, p4, cfg.footprint_bg);
                    }
                }
                for (int k = 0; k < FP_PTS; k++)
                {
                    int next = (k + 1) % FP_PTS;
                    DrawLine3D(Vector3Scale(fp_grid[FP_RINGS][k], 1.02f / DRAW_SCALE), Vector3Scale(fp_grid[FP_RINGS][next], 1.02f / DRAW_SCALE), cfg.footprint_border);
                }
            }

            for (int i = 0; i < sat_count; i++)
            {
                if (!satellites[i].is_active)
                    continue;
                bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                if (sat_alpha <= 0.0f)
                    continue;

                bool is_hl = (active_sat == &satellites[i]);
                if (!(is_pov_mode && &satellites[i] == selected_sat))
                {
                    draw_orbit_3d(&satellites[i], current_epoch, is_hl, sat_alpha, global_orbit_step);
                }

                if (is_hl && !(is_pov_mode && &satellites[i] == selected_sat))
                {
                    Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                    DrawLine3D(Vector3Zero(), draw_pos, ApplyAlpha(cfg.orbit_highlighted, sat_alpha));
                }
            }

            /* slant range overlay 3d line */
            if (cfg.show_slant_range && active_sat && active_sat->is_active)
            {
                float h_lat_rad = home_location.lat * DEG2RAD;
                float h_lon_rad = (home_location.lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                Vector3 h_pos3d = {cosf(h_lat_rad) * cosf(h_lon_rad) * draw_earth_radius, sinf(h_lat_rad) * draw_earth_radius, -cosf(h_lat_rad) * sinf(h_lon_rad) * draw_earth_radius};
                Vector3 s_pos3d = Vector3Scale(active_sat->current_pos, 1.0f / DRAW_SCALE);
                DrawLine3D(h_pos3d, s_pos3d, ApplyAlpha(cfg.ui_accent, 0.6f));
            }

            if (show_scope)
            {
                float h_lat_rad = home_location.lat * DEG2RAD;
                float h_lon_rad = (home_location.lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                
                Vector3 h_pos3d = {
                    cosf(h_lat_rad) * cosf(h_lon_rad) * draw_earth_radius, 
                    sinf(h_lat_rad) * draw_earth_radius, 
                    -cosf(h_lat_rad) * sinf(h_lon_rad) * draw_earth_radius
                };

                Vector3 up = Vector3Normalize(h_pos3d);
                Vector3 east = {-sinf(h_lon_rad), 0.0f, -cosf(h_lon_rad)};
                Vector3 north = {-cosf(h_lon_rad) * sinf(h_lat_rad), cosf(h_lat_rad), sinf(h_lon_rad) * sinf(h_lat_rad)};

                float el_rad = scope_el * DEG2RAD;
                float az_rad = scope_az * DEG2RAD;

                Vector3 dir = Vector3Add(
                    Vector3Add(Vector3Scale(north, cosf(el_rad) * cosf(az_rad)), 
                               Vector3Scale(east, cosf(el_rad) * sinf(az_rad))),
                    Vector3Scale(up, sinf(el_rad))
                );
                dir = Vector3Normalize(dir);

                /* extend out to GEO-ish distance */
                float cone_length = 25000.0f / DRAW_SCALE; 
                float cone_radius = cone_length * tanf((scope_beam / 2.0f) * DEG2RAD);
                Vector3 center_end = Vector3Add(h_pos3d, Vector3Scale(dir, cone_length));

                Vector3 perp1 = Vector3CrossProduct(dir, up);
                if (Vector3Length(perp1) < 0.01f) perp1 = Vector3CrossProduct(dir, north);
                perp1 = Vector3Normalize(perp1);
                Vector3 perp2 = Vector3CrossProduct(dir, perp1);

                Color lineCol = ApplyAlpha(cfg.ui_accent, 0.4f);

                for (int i = 0; i < 4; i++) {
                    /* calculate 4 corners at 45, 135, 225, 315 degrees */
                    float angle = (i * PI / 2.0f) + (PI / 4.0f); 
                    Vector3 pt = Vector3Add(center_end, 
                                    Vector3Add(Vector3Scale(perp1, cosf(angle) * cone_radius),
                                               Vector3Scale(perp2, sinf(angle) * cone_radius)));
                    DrawLine3D(h_pos3d, pt, lineCol);
                }
            }

            EndMode3D();

            /* screen-space icons/text for 3d objects */
            float m_size_3d = 24.0f * cfg.ui_scale;
            float m_text_3d = 16.0f * cfg.ui_scale;
            float mark_size_3d = 32.0f * cfg.ui_scale;

            Vector3 camForward = Vector3Normalize(Vector3Subtract(Camera3DParams.target, Camera3DParams.position));

            /* slant range text overlay 3d */
            if (cfg.show_slant_range && active_sat && active_sat->is_active)
            {
                float h_lat_rad = home_location.lat * DEG2RAD;
                float h_lon_rad = (home_location.lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                Vector3 h_pos3d = {cosf(h_lat_rad) * cosf(h_lon_rad) * draw_earth_radius, sinf(h_lat_rad) * draw_earth_radius, -cosf(h_lat_rad) * sinf(h_lon_rad) * draw_earth_radius};
                Vector3 s_pos3d = Vector3Scale(active_sat->current_pos, 1.0f / DRAW_SCALE);

                Vector3 mid_pos = Vector3Lerp(h_pos3d, s_pos3d, 0.5f);
                Vector3 toMid = Vector3Subtract(mid_pos, Camera3DParams.position);

                if (Vector3DotProduct(Vector3Normalize(toMid), camForward) > 0.0f)
                {
                    Vector2 mid_screen = GetWorldToScreen(mid_pos, Camera3DParams);
                    double range = get_sat_range(active_sat, current_epoch, home_location);
                    char rng_str[32];
                    TextCopy(rng_str, TextFormat("%.1f km", range));
                    Vector2 tSize = MeasureTextEx(customFont, rng_str, m_text_3d, 1.0f);

                    DrawRectangle(mid_screen.x - tSize.x / 2.0f - 4, mid_screen.y - tSize.y / 2.0f - 4, tSize.x + 8, tSize.y + 8, ApplyAlpha(cfg.ui_bg, 0.7f));
                    DrawUIText(customFont, rng_str, mid_screen.x - tSize.x / 2.0f, mid_screen.y - tSize.y / 2.0f, m_text_3d, cfg.ui_accent);
                }
            }

            bool hide_apsis = (is_pov_mode && selected_sat != NULL && active_sat == selected_sat);
            if (active_sat && active_sat->is_active && !hide_apsis)
            {
                bool is_unselected = (selected_sat != NULL && active_sat != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;

                double t_peri_unix, t_apo_unix;
                get_apsis_times(active_sat, current_epoch, &t_peri_unix, &t_apo_unix);

                Vector3 draw_p = Vector3Scale(calculate_position(active_sat, t_peri_unix), 1.0f / DRAW_SCALE);
                Vector3 draw_a = Vector3Scale(calculate_position(active_sat, t_apo_unix), 1.0f / DRAW_SCALE);

                if (!IsOccludedByEarth(Camera3DParams.position, draw_p, draw_earth_radius))
                {
                    Vector2 sp = GetWorldToScreen(draw_p, Camera3DParams);
                    DrawTexturePro(
                        periMark, (Rectangle){0, 0, periMark.width, periMark.height}, (Rectangle){sp.x, sp.y, mark_size_3d, mark_size_3d}, (Vector2){mark_size_3d / 2.f, mark_size_3d / 2.f}, 0.0f,
                        ApplyAlpha(cfg.periapsis, sat_alpha)
                    );
                }
                if (!IsOccludedByEarth(Camera3DParams.position, draw_a, draw_earth_radius))
                {
                    Vector2 sp = GetWorldToScreen(draw_a, Camera3DParams);
                    DrawTexturePro(
                        apoMark, (Rectangle){0, 0, apoMark.width, apoMark.height}, (Rectangle){sp.x, sp.y, mark_size_3d, mark_size_3d}, (Vector2){mark_size_3d / 2.f, mark_size_3d / 2.f}, 0.0f,
                        ApplyAlpha(cfg.apoapsis, sat_alpha)
                    );
                }
            }

            for (int i = 0; i < sat_count; i++)
            {
                if (!satellites[i].is_active)
                    continue;
                bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                if (sat_alpha <= 0.0f)
                    continue;

                Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                Vector3 toTarget = Vector3Subtract(draw_pos, Camera3DParams.position);

                if (Vector3DotProduct(toTarget, camForward) > 0.0f && !IsOccludedByEarth(Camera3DParams.position, draw_pos, draw_earth_radius))
                {
                    if (!(is_pov_mode && &satellites[i] == selected_sat))
                    {
                        bool is_hl = (active_sat == &satellites[i]);
                        Color sCol = (selected_sat == &satellites[i]) ? cfg.sat_selected : (hovered_sat == &satellites[i]) ? cfg.sat_highlighted : cfg.sat_normal;
                        sCol = ApplyAlpha(sCol, sat_alpha);
                        Vector2 sp = GetWorldToScreen(draw_pos, Camera3DParams);
                        DrawTexturePro(satIcon, (Rectangle){0, 0, satIcon.width, satIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f, sCol);

                        if (is_hl)
                        {
                            DrawUIText(customFont, satellites[i].name, sp.x + (m_size_3d / 2.f) + 4.f, sp.y - (m_size_3d / 2.f), m_text_3d, sCol);
                        }
                    }
                }
            }

            float h_lat_rad = home_location.lat * DEG2RAD, h_lon_rad = (home_location.lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
            Vector3 h_pos = {cosf(h_lat_rad) * cosf(h_lon_rad) * draw_earth_radius, sinf(h_lat_rad) * draw_earth_radius, -cosf(h_lat_rad) * sinf(h_lon_rad) * draw_earth_radius};
            Vector3 h_normal = Vector3Normalize(h_pos);
            Vector3 h_viewDir = Vector3Normalize(Vector3Subtract(Camera3DParams.position, h_pos));
            Vector3 h_toTarget = Vector3Subtract(h_pos, Camera3DParams.position);

            if (Vector3DotProduct(h_normal, h_viewDir) > 0.0f && Vector3DotProduct(h_toTarget, camForward) > 0.0f)
            {
                Vector2 sp = GetWorldToScreen(h_pos, Camera3DParams);
                DrawTexturePro(
                    markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f, WHITE
                );

                if (camDistance < 50.0f)
                {
                    DrawUIText(customFont, home_location.name, sp.x + (m_size_3d / 2.f) + 4.f, sp.y - (m_size_3d / 2.f), m_text_3d, WHITE);
                }
            }

            if (cfg.show_markers)
            {
                for (int m = 0; m < marker_count; m++)
                {
                    float lat_rad = markers[m].lat * DEG2RAD, lon_rad = (markers[m].lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                    Vector3 m_pos = {cosf(lat_rad) * cosf(lon_rad) * draw_earth_radius, sinf(lat_rad) * draw_earth_radius, -cosf(lat_rad) * sinf(lon_rad) * draw_earth_radius};
                    Vector3 normal = Vector3Normalize(m_pos);
                    Vector3 viewDir = Vector3Normalize(Vector3Subtract(Camera3DParams.position, m_pos));
                    Vector3 toTarget = Vector3Subtract(m_pos, Camera3DParams.position);

                    if (Vector3DotProduct(normal, viewDir) > 0.0f && Vector3DotProduct(toTarget, camForward) > 0.0f)
                    {
                        Vector2 sp = GetWorldToScreen(m_pos, Camera3DParams);
                        DrawTexturePro(
                            markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f, WHITE
                        );

                        if (camDistance < 50.0f)
                        {
                            DrawUIText(customFont, markers[m].name, sp.x + (m_size_3d / 2.f) + 4.f, sp.y - (m_size_3d / 2.f), m_text_3d, WHITE);
                        }
                    }
                }
            }

            if (picking_home)
            {
                float lat, lon;
                if (GetMouseEarthIntersection(GetMousePosition(), false, Camera2DParams, Camera3DParams, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &lat, &lon))
                {
                    float lon_rad = (lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                    float lat_rad = lat * DEG2RAD;
                    Vector3 pos = {cosf(lat_rad) * cosf(lon_rad) * draw_earth_radius, sinf(lat_rad) * draw_earth_radius, -cosf(lat_rad) * sinf(lon_rad) * draw_earth_radius};
                    Vector2 sp = GetWorldToScreen(pos, Camera3DParams);
                    DrawTexturePro(
                        markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f,
                        (Color){0, 255, 255, 255}
                    );
                }
            }
        }

        /* ui overlay rendering */
        UIContext uiCtx = {
            .exit_app = &exit_app,
            .current_epoch = &current_epoch,
            .time_multiplier = &time_multiplier,
            .saved_multiplier = &saved_multiplier,
            .is_auto_warping = &is_auto_warping,
            .auto_warp_target = &auto_warp_target,
            .auto_warp_initial_diff = &auto_warp_initial_diff,
            .is_2d_view = &is_2d_view,
            .hide_unselected = &hide_unselected,
            .picking_home = &picking_home,
            .is_ecliptic_frame = &is_ecliptic_frame,
            .is_pov_mode = &is_pov_mode,
            .show_scope = &show_scope,
            .scope_az = &scope_az,
            .scope_el = &scope_el,
            .scope_beam = &scope_beam,
            .selected_sat = &selected_sat,
            .hovered_sat = hovered_sat,
            .active_sat = active_sat,
            .active_lock = &active_lock,
            .datetime_str = datetime_str,
            .gmst_deg = gmst_deg,
            .map_w = map_w,
            .map_h = map_h,
            .camera2d = &Camera2DParams,
            .camera3d = &Camera3DParams
        };
        DrawGUI(&uiCtx, &cfg, customFont);

        EndDrawing();
    }

    /* cleanup and save*/
    UnloadTexture(logoTex);
    UnloadTexture(satIcon);
    UnloadTexture(markerIcon);
    UnloadTexture(periMark);
    UnloadTexture(apoMark);
    UnloadTexture(earthTexture);
    UnloadTexture(earthNightTexture);
    UnloadTexture(skyboxTexture);
    UnloadModel(skyboxModel);
    UnloadModel(earthModel);
    UnloadShader(shader3D);
    UnloadShader(shader2D);
    UnloadShader(shaderCloud);
    UnloadShader(shaderMoon);
    UnloadTexture(cloudTexture);
    UnloadModel(cloudModel);
    UnloadTexture(moonTexture);
    UnloadModel(moonModel);
    UnloadShader(shaderAtmosphere);
    UnloadModel(atmosphereModel);
    UnloadFont(customFont);

    SaveSatSelection();

    CloseWindow();
    return 0;
}