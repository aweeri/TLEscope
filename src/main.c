#define _GNU_SOURCE
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h> 
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "types.h"
#include "config.h"
#include "astro.h"

// fragment shader for the 3D globe day/night transition
const char* fs3D = 
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D texture1;\n"
    "uniform vec3 sunDir;\n"
    "void main() {\n"
    "    vec4 day = texture(texture0, fragTexCoord);\n"
    "    vec4 night = texture(texture1, fragTexCoord);\n"
    "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
    "    float phi = fragTexCoord.y * 3.14159265359;\n"
    "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
    "    float intensity = dot(normal, sunDir);\n"
    "    float blend = smoothstep(-0.15, 0.15, intensity);\n"
    "    finalColor = mix(night, day, blend) * fragColor;\n"
    "}\n";

// same logic but for the 2D map view
const char* fs2D = 
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D texture1;\n"
    "uniform vec3 sunDir;\n"
    "void main() {\n"
    "    vec4 day = texture(texture0, fragTexCoord);\n"
    "    vec4 night = texture(texture1, fragTexCoord);\n"
    "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
    "    float phi = fragTexCoord.y * 3.14159265359;\n"
    "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
    "    float intensity = dot(normal, sunDir);\n"
    "    float blend = smoothstep(-0.15, 0.15, intensity);\n"
    "    finalColor = mix(night, day, blend) * fragColor;\n"
    "}\n";

// shader to handle cloud transparency based on light intensity
const char* fsCloud3D = 
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec3 sunDir;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord);\n"
    "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
    "    float phi = fragTexCoord.y * 3.14159265359;\n"
    "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
    "    float intensity = dot(normal, sunDir);\n"
    "    float alpha = smoothstep(-0.15, 0.05, intensity);\n"
    "    finalColor = vec4(texel.rgb, texel.a * alpha) * fragColor;\n"
    "}\n";

// default configuration settings
static AppConfig cfg = {
    .window_width = 1280, .window_height = 720, .target_fps = 120, .ui_scale = 1.0f,
    .show_clouds = false, .show_night_lights = true,
    .bg_color = {0, 0, 0, 255}, .text_main = {255, 255, 255, 255}, .theme = "default",
    .ui_primary = {32, 32, 32, 255}, .ui_secondary = {64, 64, 64, 255}, .ui_accent = {0, 255, 0, 255}
};

static Font customFont;
static Texture2D satIcon, markerIcon, earthTexture, moonTexture, cloudTexture, earthNightTexture;
static Texture2D periMark, apoMark;
static Model earthModel, moonModel, cloudModel;

// helper to adjust color transparency
static Color ApplyAlpha(Color c, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    c.a = (unsigned char)(c.a * alpha);
    return c;
}

// checks if a point is hidden behind the earth from the camera's perspective
static bool IsOccludedByEarth(Vector3 camPos, Vector3 targetPos, float earthRadius) {
    Vector3 v = Vector3Subtract(targetPos, camPos);
    float L = Vector3Length(v);
    Vector3 d = Vector3Scale(v, 1.0f / L);
    float t = -Vector3DotProduct(camPos, d);
    if (t > 0.0f && t < L) {
        Vector3 closest = Vector3Add(camPos, Vector3Scale(d, t));
        if (Vector3Length(closest) < earthRadius * 0.99f) return true; 
    }
    return false;
}

// generates a UV-mapped sphere for planets and clouds
static Mesh GenEarthMesh(float radius, int slices, int rings) {
    Mesh mesh = { 0 };
    int vertexCount = (rings + 1) * (slices + 1);
    int triangleCount = rings * slices * 2;

    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;
    mesh.vertices = (float *)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float *)MemAlloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float *)MemAlloc(vertexCount * 3 * sizeof(float));
    mesh.indices = (unsigned short *)MemAlloc(triangleCount * 3 * sizeof(unsigned short));

    int vIndex = 0;
    for (int i = 0; i <= rings; i++) {
        float v = (float)i / (float)rings;
        float phi = v * PI; 
        for (int j = 0; j <= slices; j++) {
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
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < slices; j++) {
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

// renders the orbital path line in 3D space
static void draw_orbit_3d(Satellite* sat, double current_epoch, bool is_highlighted, float alpha) {
    Color orbitColor = ApplyAlpha(is_highlighted ? cfg.orbit_highlighted : cfg.orbit_normal, alpha);

    if (is_highlighted) {
        Vector3 prev_pos = {0};
        int segments = fmin(4000, fmax(90, (int)(400 * cfg.orbits_to_draw)));
        double orbits_count = (double)cfg.orbits_to_draw;
        
        double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
        double time_step = (period_days * orbits_count) / segments;

        for (int i = 0; i <= segments; i++) {
            double t = current_epoch + (i * time_step);
            double t_unix = get_unix_from_epoch(t);
            Vector3 pos = Vector3Scale(calculate_position(sat, t_unix), 1.0f / DRAW_SCALE);

            if (i > 0) DrawLine3D(prev_pos, pos, orbitColor);
            prev_pos = pos;
        }
    } else {
        if (!sat->orbit_cached) return;
        Vector3 prev_pos = sat->orbit_cache[0];
        for (int i = 1; i < ORBIT_CACHE_SIZE; i++) {
            Vector3 pos = sat->orbit_cache[i];
            DrawLine3D(prev_pos, pos, orbitColor);
            prev_pos = pos;
        }
    }
}

static void DrawUIText(const char* text, float x, float y, float size, Color color) {
    DrawTextEx(customFont, text, (Vector2){x, y}, size, 1.0f, color);
}

// handles speed stepping for time simulation
static double StepTimeMultiplier(double current, bool increase) {
    if (increase) {
        if (current == 0.0) return 0.25;
        if (current >= 0.25) return current * 2.0;
        if (current <= -0.25) {
            if (current == -0.25) return 0.0;
            return current / 2.0;
        }
    } else {
        if (current == 0.0) return -0.25;
        if (current <= -0.25) return current * 2.0;
        if (current >= 0.25) {
            if (current == 0.25) return 0.0;
            return current / 2.0;
        }
    }
    return current;
}

static double unix_to_epoch(double target_unix) {
    double e0 = get_current_real_time_epoch();
    double u0 = get_unix_from_epoch(e0);
    return e0 + (target_unix - u0) / 86400.0;
}

typedef enum { LOCK_NONE, LOCK_EARTH, LOCK_MOON } TargetLock;

// simple loading bar for startup
static void DrawLoadingScreen(float progress, const char* message, Texture2D logoTex) {
    BeginDrawing();
    ClearBackground(cfg.bg_color);
    
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float barW = 400 * cfg.ui_scale;
    float barH = 16 * cfg.ui_scale;
    
    float startY = screenH / 2.0f;
    
    if (logoTex.id != 0) {
        float logoScale = (120.0f * cfg.ui_scale) / logoTex.height; 
        Vector2 logoPos = { (screenW - logoTex.width * logoScale) / 2.0f, startY - (logoTex.height * logoScale) - 40 * cfg.ui_scale };
        DrawTextureEx(logoTex, logoPos, 0.0f, logoScale, WHITE);
    }

    Rectangle barOutline = { (screenW - barW)/2, startY, barW, barH };
    Rectangle barProgress = { barOutline.x + 3, barOutline.y + 3, (barW - 6) * progress, barH - 6 };
    
    #if defined(_WIN32) || defined(_WIN64)
        DrawRectangleRoundedLines(barOutline, 0.5f, 16, 2.0f, cfg.text_main);
    #else
        DrawRectangleRoundedLines(barOutline, 0.5f, 16, cfg.text_main);
    #endif
    if (progress > 0.0f) DrawRectangleRounded(barProgress, 0.5f, 16, cfg.text_secondary);
    
    Vector2 msgSize = MeasureTextEx(customFont, message, 18 * cfg.ui_scale, 1.0f);
    DrawUIText(message, (screenW - msgSize.x)/2, barOutline.y + barH + 20 * cfg.ui_scale, 18 * cfg.ui_scale, cfg.text_main);
    
    EndDrawing();
}

bool show_ui = true;

int main(void) {
    LoadAppConfig("settings.json", &cfg);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(cfg.window_width, cfg.window_height, "TLEscope");
    SetExitKey(0); // disable default exit key (ESC)

    // load app icons and textures
    Image logoImg = LoadImage("logo.png");
    Texture2D logoTex = {0};
    if (logoImg.data != NULL) {
        SetWindowIcon(logoImg);
        logoTex = LoadTextureFromImage(logoImg);
        UnloadImage(logoImg);
    }
    Image logoLImg = LoadImage("logo.png");
    Texture2D logoLTex = {0};
    if (logoLImg.data != NULL) {
        logoLTex = LoadTextureFromImage(logoLImg);
        UnloadImage(logoLImg);
    }
    
    // font and gui setup
    int glyphsCount = 0;
    int *glyphs = LoadCodepoints(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", &glyphsCount);
    customFont = LoadFontEx(TextFormat("themes/%s/font.ttf", cfg.theme), 64, glyphs, glyphsCount);
    GenTextureMipmaps(&customFont.texture);
    SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
    GuiSetFont(customFont);

    // data and asset loading sequence
    DrawLoadingScreen(0.1f, "Fetching TLE Data...", logoLTex);
    load_tle_data("data.tle");

    DrawLoadingScreen(0.25f, "Initializing Textures...", logoTex);
    earthTexture = LoadTexture(TextFormat("themes/%s/earth.png", cfg.theme));
    earthNightTexture = LoadTexture(TextFormat("themes/%s/earth_night.png", cfg.theme));
    
    DrawLoadingScreen(0.4f, "Compiling Shaders...", logoTex);
    Shader shader3D = LoadShaderFromMemory(NULL, fs3D);
    int sunDirLoc3D = GetShaderLocation(shader3D, "sunDir");
    shader3D.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader3D, "texture1");

    Shader shader2D = LoadShaderFromMemory(NULL, fs2D);
    int sunDirLoc2D = GetShaderLocation(shader2D, "sunDir");
    int nightTexLoc2D = GetShaderLocation(shader2D, "texture1");

    Shader shaderCloud = LoadShaderFromMemory(NULL, fsCloud3D);
    int sunDirLocCloud = GetShaderLocation(shaderCloud, "sunDir");
    
    DrawLoadingScreen(0.6f, "Generating Meshes...", logoTex);
    float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
    Mesh sphereMesh = GenEarthMesh(draw_earth_radius, 64, 64);
    earthModel = LoadModelFromMesh(sphereMesh);
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
    earthModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = earthNightTexture;
    Shader defaultEarthShader = earthModel.materials[0].shader;
    
    DrawLoadingScreen(0.8f, "Loading Celestial Bodies...", logoTex);
    float draw_cloud_radius = (EARTH_RADIUS_KM + 25.0f) / DRAW_SCALE;
    Mesh cloudMesh = GenEarthMesh(draw_cloud_radius, 64, 64);
    cloudModel = LoadModelFromMesh(cloudMesh);
    cloudTexture = LoadTexture(TextFormat("themes/%s/clouds.png", cfg.theme));
    cloudModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudTexture;
    Shader defaultCloudShader = cloudModel.materials[0].shader;
    
    float draw_moon_radius = MOON_RADIUS_KM / DRAW_SCALE;
    Mesh moonMesh = GenEarthMesh(draw_moon_radius, 32, 32); 
    moonModel = LoadModelFromMesh(moonMesh);
    moonTexture = LoadTexture(TextFormat("themes/%s/moon.png", cfg.theme));
    moonModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture;

    DrawLoadingScreen(0.95f, "Finalizing UI...", logoTex);
    satIcon = LoadTexture(TextFormat("themes/%s/sat_icon.png", cfg.theme));
    markerIcon = LoadTexture(TextFormat("themes/%s/marker_icon.png", cfg.theme));
    periMark = LoadTexture(TextFormat("themes/%s/smallmark.png", cfg.theme));
    apoMark = LoadTexture(TextFormat("themes/%s/smallmark.png", cfg.theme));
    
    SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(periMark, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(apoMark, TEXTURE_FILTER_BILINEAR);

    DrawLoadingScreen(1.0f, "Ready!", logoTex);

    // camera and view state initialization
    Camera camera3d = { 0 };
    camera3d.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera3d.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera3d.fovy = 45.0f;
    camera3d.projection = CAMERA_PERSPECTIVE;

    Camera2D camera2d = { 0 };
    camera2d.zoom = 1.0f;
    camera2d.offset = (Vector2){ cfg.window_width / 2.0f, cfg.window_height / 2.0f };
    camera2d.target = (Vector2){ 0.0f, 0.0f };

    float target_camera2d_zoom = camera2d.zoom;
    Vector2 target_camera2d_target = camera2d.target;

    float map_w = 2048.0f, map_h = 1024.0f;
    float camDistance = 35.0f, camAngleX = 0.785f, camAngleY = 0.5f;

    float target_camDistance = camDistance;
    float target_camAngleX = camAngleX;
    float target_camAngleY = camAngleY;
    Vector3 target_camera3d_target = camera3d.target;

    double current_epoch = get_current_real_time_epoch();
    double time_multiplier = 1.0; 
    double saved_multiplier = 1.0; 
    bool is_2d_view = false;
    
    bool hide_unselected = false;
    float unselected_fade = 1.0f;

    bool is_auto_warping = false;
    double auto_warp_target = 0.0;
    double auto_warp_initial_diff = 0.0;

    Satellite* hovered_sat = NULL;
    Satellite* selected_sat = NULL;
    TargetLock active_lock = LOCK_EARTH;
    double last_left_click_time = 0.0;

    SetTargetFPS(cfg.target_fps);
    int current_update_idx = 0;

    // UI window state and positioning
    bool show_help = false;
    bool show_settings = false;
    bool show_passes_dialog = false;
    bool show_polar_dialog = false;
    int selected_pass_idx = -1;
    
    float hw_x = 100.0f, hw_y = 250.0f;
    float sw_x = 100.0f, sw_y = 250.0f;
    float pd_x = 1000.0f, pd_y = 200.0f;
    float pl_x = 550.0f, pl_y = 150.0f;

    bool drag_help = false, drag_settings = false, drag_passes = false, drag_polar = false;
    Vector2 drag_help_off = {0}, drag_settings_off = {0}, drag_passes_off = {0}, drag_polar_off = {0};

    bool show_time_dialog = false;
    bool drag_time_dialog = false;
    Vector2 drag_time_off = {0};
    float td_x = 300.0f, td_y = 100.0f;
    
    char text_year[8] = "2026", text_month[4] = "1", text_day[4] = "1";
    char text_hour[4] = "12", text_min[4] = "0", text_sec[4] = "0";
    char text_unix[64] = "0";
    bool edit_year = false, edit_month = false, edit_day = false;
    bool edit_hour = false, edit_min = false, edit_sec = false;
    bool edit_unix = false;

    while (!WindowShouldClose()) {

        bool is_typing = edit_year || edit_month || edit_day || 
                         edit_hour || edit_min || edit_sec || edit_unix;

        // handle keyboard shortcuts
        if (!is_typing) {
            if (IsKeyPressed(KEY_SPACE)) {
                is_auto_warping = false;
                if (time_multiplier != 0.0) {
                    saved_multiplier = time_multiplier;
                    time_multiplier = 0.0;
                } else {
                    time_multiplier = saved_multiplier != 0.0 ? saved_multiplier : 1.0;
                }
            }
            if (IsKeyPressed(KEY_PERIOD)) { is_auto_warping = false; time_multiplier = StepTimeMultiplier(time_multiplier, true); }
            if (IsKeyPressed(KEY_COMMA)) { is_auto_warping = false; time_multiplier = StepTimeMultiplier(time_multiplier, false); }
            if (IsKeyPressed(KEY_M)) is_2d_view = !is_2d_view;
            
            if (IsKeyPressed(KEY_SLASH)) {
                is_auto_warping = false;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) current_epoch = get_current_real_time_epoch(); 
                else {
                    time_multiplier = 1.0; 
                    saved_multiplier = 1.0;
                }
            }

            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) cfg.ui_scale += 0.1f;
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) cfg.ui_scale -= 0.1f;
        }

        if (cfg.ui_scale < 0.5f) cfg.ui_scale = 0.5f;
        if (cfg.ui_scale > 4.0f) cfg.ui_scale = 4.0f;

        // smooth auto-warp integration @juliasatt you're a genius
        if (is_auto_warping) {
            double diff_sec = (auto_warp_target - current_epoch) * 86400.0;
            if (diff_sec <= 0.0) {
                current_epoch = auto_warp_target;
                time_multiplier = 1.0;
                saved_multiplier = 1.0;
                is_auto_warping = false;
            } else {
                double base_speed = auto_warp_initial_diff / 2.0;
                double eased_speed = fmin(base_speed, diff_sec * 3.0); 
                if (eased_speed < 1.0) eased_speed = 1.0;
                time_multiplier = eased_speed;
                
                if (diff_sec <= time_multiplier * GetFrameTime()) {
                    current_epoch = auto_warp_target;
                    time_multiplier = 1.0;
                    saved_multiplier = 1.0;
                    is_auto_warping = false;
                }
            }
        }

        // update simulation time and satellite positions
        current_epoch += (GetFrameTime() * time_multiplier) / 86400.0; 
        current_epoch = normalize_epoch(current_epoch);

        if (sat_count > 0) {
            int updates_per_frame = 50; 
            for (int i = 0; i < updates_per_frame; i++) {
                update_orbit_cache(&satellites[current_update_idx], current_epoch);
                current_update_idx = (current_update_idx + 1) % sat_count;
            }
        }

        double current_unix = get_unix_from_epoch(current_epoch);

        for (int i = 0; i < sat_count; i++) {
            if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;
            satellites[i].current_pos = calculate_position(&satellites[i], current_unix);
        }

        // handle fading of unselected orbits
        bool should_hide = (hide_unselected && selected_sat != NULL);
        if (should_hide) {
            unselected_fade -= 3.0f * GetFrameTime();
            if (unselected_fade < 0.0f) unselected_fade = 0.0f;
        } else {
            unselected_fade += 3.0f * GetFrameTime();
            if (unselected_fade > 1.0f) unselected_fade = 1.0f;
        }

        char datetime_str[64];
        epoch_to_datetime_str(current_epoch, datetime_str);
        double gmst_deg = epoch_to_gmst(current_epoch);

        // calculate moon position and rotation
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

        // handle 2D camera controls and selection
        if (is_2d_view) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && IsKeyDown(KEY_LEFT_SHIFT))) {
                target_camera2d_target = Vector2Add(target_camera2d_target, Vector2Scale(mouseDelta, -1.0f / target_camera2d_zoom));
                active_lock = LOCK_NONE;
            }
            float wheel = GetMouseWheelMove();
            if (wheel != 0 && !is_typing) {
                Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera2d);
                camera2d.offset = GetMousePosition();
                camera2d.target = mouseWorldPos;
                target_camera2d_target = mouseWorldPos; 
                target_camera2d_zoom += wheel * 0.1f * target_camera2d_zoom;
                if (target_camera2d_zoom < 0.1f) target_camera2d_zoom = 0.1f;
                active_lock = LOCK_NONE;
            }

            Vector2 mousePos = GetMousePosition();
            float closest_dist = 9999.0f;
            float hit_radius_pixels = 12.0f * cfg.ui_scale;

            for (int i = 0; i < sat_count; i++) {
                if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;
                
                float mx, my;
                get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &mx, &my);
                
                Vector2 screenPos = GetWorldToScreen2D((Vector2){mx, my}, camera2d);
                float dist = Vector2Distance(mousePos, screenPos);
                
                if (dist < hit_radius_pixels && dist < closest_dist) { closest_dist = dist; hovered_sat = &satellites[i]; }
            }
        } else {
            // handle 3D camera controls and selection
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                if (IsKeyDown(KEY_LEFT_SHIFT)) {
                    Vector3 forward = Vector3Normalize(Vector3Subtract(camera3d.target, camera3d.position));
                    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera3d.up));
                    Vector3 upVector = Vector3Normalize(Vector3CrossProduct(right, forward));
                    float panSpeed = target_camDistance * 0.001f; 
                    target_camera3d_target = Vector3Add(target_camera3d_target, Vector3Scale(right, -mouseDelta.x * panSpeed));
                    target_camera3d_target = Vector3Add(target_camera3d_target, Vector3Scale(upVector, mouseDelta.y * panSpeed));
                    active_lock = LOCK_NONE;
                } else {
                    target_camAngleX -= mouseDelta.x * 0.005f;
                    target_camAngleY += mouseDelta.y * 0.005f;
                    if (target_camAngleY > 1.5f) target_camAngleY = 1.5f;
                    if (target_camAngleY < -1.5f) target_camAngleY = -1.5f;
                }
            }
            if (!is_typing) {
                target_camDistance -= GetMouseWheelMove() * (target_camDistance * 0.1f);
                if (target_camDistance < draw_earth_radius + 1.0f) target_camDistance = draw_earth_radius + 1.0f;
            }

            Ray mouseRay = GetMouseRay(GetMousePosition(), camera3d);
            float closest_dist = 9999.0f;

            for (int i = 0; i < sat_count; i++) {
                if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;

                Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                if (Vector3DistanceSqr(camera3d.target, draw_pos) > (camDistance * camDistance * 16.0f)) continue; 
                
                float distToCam = Vector3Distance(camera3d.position, draw_pos);
                float hit_radius_3d = 0.015f * distToCam * cfg.ui_scale;

                RayCollision col = GetRayCollisionSphere(mouseRay, draw_pos, hit_radius_3d); 
                if (col.hit && col.distance < closest_dist) { closest_dist = col.distance; hovered_sat = &satellites[i]; }
            }
        }

        // define UI layout regions
        Rectangle helpWindow = { hw_x, hw_y, 900 * cfg.ui_scale, 140 * cfg.ui_scale };
        Rectangle settingsWindow = { sw_x, sw_y, 220 * cfg.ui_scale, 140 * cfg.ui_scale };
        Rectangle timeWindow = { td_x, td_y, 520 * cfg.ui_scale, 240 * cfg.ui_scale };
        
        float pass_w = 480 * cfg.ui_scale;
        float pass_h = (60 + (num_passes == 0 ? 1 : num_passes) * 55) * cfg.ui_scale;
        Rectangle passesWindow = { pd_x, pd_y, pass_w, pass_h };
        Rectangle polarWindow = { pl_x, pl_y, 300 * cfg.ui_scale, 360 * cfg.ui_scale };

        Rectangle btnHelp = { 10 * cfg.ui_scale, GetScreenHeight() - 40 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };
        Rectangle btnSet  = { 45 * cfg.ui_scale, GetScreenHeight() - 40 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };

        Rectangle btnRewind = { 10 * cfg.ui_scale, 10 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };
        Rectangle btnPlayPause = { 45 * cfg.ui_scale, 10 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };
        Rectangle btnFastForward = { 80 * cfg.ui_scale, 10 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };
        Rectangle btnNow = { 115 * cfg.ui_scale, 10 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };
        Rectangle btnClock = { 150 * cfg.ui_scale, 10 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };
        Rectangle btnPasses = { 185 * cfg.ui_scale, 10 * cfg.ui_scale, 30 * cfg.ui_scale, 30 * cfg.ui_scale };

        // check if mouse is over any UI element to prevent world interaction
        bool over_ui = false;
        if (CheckCollisionPointRec(GetMousePosition(), btnHelp)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnSet)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnRewind)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnPlayPause)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnFastForward)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnNow)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnClock)) over_ui = true;
        if (CheckCollisionPointRec(GetMousePosition(), btnPasses)) over_ui = true;

        if (show_help && CheckCollisionPointRec(GetMousePosition(), helpWindow)) over_ui = true;
        if (show_settings && CheckCollisionPointRec(GetMousePosition(), settingsWindow)) over_ui = true;
        if (show_time_dialog && CheckCollisionPointRec(GetMousePosition(), timeWindow)) over_ui = true;
        if (show_passes_dialog && CheckCollisionPointRec(GetMousePosition(), passesWindow)) over_ui = true;
        if (show_polar_dialog && CheckCollisionPointRec(GetMousePosition(), polarWindow)) over_ui = true;

        // handle clicking/locking on objects
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (!over_ui) {
                edit_year = false; edit_month = false; edit_day = false;
                edit_hour = false; edit_min = false; edit_sec = false;
                edit_unix = false;

                selected_sat = hovered_sat; 

                double current_time = GetTime();
                if (current_time - last_left_click_time < 0.3) {
                    if (is_2d_view) {
                        Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera2d);
                        bool hit_moon = false;
                        for (int offset_i = -1; offset_i <= 1; offset_i++) {
                            float x_off = offset_i * map_w;
                            if (Vector2Distance(mouseWorld, (Vector2){moon_mx + x_off, moon_my}) < (15.0f * cfg.ui_scale / camera2d.zoom)) {
                                hit_moon = true;
                                break;
                            }
                        }
                        active_lock = hit_moon ? LOCK_MOON : LOCK_EARTH;
                    } else {
                        Ray mouseRay = GetMouseRay(GetMousePosition(), camera3d);
                        RayCollision earthCol = GetRayCollisionSphere(mouseRay, Vector3Zero(), draw_earth_radius);
                        RayCollision moonCol = GetRayCollisionSphere(mouseRay, draw_moon_pos, draw_moon_radius);

                        if (moonCol.hit && (!earthCol.hit || moonCol.distance < earthCol.distance)) {
                            active_lock = LOCK_MOON;
                        } else if (earthCol.hit) {
                            active_lock = LOCK_EARTH;
                        }
                    }
                }
                last_left_click_time = current_time;
            }
        }

        // update camera targets based on lock state
        if (active_lock == LOCK_EARTH) {
            if (is_2d_view) target_camera2d_target = Vector2Zero();
            else target_camera3d_target = Vector3Zero();
        } else if (active_lock == LOCK_MOON) {
            if (is_2d_view) target_camera2d_target = (Vector2){moon_mx, moon_my};
            else target_camera3d_target = draw_moon_pos;
        }

        // smooth camera interpolation
        float smooth_speed = 10.0f * GetFrameTime();
        
        camera2d.zoom = Lerp(camera2d.zoom, target_camera2d_zoom, smooth_speed);
        camera2d.target = Vector2Lerp(camera2d.target, target_camera2d_target, smooth_speed);
        
        camAngleX = Lerp(camAngleX, target_camAngleX, smooth_speed);
        camAngleY = Lerp(camAngleY, target_camAngleY, smooth_speed);
        camDistance = Lerp(camDistance, target_camDistance, smooth_speed);
        camera3d.target = Vector3Lerp(camera3d.target, target_camera3d_target, smooth_speed);

        if (!is_2d_view) {
            camera3d.position.x = camera3d.target.x + camDistance * cosf(camAngleY) * sinf(camAngleX);
            camera3d.position.y = camera3d.target.y + camDistance * sinf(camAngleY);
            camera3d.position.z = camera3d.target.z + camDistance * cosf(camAngleY) * cosf(camAngleX);
        }

        Satellite* active_sat = hovered_sat ? hovered_sat : selected_sat;

        if (show_passes_dialog && selected_sat) {
            if (last_pass_calc_sat != selected_sat || (num_passes > 0 && current_epoch > passes[0].los_epoch + 1.0/1440.0)) {
                CalculatePasses(selected_sat, current_epoch);
            }
        }

        // calculate radio footprint geometry
        #define FP_RINGS 12
        #define FP_PTS 120
        Vector3 fp_grid[FP_RINGS + 1][FP_PTS];
        bool has_footprint = false;
        
        if (active_sat) {
            float r = Vector3Length(active_sat->current_pos);
            if (r > EARTH_RADIUS_KM) {
                has_footprint = true;
                float theta = acosf(EARTH_RADIUS_KM / r);
                Vector3 s_norm = Vector3Normalize(active_sat->current_pos);
                Vector3 up = fabsf(s_norm.y) > 0.99f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
                Vector3 u = Vector3Normalize(Vector3CrossProduct(up, s_norm));
                Vector3 v = Vector3CrossProduct(s_norm, u);
                
                for (int i = 0; i <= FP_RINGS; i++) {
                    float a = theta * ((float)i / FP_RINGS);
                    float d_plane = EARTH_RADIUS_KM * cosf(a), r_circle = EARTH_RADIUS_KM * sinf(a);
                    for (int k = 0; k < FP_PTS; k++) {
                        float alpha = (2.0f * PI * k) / FP_PTS;
                        fp_grid[i][k] = Vector3Add(Vector3Scale(s_norm, d_plane),
                            Vector3Add(Vector3Scale(u, cosf(alpha) * r_circle), Vector3Scale(v, sinf(alpha) * r_circle)));
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground(cfg.bg_color);

        float m_size_2d = 24.0f * cfg.ui_scale / camera2d.zoom;
        float m_text_2d = 16.0f * cfg.ui_scale / camera2d.zoom;
        float mark_size_2d = 32.0f * cfg.ui_scale / camera2d.zoom;

        if (is_2d_view) {
            // start of 2D map rendering
            BeginMode2D(camera2d);
                if (cfg.show_night_lights) {
                    BeginShaderMode(shader2D);
                    SetShaderValueTexture(shader2D, nightTexLoc2D, earthNightTexture);
                    
                    Vector3 sunEci = calculate_sun_position(current_epoch);
                    float earth_rot_rad = (gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                    Vector3 sunEcef = Vector3Transform(sunEci, MatrixRotateY(-earth_rot_rad));
                    SetShaderValue(shader2D, sunDirLoc2D, &sunEcef, SHADER_UNIFORM_VEC3);
                }

                DrawTexturePro(earthTexture, (Rectangle){0, 0, earthTexture.width, earthTexture.height}, 
                    (Rectangle){-map_w/2, -map_h/2, map_w, map_h}, (Vector2){0,0}, 0.0f, WHITE);

                if (cfg.show_night_lights) {
                    EndShaderMode();
                }

                // calculate scissor area to prevent drawing outside map bounds
                Vector2 mapMin = GetWorldToScreen2D((Vector2){-map_w/2.0f, -map_h/2.0f}, camera2d);
                Vector2 mapMax = GetWorldToScreen2D((Vector2){map_w/2.0f, map_h/2.0f}, camera2d);
                
                int sc_x = (int)mapMin.x, sc_y = (int)mapMin.y;
                int sc_w = (int)(mapMax.x - mapMin.x), sc_h = (int)(mapMax.y - mapMin.y);
                
                if (sc_x < 0) { sc_w += sc_x; sc_x = 0; }
                if (sc_y < 0) { sc_h += sc_y; sc_y = 0; }
                if (sc_x + sc_w > GetScreenWidth()) sc_w = GetScreenWidth() - sc_x;
                if (sc_y + sc_h > GetScreenHeight()) sc_h = GetScreenHeight() - sc_y;

                if (sc_w > 0 && sc_h > 0) {
                    BeginScissorMode(sc_x, sc_y, sc_w, sc_h);

                    // draw footprint coverage triangles
                    if (active_sat && has_footprint) {
                        for (int i = 0; i < FP_RINGS; i++) {
                            for (int k = 0; k < FP_PTS; k++) {
                                int next = (k + 1) % FP_PTS;
                                float x1, y1, x2, y2, x3, y3, x4, y4;
                                get_map_coordinates(fp_grid[i][k], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x1, &y1);
                                get_map_coordinates(fp_grid[i][next], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x2, &y2);
                                get_map_coordinates(fp_grid[i+1][k], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x3, &y3);
                                get_map_coordinates(fp_grid[i+1][next], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x4, &y4);
                                
                                if (x2 - x1 > map_w * 0.6f) x2 -= map_w; else if (x2 - x1 < -map_w * 0.6f) x2 += map_w;
                                if (x3 - x1 > map_w * 0.6f) x3 -= map_w; else if (x3 - x1 < -map_w * 0.6f) x3 += map_w;
                                if (x4 - x1 > map_w * 0.6f) x4 -= map_w; else if (x4 - x1 < -map_w * 0.6f) x4 += map_w;
                                
                                for (int offset_i = -1; offset_i <= 1; offset_i++) {
                                    float x_off = offset_i * map_w;
                                    DrawTriangle((Vector2){x1+x_off, y1}, (Vector2){x3+x_off, y3}, (Vector2){x2+x_off, y2}, cfg.footprint_bg);
                                    DrawTriangle((Vector2){x2+x_off, y2}, (Vector2){x3+x_off, y3}, (Vector2){x4+x_off, y4}, cfg.footprint_bg);
                                }
                            }
                        }
                        for (int k = 0; k < FP_PTS; k++) {
                            int next = (k + 1) % FP_PTS;
                            float x1, y1, x2, y2;
                            get_map_coordinates(fp_grid[FP_RINGS][k], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x1, &y1);
                            get_map_coordinates(fp_grid[FP_RINGS][next], gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &x2, &y2);
                            if (x2 - x1 > map_w * 0.6f) x2 -= map_w; else if (x2 - x1 < -map_w * 0.6f) x2 += map_w;
                            for (int offset_i = -1; offset_i <= 1; offset_i++) {
                                if (fabs(x2 - x1) < map_w * 0.6f) {
                                    DrawLineEx((Vector2){x1 + offset_i*map_w, y1}, (Vector2){x2 + offset_i*map_w, y2}, 2.0f/camera2d.zoom, cfg.footprint_border);
                                }
                            }
                        }
                    }

                    // draw satellites and their ground tracks
                    for (int i = 0; i < sat_count; i++) {
                        bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                        float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                        if (sat_alpha <= 0.0f) continue;

                        bool is_hl = (active_sat == &satellites[i]);
                        Color sCol = (selected_sat == &satellites[i]) ? cfg.sat_selected : (hovered_sat == &satellites[i]) ? cfg.sat_highlighted : cfg.sat_normal;
                        sCol = ApplyAlpha(sCol, sat_alpha);

                        if (is_hl) {
                            int segments = fmin(4000, fmax(50, (int)(400 * cfg.orbits_to_draw))); 
                            Vector2 track_pts[4001]; 

                            double period_days = (2.0 * PI / satellites[i].mean_motion) / 86400.0;
                            double time_step = (period_days * cfg.orbits_to_draw) / segments;

                            for (int j = 0; j <= segments; j++) {
                                double t = current_epoch + (j * time_step);
                                double t_unix = get_unix_from_epoch(t);
                                get_map_coordinates(calculate_position(&satellites[i], t_unix), epoch_to_gmst(t), cfg.earth_rotation_offset, map_w, map_h, &track_pts[j].x, &track_pts[j].y);
                            }

                            for (int offset_i = -1; offset_i <= 1; offset_i++) {
                                float x_off = offset_i * map_w;
                                for (int j = 1; j <= segments; j++) {
                                    if (fabs(track_pts[j].x - track_pts[j-1].x) < map_w * 0.6f) {
                                        DrawLineEx((Vector2){track_pts[j-1].x+x_off, track_pts[j-1].y}, (Vector2){track_pts[j].x+x_off, track_pts[j].y}, 2.0f/camera2d.zoom, ApplyAlpha(cfg.orbit_highlighted, sat_alpha));
                                    }
                                }

                                Vector2 peri2d, apo2d;
                                get_apsis_2d(&satellites[i], current_epoch, false, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &peri2d);
                                get_apsis_2d(&satellites[i], current_epoch, true, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &apo2d);
                                
                                DrawTexturePro(periMark, (Rectangle){0,0,periMark.width,periMark.height}, 
                                    (Rectangle){peri2d.x + x_off, peri2d.y, mark_size_2d, mark_size_2d}, (Vector2){mark_size_2d/2.f, mark_size_2d/2.f}, 0.0f, ApplyAlpha(cfg.periapsis, sat_alpha));
                                DrawTexturePro(apoMark, (Rectangle){0,0,apoMark.width,apoMark.height}, 
                                    (Rectangle){apo2d.x + x_off, apo2d.y, mark_size_2d, mark_size_2d}, (Vector2){mark_size_2d/2.f, mark_size_2d/2.f}, 0.0f, ApplyAlpha(cfg.apoapsis, sat_alpha));
                            }
                        }

                        float sat_mx, sat_my;
                        get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sat_mx, &sat_my);
                        for (int offset_i = -1; offset_i <= 1; offset_i++) {
                            DrawTexturePro(satIcon, (Rectangle){0,0,satIcon.width,satIcon.height}, 
                                (Rectangle){sat_mx+(offset_i*map_w), sat_my, m_size_2d, m_size_2d}, (Vector2){m_size_2d/2.f, m_size_2d/2.f}, 0.0f, sCol);
                        }
                    }

                    // draw home and observer markers
                    float hx = (home_location.lon / 360.0f) * map_w;
                    float hy = -(home_location.lat / 180.0f) * map_h;
                    for (int offset_i = -1; offset_i <= 1; offset_i++) {
                        float x_off = offset_i * map_w;
                        DrawTexturePro(markerIcon, (Rectangle){0,0,markerIcon.width,markerIcon.height}, 
                            (Rectangle){hx+x_off, hy, m_size_2d, m_size_2d}, (Vector2){m_size_2d/2.f, m_size_2d/2.f}, 0.0f, WHITE);
                        
                        if (camera2d.zoom > 0.1f) {
                            DrawUIText(home_location.name, hx+x_off+(m_size_2d/2.f)+4.f, hy-(m_size_2d/2.f), m_text_2d, WHITE);
                        }
                    }

                    for (int m = 0; m < marker_count; m++) {
                        float mx = (markers[m].lon / 360.0f) * map_w;
                        float my = -(markers[m].lat / 180.0f) * map_h;
                        for (int offset_i = -1; offset_i <= 1; offset_i++) {
                            float x_off = offset_i * map_w;
                            DrawTexturePro(markerIcon, (Rectangle){0,0,markerIcon.width,markerIcon.height}, 
                                (Rectangle){mx+x_off, my, m_size_2d, m_size_2d}, (Vector2){m_size_2d/2.f, m_size_2d/2.f}, 0.0f, WHITE);
                            
                            if (camera2d.zoom > 0.1f) {
                                DrawUIText(markers[m].name, mx+x_off+(m_size_2d/2.f)+4.f, my-(m_size_2d/2.f), m_text_2d, WHITE);
                            }
                        }
                    }

                    EndScissorMode();
                }
            EndMode2D();
        } else {
            // start of 3D globe rendering
            BeginMode3D(camera3d);
                earthModel.transform = MatrixRotateY((gmst_deg + cfg.earth_rotation_offset) * DEG2RAD);
                
                if (cfg.show_night_lights) {
                    earthModel.materials[0].shader = shader3D;
                    Vector3 sunEci = calculate_sun_position(current_epoch);
                    float earth_rot_rad = (gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                    Vector3 sunEcef = Vector3Transform(sunEci, MatrixRotateY(-earth_rot_rad));
                    SetShaderValue(shader3D, sunDirLoc3D, &sunEcef, SHADER_UNIFORM_VEC3);
                } else {
                    earthModel.materials[0].shader = defaultEarthShader;
                }

                DrawModel(earthModel, Vector3Zero(), 1.0f, WHITE);

                // render animated cloud layer
                if (cfg.show_clouds) {
                    double continuous_cloud_angle = fmod(gmst_deg + cfg.earth_rotation_offset + (current_epoch * 360.0 * 0.04), 360.0);
                    float cloud_rot_rad = (float)(continuous_cloud_angle * DEG2RAD);
                    cloudModel.transform = MatrixRotateY(cloud_rot_rad);
                    
                    if (cfg.show_night_lights) {
                        cloudModel.materials[0].shader = shaderCloud;
                        Vector3 sunEci = calculate_sun_position(current_epoch);
                        Vector3 sunCloudSpace = Vector3Transform(sunEci, MatrixRotateY(-cloud_rot_rad));
                        SetShaderValue(shaderCloud, sunDirLocCloud, &sunCloudSpace, SHADER_UNIFORM_VEC3);
                    } else {
                        cloudModel.materials[0].shader = defaultCloudShader;
                    }

                    DrawModel(cloudModel, Vector3Zero(), 1.0f, WHITE);
                }

                DrawModel(moonModel, draw_moon_pos, 1.0f, WHITE);

                // draw radio footprints on globe surface
                if (active_sat && has_footprint) {
                    for (int i = 0; i < FP_RINGS; i++) {
                        for (int k = 0; k < FP_PTS; k++) {
                            int next = (k + 1) % FP_PTS;
                            Vector3 p1 = Vector3Scale(fp_grid[i][k], 1.01f/DRAW_SCALE), p2 = Vector3Scale(fp_grid[i][next], 1.01f/DRAW_SCALE);
                            Vector3 p3 = Vector3Scale(fp_grid[i+1][k], 1.01f/DRAW_SCALE), p4 = Vector3Scale(fp_grid[i+1][next], 1.01f/DRAW_SCALE);
                            DrawTriangle3D(p1, p3, p2, cfg.footprint_bg);
                            DrawTriangle3D(p2, p3, p4, cfg.footprint_bg);
                        }
                    }
                    for (int k = 0; k < FP_PTS; k++) {
                        int next = (k + 1) % FP_PTS;
                        DrawLine3D(Vector3Scale(fp_grid[FP_RINGS][k], 1.01f/DRAW_SCALE), Vector3Scale(fp_grid[FP_RINGS][next], 1.01f/DRAW_SCALE), cfg.footprint_border);
                    }
                }

                for (int i = 0; i < sat_count; i++) {
                    bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                    float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                    if (sat_alpha <= 0.0f) continue;

                    bool is_hl = (active_sat == &satellites[i]);
                    draw_orbit_3d(&satellites[i], current_epoch, is_hl, sat_alpha);

                    if (is_hl) {
                        Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                        DrawLine3D(Vector3Zero(), draw_pos, ApplyAlpha(cfg.orbit_highlighted, sat_alpha));
                    }
                }
            EndMode3D();

            float m_size_3d = 24.0f * cfg.ui_scale;
            float m_text_3d = 16.0f * cfg.ui_scale;
            float mark_size_3d = 32.0f * cfg.ui_scale;

            // overlay periapsis/apoapsis indicators on the 3D view
            if (active_sat) {
                bool is_unselected = (selected_sat != NULL && active_sat != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                
                double t_peri_unix, t_apo_unix;
                get_apsis_times(active_sat, current_epoch, &t_peri_unix, &t_apo_unix);

                Vector3 draw_p = Vector3Scale(calculate_position(active_sat, t_peri_unix), 1.0f/DRAW_SCALE);
                Vector3 draw_a = Vector3Scale(calculate_position(active_sat, t_apo_unix), 1.0f/DRAW_SCALE);

                if (!IsOccludedByEarth(camera3d.position, draw_p, draw_earth_radius)) {
                    Vector2 sp = GetWorldToScreen(draw_p, camera3d);
                    DrawTexturePro(periMark, (Rectangle){0,0,periMark.width,periMark.height}, 
                        (Rectangle){sp.x, sp.y, mark_size_3d, mark_size_3d}, (Vector2){mark_size_3d/2.f, mark_size_3d/2.f}, 0.0f, ApplyAlpha(cfg.periapsis, sat_alpha));
                }
                if (!IsOccludedByEarth(camera3d.position, draw_a, draw_earth_radius)) {
                    Vector2 sp = GetWorldToScreen(draw_a, camera3d);
                    DrawTexturePro(apoMark, (Rectangle){0,0,apoMark.width,apoMark.height}, 
                        (Rectangle){sp.x, sp.y, mark_size_3d, mark_size_3d}, (Vector2){mark_size_3d/2.f, mark_size_3d/2.f}, 0.0f, ApplyAlpha(cfg.apoapsis, sat_alpha));
                }
            }

            // draw satellite icons with occlusion checking
            for (int i = 0; i < sat_count; i++) {
                bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                if (sat_alpha <= 0.0f) continue;

                Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                Vector3 toTarget = Vector3Subtract(draw_pos, camera3d.position);
                Vector3 camForward = Vector3Normalize(Vector3Subtract(camera3d.target, camera3d.position));

                if (Vector3DotProduct(toTarget, camForward) > 0.0f && !IsOccludedByEarth(camera3d.position, draw_pos, draw_earth_radius)) {
                    Color sCol = (selected_sat == &satellites[i]) ? cfg.sat_selected : (hovered_sat == &satellites[i]) ? cfg.sat_highlighted : cfg.sat_normal;
                    sCol = ApplyAlpha(sCol, sat_alpha);
                    Vector2 sp = GetWorldToScreen(draw_pos, camera3d);
                    DrawTexturePro(satIcon, (Rectangle){0,0,satIcon.width,satIcon.height}, 
                        (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d/2.f, m_size_3d/2.f}, 0.0f, sCol);
                }
            }

            Vector3 camForward = Vector3Normalize(Vector3Subtract(camera3d.target, camera3d.position));

            // map-to-3D projection for ground markers
            float h_lat_rad = home_location.lat * DEG2RAD, h_lon_rad = (home_location.lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
            Vector3 h_pos = { cosf(h_lat_rad)*cosf(h_lon_rad)*draw_earth_radius, sinf(h_lat_rad)*draw_earth_radius, -cosf(h_lat_rad)*sinf(h_lon_rad)*draw_earth_radius };
            Vector3 h_normal = Vector3Normalize(h_pos);
            Vector3 h_viewDir = Vector3Normalize(Vector3Subtract(camera3d.position, h_pos));
            Vector3 h_toTarget = Vector3Subtract(h_pos, camera3d.position);
            
            if (Vector3DotProduct(h_normal, h_viewDir) > 0.0f && Vector3DotProduct(h_toTarget, camForward) > 0.0f) {
                Vector2 sp = GetWorldToScreen(h_pos, camera3d);
                DrawTexturePro(markerIcon, (Rectangle){0,0,markerIcon.width,markerIcon.height}, 
                    (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d/2.f, m_size_3d/2.f}, 0.0f, WHITE);
                
                if (camDistance < 50.0f) {
                    DrawUIText(home_location.name, sp.x+(m_size_3d/2.f)+4.f, sp.y-(m_size_3d/2.f), m_text_3d, WHITE);
                }
            }

            for (int m = 0; m < marker_count; m++) {
                float lat_rad = markers[m].lat * DEG2RAD, lon_rad = (markers[m].lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                Vector3 m_pos = { cosf(lat_rad)*cosf(lon_rad)*draw_earth_radius, sinf(lat_rad)*draw_earth_radius, -cosf(lat_rad)*sinf(lon_rad)*draw_earth_radius };
                Vector3 normal = Vector3Normalize(m_pos);
                Vector3 viewDir = Vector3Normalize(Vector3Subtract(camera3d.position, m_pos));
                Vector3 toTarget = Vector3Subtract(m_pos, camera3d.position);

                if (Vector3DotProduct(normal, viewDir) > 0.0f && Vector3DotProduct(toTarget, camForward) > 0.0f) {
                    Vector2 sp = GetWorldToScreen(m_pos, camera3d);
                    DrawTexturePro(markerIcon, (Rectangle){0,0,markerIcon.width,markerIcon.height}, 
                        (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d/2.f, m_size_3d/2.f}, 0.0f, WHITE);
                    
                    if (camDistance < 50.0f) {
                        DrawUIText(markers[m].name, sp.x+(m_size_3d/2.f)+4.f, sp.y-(m_size_3d/2.f), m_text_3d, WHITE);
                    }
                }
            }
        }

        // render top status bar
        char top_text[128];
        sprintf(top_text, "%s | Speed: %gx %s", datetime_str, time_multiplier, (time_multiplier == 0.0) ? "[PAUSED]" : "");
        DrawUIText(top_text, 230*cfg.ui_scale, 15*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_main);

        // real-time indicator rendering
        if (time_multiplier == 1.0 && fabs(current_epoch - get_current_real_time_epoch()) < (5.0 / 86400.0) && !is_auto_warping) {
            float blink_alpha = (sinf(GetTime() * 4.0f) * 0.5f + 0.5f);
            
            int oldStyle = GuiGetStyle(LABEL, TEXT_COLOR_NORMAL);
            GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(ApplyAlpha(cfg.ui_accent, blink_alpha)));
            GuiLabel((Rectangle){ 230*cfg.ui_scale, 34*cfg.ui_scale, 20*cfg.ui_scale, 20*cfg.ui_scale }, "#212#");
            GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, oldStyle);

            DrawUIText("REAL TIME", 255*cfg.ui_scale, 40*cfg.ui_scale, 16*cfg.ui_scale, cfg.ui_accent);
        }

        // global UI style configuration
        GuiSetStyle(DEFAULT, TEXT_SIZE, 16 * cfg.ui_scale);
        GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(cfg.ui_primary));
        GuiSetStyle(DEFAULT, LINE_COLOR, ColorToInt(cfg.ui_secondary)); 
        GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, ColorToInt(cfg.ui_primary));
        GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, ColorToInt(cfg.ui_secondary));
        GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, ColorToInt(cfg.ui_accent));
        GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(cfg.ui_secondary));
        GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg.ui_accent));
        GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg.ui_accent));
        GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(cfg.text_main));
        GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(cfg.text_main));
        GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(cfg.text_main)); 
        GuiSetStyle(CHECKBOX, TEXT_PADDING, 8 * cfg.ui_scale);
        GuiSetStyle(TEXTBOX, BORDER_COLOR_FOCUSED, ColorToInt(cfg.ui_secondary));
        GuiSetStyle(TEXTBOX, BORDER_COLOR_PRESSED, ColorToInt(cfg.ui_secondary));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED, ColorToInt(cfg.text_main));
        GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, ColorToInt(cfg.text_main));
        GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, ColorToInt(cfg.ui_primary));

        // render main UI buttons
        if (GuiButton(btnHelp, "#193#")) show_help = !show_help;
        if (GuiButton(btnSet, "#142#")) show_settings = !show_settings;

        if (GuiButton(btnRewind, "#118#")) { is_auto_warping = false; time_multiplier = StepTimeMultiplier(time_multiplier, false); }
        if (GuiButton(btnPlayPause, (time_multiplier == 0.0) ? "#131#" : "#132#")) {
            is_auto_warping = false;
            if (time_multiplier != 0.0) {
                saved_multiplier = time_multiplier;
                time_multiplier = 0.0;
            } else {
                time_multiplier = saved_multiplier != 0.0 ? saved_multiplier : 1.0;
            }
        }
        if (GuiButton(btnFastForward, "#119#")) { is_auto_warping = false; time_multiplier = StepTimeMultiplier(time_multiplier, true); }
        
        if (GuiButton(btnNow, "#143#")) { 
            is_auto_warping = false;
            current_epoch = get_current_real_time_epoch();
            time_multiplier = 1.0;
            saved_multiplier = 1.0;
        }
        
        if (GuiButton(btnClock, "#139#")) { 
            show_time_dialog = !show_time_dialog;
            if (show_time_dialog) {
                time_t t_unix = (time_t)get_unix_from_epoch(current_epoch);
                struct tm *tm_info = gmtime(&t_unix); 
                if (tm_info) {
                    sprintf(text_year, "%d", tm_info->tm_year + 1900);
                    sprintf(text_month, "%d", tm_info->tm_mon + 1);
                    sprintf(text_day, "%d", tm_info->tm_mday);
                    sprintf(text_hour, "%d", tm_info->tm_hour);
                    sprintf(text_min, "%d", tm_info->tm_min);
                    sprintf(text_sec, "%d", tm_info->tm_sec);
                }
                sprintf(text_unix, "%ld", (long)t_unix);
            }
        }

        if (GuiButton(btnPasses, "#208#")) {
            show_passes_dialog = !show_passes_dialog;
            if (show_passes_dialog && selected_sat) {
                CalculatePasses(selected_sat, current_epoch);
            }
        }

        // draw various popup windows (Help, Settings, Time, etc.)
        if (show_help) {
            Rectangle titleBar = { hw_x, hw_y, helpWindow.width, 24 * cfg.ui_scale }; 
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), titleBar)) {
                drag_help = true;
                drag_help_off = Vector2Subtract(GetMousePosition(), (Vector2){hw_x, hw_y});
            }
            if (drag_help) {
                hw_x = GetMousePosition().x - drag_help_off.x;
                hw_y = GetMousePosition().y - drag_help_off.y;
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) drag_help = false;
            }
            
            if (GuiWindowBox(helpWindow, "Help & Controls")) show_help = false;
            
            DrawUIText(is_2d_view ? "Controls: RMB to pan, Scroll to zoom. 'M' switches to 3D. Space: Pause." : "Controls: RMB to orbit, Shift+RMB to pan. 'M' switches to 2D. Space: Pause.", hw_x + 10*cfg.ui_scale, hw_y + 35*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
            DrawUIText("Time: '.' (Faster 2x), ',' (Slower 0.5x), '/' (1x Speed), 'Shift+/' (Reset)", hw_x + 10*cfg.ui_scale, hw_y + 65*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
            DrawUIText(TextFormat("UI Scale: '-' / '+' (%.1fx)", cfg.ui_scale), hw_x + 10*cfg.ui_scale, hw_y + 95*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
        }

        if (show_settings) {
            Rectangle titleBar = { sw_x, sw_y, settingsWindow.width, 24 * cfg.ui_scale };
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), titleBar)) {
                drag_settings = true;
                drag_settings_off = Vector2Subtract(GetMousePosition(), (Vector2){sw_x, sw_y});
            }
            if (drag_settings) {
                sw_x = GetMousePosition().x - drag_settings_off.x;
                sw_y = GetMousePosition().y - drag_settings_off.y;
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) drag_settings = false;
            }
            
            if (GuiWindowBox(settingsWindow, "Settings")) show_settings = false;
            
            Rectangle cbRec = { sw_x + 10 * cfg.ui_scale, sw_y + 40 * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
            GuiCheckBox(cbRec, "Hide Unselected", &hide_unselected);

            Rectangle cbRec2 = { sw_x + 10 * cfg.ui_scale, sw_y + 70 * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
            GuiCheckBox(cbRec2, "Show Clouds", &cfg.show_clouds);

            Rectangle cbRec3 = { sw_x + 10 * cfg.ui_scale, sw_y + 100 * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
            GuiCheckBox(cbRec3, "Night Lights", &cfg.show_night_lights);
        }

        if (show_time_dialog) {
            if (IsKeyPressed(KEY_TAB)) {
                if (edit_year) { edit_year = false; edit_month = true; }
                else if (edit_month) { edit_month = false; edit_day = true; }
                else if (edit_day) { edit_day = false; edit_hour = true; }
                else if (edit_hour) { edit_hour = false; edit_min = true; }
                else if (edit_min) { edit_min = false; edit_sec = true; }
                else if (edit_sec) { edit_sec = false; edit_unix = true; }
                else if (edit_unix) { edit_unix = false; edit_year = true; }
                else { edit_year = true; } 
            }
            Rectangle titleBar = { td_x, td_y, timeWindow.width, 24 * cfg.ui_scale };
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), titleBar)) {
                drag_time_dialog = true;
                drag_time_off = Vector2Subtract(GetMousePosition(), (Vector2){td_x, td_y});
            }
            if (drag_time_dialog) {
                td_x = GetMousePosition().x - drag_time_off.x;
                td_y = GetMousePosition().y - drag_time_off.y;
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) drag_time_dialog = false;
            }

            if (GuiWindowBox(timeWindow, "Set Date & Time (UTC)")) show_time_dialog = false;

            GuiLabel((Rectangle){ td_x + 20*cfg.ui_scale, td_y + 45*cfg.ui_scale, 100*cfg.ui_scale, 28*cfg.ui_scale }, "Date (Y-M-D):");
            if (GuiTextBox((Rectangle){ td_x + 130*cfg.ui_scale, td_y + 45*cfg.ui_scale, 70*cfg.ui_scale, 32*cfg.ui_scale }, text_year, 8, edit_year)) edit_year = !edit_year;
            if (GuiTextBox((Rectangle){ td_x + 210*cfg.ui_scale, td_y + 45*cfg.ui_scale, 50*cfg.ui_scale, 32*cfg.ui_scale }, text_month, 4, edit_month)) edit_month = !edit_month;
            if (GuiTextBox((Rectangle){ td_x + 270*cfg.ui_scale, td_y + 45*cfg.ui_scale, 50*cfg.ui_scale, 32*cfg.ui_scale }, text_day, 4, edit_day)) edit_day = !edit_day;

            GuiLabel((Rectangle){ td_x + 20*cfg.ui_scale, td_y + 90*cfg.ui_scale, 100*cfg.ui_scale, 28*cfg.ui_scale }, "Time (H:M:S):");
            if (GuiTextBox((Rectangle){ td_x + 130*cfg.ui_scale, td_y + 90*cfg.ui_scale, 50*cfg.ui_scale, 32*cfg.ui_scale }, text_hour, 4, edit_hour)) edit_hour = !edit_hour;
            if (GuiTextBox((Rectangle){ td_x + 190*cfg.ui_scale, td_y + 90*cfg.ui_scale, 50*cfg.ui_scale, 32*cfg.ui_scale }, text_min, 4, edit_min)) edit_min = !edit_min;
            if (GuiTextBox((Rectangle){ td_x + 250*cfg.ui_scale, td_y + 90*cfg.ui_scale, 50*cfg.ui_scale, 32*cfg.ui_scale }, text_sec, 4, edit_sec)) edit_sec = !edit_sec;

            if (GuiButton((Rectangle){ td_x + 340*cfg.ui_scale, td_y + 45*cfg.ui_scale, 160*cfg.ui_scale, 77*cfg.ui_scale }, "Apply Date/Time")) {
                is_auto_warping = false;
                struct tm t = {0};
                t.tm_year = atoi(text_year) - 1900;
                t.tm_mon = atoi(text_month) - 1;
                t.tm_mday = atoi(text_day);
                t.tm_hour = atoi(text_hour);
                t.tm_min = atoi(text_min);
                t.tm_sec = atoi(text_sec);

                #ifdef _WIN32
                    time_t unix_time = _mkgmtime(&t);
                #else
                    time_t unix_time = timegm(&t);
                #endif

                if (unix_time != -1) {
                    current_epoch = unix_to_epoch((double)unix_time);
                }
                show_time_dialog = false;
            }

            DrawLine(td_x + 20*cfg.ui_scale, td_y + 145*cfg.ui_scale, td_x + 500*cfg.ui_scale, td_y + 145*cfg.ui_scale, cfg.ui_secondary);

            GuiLabel((Rectangle){ td_x + 20*cfg.ui_scale, td_y + 165*cfg.ui_scale, 150*cfg.ui_scale, 28*cfg.ui_scale }, "Unix Epoch:");
            if (GuiTextBox((Rectangle){ td_x + 170*cfg.ui_scale, td_y + 165*cfg.ui_scale, 160*cfg.ui_scale, 32*cfg.ui_scale }, text_unix, 64, edit_unix)) edit_unix = !edit_unix;

            if (GuiButton((Rectangle){ td_x + 340*cfg.ui_scale, td_y + 165*cfg.ui_scale, 160*cfg.ui_scale, 32*cfg.ui_scale }, "Apply Epoch")) {
                is_auto_warping = false;
                double ep;
                if (sscanf(text_unix, "%lf", &ep) == 1) current_epoch = unix_to_epoch(ep);
                show_time_dialog = false;
            }
        }

        if (show_passes_dialog) {
            Rectangle titleBar = { pd_x, pd_y, passesWindow.width, 30 * cfg.ui_scale };
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), titleBar)) {
                drag_passes = true;
                drag_passes_off = Vector2Subtract(GetMousePosition(), (Vector2){pd_x, pd_y});
            }
            if (drag_passes) {
                pd_x = GetMousePosition().x - drag_passes_off.x;
                pd_y = GetMousePosition().y - drag_passes_off.y;
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) drag_passes = false;
            }

            if (GuiWindowBox(passesWindow, "Upcoming Passes (Home Location)")) show_passes_dialog = false;

            if (!selected_sat) {
                DrawUIText("Select a satellite.", pd_x + 20*cfg.ui_scale, pd_y + 40*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_main);
            } else if (num_passes == 0) {
                DrawUIText("No passes in the next 3 days.", pd_x + 20*cfg.ui_scale, pd_y + 40*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_main);
            } else {
                for (int i = 0; i < num_passes; i++) {
                    Rectangle row = { pd_x + 10*cfg.ui_scale, pd_y + (40 + i*55)*cfg.ui_scale, passesWindow.width - 20*cfg.ui_scale, 45*cfg.ui_scale };
                    Rectangle rowBtn = { row.x, row.y, row.width - 40*cfg.ui_scale, row.height };
                    Rectangle jumpBtn = { row.x + row.width - 35*cfg.ui_scale, row.y, 35*cfg.ui_scale, row.height };
                    
                    if (GuiButton(rowBtn, "")) {
                        show_polar_dialog = true;
                        selected_pass_idx = i;
                    }

                    if (GuiButton(jumpBtn, "#134#")) {
                        auto_warp_target = passes[i].aos_epoch;
                        auto_warp_initial_diff = (auto_warp_target - current_epoch) * 86400.0;
                        if (auto_warp_initial_diff > 0.0) {
                            is_auto_warping = true;
                            show_polar_dialog = true;
                            selected_pass_idx = 0; 
                        }
                    }
                    
                    char aos_str[16], los_str[16];
                    epoch_to_time_str(passes[i].aos_epoch, aos_str);
                    epoch_to_time_str(passes[i].los_epoch, los_str);
                    
                    char aos_info[32], los_info[32], max_info[32];
                    sprintf(aos_info, "#121# %s", aos_str);
                    sprintf(los_info, "#120# %s", los_str);
                    sprintf(max_info, "Max: %.1fdeg", passes[i].max_el);
                    
                    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(cfg.text_main));
                    
                    float lx = row.x + 10 * cfg.ui_scale;
                    float ly = row.y + 12 * cfg.ui_scale;
                    float lh = 20 * cfg.ui_scale;
                    
                    GuiLabel((Rectangle){ lx, ly, 120 * cfg.ui_scale, lh }, aos_info);
                    GuiLabel((Rectangle){ lx + 130 * cfg.ui_scale, ly, 120 * cfg.ui_scale, lh }, los_info);
                    GuiLabel((Rectangle){ lx + 260 * cfg.ui_scale, ly, 120 * cfg.ui_scale, lh }, max_info);

                    if (current_epoch >= passes[i].aos_epoch && current_epoch <= passes[i].los_epoch) {
                        float prog = (current_epoch - passes[i].aos_epoch) / (passes[i].los_epoch - passes[i].aos_epoch);
                        Rectangle pb_bg = { rowBtn.x + 10*cfg.ui_scale, rowBtn.y + 35*cfg.ui_scale, rowBtn.width - 20*cfg.ui_scale, 4*cfg.ui_scale };
                        Rectangle pb_fg = { pb_bg.x, pb_bg.y, pb_bg.width * prog, pb_bg.height };
                        DrawRectangleRec(pb_bg, cfg.ui_secondary);
                        DrawRectangleRec(pb_fg, cfg.ui_accent);
                    }
                }
            }
        }

        if (show_polar_dialog) {
            Rectangle titleBar = { pl_x, pl_y, polarWindow.width, 24 * cfg.ui_scale };
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), titleBar)) {
                drag_polar = true;
                drag_polar_off = Vector2Subtract(GetMousePosition(), (Vector2){pl_x, pl_y});
            }
            if (drag_polar) {
                pl_x = GetMousePosition().x - drag_polar_off.x;
                pl_y = GetMousePosition().y - drag_polar_off.y;
                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) drag_polar = false;
            }

            if (GuiWindowBox(polarWindow, "Polar Tracking Plot")) show_polar_dialog = false;

            if (selected_pass_idx >= 0 && selected_pass_idx < num_passes && selected_sat) {
                SatPass* p = &passes[selected_pass_idx];
                float cx = pl_x + polarWindow.width/2;
                float cy = pl_y + 160 * cfg.ui_scale;
                float r_max = 100 * cfg.ui_scale;

                // draw background grid for polar plot
                DrawCircleLines(cx, cy, r_max, cfg.ui_secondary); 
                DrawCircleLines(cx, cy, r_max * 0.666f, cfg.ui_secondary); 
                DrawCircleLines(cx, cy, r_max * 0.333f, cfg.ui_secondary); 
                DrawLine(cx - r_max, cy, cx + r_max, cy, cfg.ui_secondary);
                DrawLine(cx, cy - r_max, cx, cy + r_max, cfg.ui_secondary);
                
                DrawUIText("N", cx - 5*cfg.ui_scale, cy - r_max - 20*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
                DrawUIText("E", cx + r_max + 5*cfg.ui_scale, cy - 8*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
                DrawUIText("S", cx - 5*cfg.ui_scale, cy + r_max + 5*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
                DrawUIText("W", cx - r_max - 20*cfg.ui_scale, cy - 8*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);

                // draw the predicted path line
                for (int k = 0; k < p->num_pts - 1; k++) {
                    float az1 = p->path_pts[k].x, el1 = p->path_pts[k].y;
                    float az2 = p->path_pts[k+1].x, el2 = p->path_pts[k+1].y;
                    float r1 = r_max * (90 - el1)/90.0f;
                    float r2 = r_max * (90 - el2)/90.0f;
                    Vector2 pt1 = { cx + r1 * sin(az1*DEG2RAD), cy - r1 * cos(az1*DEG2RAD) };
                    Vector2 pt2 = { cx + r2 * sin(az2*DEG2RAD), cy - r2 * cos(az2*DEG2RAD) };
                    DrawLineEx(pt1, pt2, 2.0f, cfg.ui_accent);
                }

                if (current_epoch >= p->aos_epoch && current_epoch <= p->los_epoch) {
                    double t_unix = get_unix_from_epoch(current_epoch);
                    double gmst = epoch_to_gmst(current_epoch);
                    double c_az, c_el;
                    get_az_el(calculate_position(selected_sat, t_unix), gmst, home_location.lat, home_location.lon, &c_az, &c_el);
                    
                    float r_c = r_max * (90 - c_el)/90.0f;
                    Vector2 pt_c = { cx + r_c * sin(c_az*DEG2RAD), cy - r_c * cos(c_az*DEG2RAD) };
                    DrawCircleV(pt_c, 4.0f*cfg.ui_scale, RED);

                    char c_info[128];
                    sprintf(c_info, "Az: %05.1f deg  El: %04.1f deg", c_az, c_el);
                    DrawUIText(c_info, pl_x + 20*cfg.ui_scale, pl_y + 290*cfg.ui_scale, 18*cfg.ui_scale, cfg.text_main);

                    int sec_till_los = (int)((p->los_epoch - current_epoch)*86400.0);
                    char tl_info[64];
                    sprintf(tl_info, "LOS in: %02d:%02d", sec_till_los/60, sec_till_los%60);
                    DrawUIText(tl_info, pl_x + 20*cfg.ui_scale, pl_y + 315*cfg.ui_scale, 16*cfg.ui_scale, cfg.ui_accent);
                } else if (current_epoch < p->aos_epoch) {
                    int sec_till_aos = (int)((p->aos_epoch - current_epoch)*86400.0);
                    char tl_info[64];
                    sprintf(tl_info, "AOS in: %02d:%02d:%02d", sec_till_aos/3600, (sec_till_aos%3600)/60, sec_till_aos%60);
                    DrawUIText(tl_info, pl_x + 20*cfg.ui_scale, pl_y + 295*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
                } else {
                    DrawUIText("Pass Complete", pl_x + 20*cfg.ui_scale, pl_y + 295*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);
                }
            } else {
                DrawUIText("No valid pass selected.", pl_x + 20*cfg.ui_scale, pl_y + 40*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_main);
            }
        }

        // render satellite info box near the icon
        if (active_sat) {
            Vector2 screenPos;
            if (is_2d_view) {
                float sat_mx, sat_my;
                get_map_coordinates(active_sat->current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sat_mx, &sat_my);
                float cam_x = camera2d.target.x;
                
                while (sat_mx - cam_x > map_w/2.0f) sat_mx -= map_w;
                while (sat_mx - cam_x < -map_w/2.0f) sat_mx += map_w;
                
                screenPos = GetWorldToScreen2D((Vector2){sat_mx, sat_my}, camera2d);
            } else {
                screenPos = GetWorldToScreen(Vector3Scale(active_sat->current_pos, 1.0f/DRAW_SCALE), camera3d);
            }
            
            float boxW = 280*cfg.ui_scale;
            float boxH = 185*cfg.ui_scale;
            float boxX = screenPos.x + (15*cfg.ui_scale);
            float boxY = screenPos.y + (15*cfg.ui_scale);
            
            if (boxX + boxW > GetScreenWidth()) boxX = screenPos.x - boxW - (15*cfg.ui_scale);
            if (boxY + boxH > GetScreenHeight()) boxY = screenPos.y - boxH - (15*cfg.ui_scale);

            DrawRectangle(boxX - (5*cfg.ui_scale), boxY - (5*cfg.ui_scale), boxW, boxH, cfg.ui_bg);
            
            Color titleColor = (active_sat == hovered_sat) ? cfg.sat_highlighted : cfg.sat_selected;
            DrawUIText(active_sat->name, boxX, boxY, 20*cfg.ui_scale, titleColor);
            
            double r_km = Vector3Length(active_sat->current_pos);
            double v_kms = sqrt(MU * (2.0/r_km - 1.0/active_sat->semi_major_axis));
            float lat_deg = asinf(active_sat->current_pos.y / r_km) * RAD2DEG;
            float lon_deg = (atan2f(-active_sat->current_pos.z, active_sat->current_pos.x) - ((gmst_deg + cfg.earth_rotation_offset)*DEG2RAD)) * RAD2DEG;
            
            while (lon_deg > 180.0f) lon_deg -= 360.0f;
            while (lon_deg < -180.0f) lon_deg += 360.0f;

            char info[256];
            sprintf(info, "Inc: %.2f deg\nRAAN: %.2f deg\nEcc: %.5f\nAlt: %.2f km\nSpd: %.2f km/s\nLat: %.2f deg\nLon: %.2f deg", 
                    active_sat->inclination*RAD2DEG, active_sat->raan*RAD2DEG, active_sat->eccentricity, r_km-EARTH_RADIUS_KM, v_kms, lat_deg, lon_deg);
            DrawUIText(info, boxX, boxY + (28*cfg.ui_scale), 18*cfg.ui_scale, cfg.text_main);

            // draw textual altitude info at peri/apo points
            Vector2 periScreen, apoScreen;
            bool show_peri = true;
            bool show_apo = true;

            double t_peri_unix, t_apo_unix;
            get_apsis_times(active_sat, current_epoch, &t_peri_unix, &t_apo_unix);

            double real_rp = Vector3Length(calculate_position(active_sat, t_peri_unix));
            double real_ra = Vector3Length(calculate_position(active_sat, t_apo_unix));
            
            if (is_2d_view) {
                Vector2 p2, a2;
                get_apsis_2d(active_sat, current_epoch, false, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &p2);
                get_apsis_2d(active_sat, current_epoch, true, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &a2);
                float cam_x = camera2d.target.x;
                
                while (p2.x - cam_x > map_w/2.0f) p2.x -= map_w; 
                while (p2.x - cam_x < -map_w/2.0f) p2.x += map_w;
                while (a2.x - cam_x > map_w/2.0f) a2.x -= map_w; 
                while (a2.x - cam_x < -map_w/2.0f) a2.x += map_w;
                
                periScreen = GetWorldToScreen2D(p2, camera2d);
                apoScreen = GetWorldToScreen2D(a2, camera2d);
            } else {
                Vector3 draw_p = Vector3Scale(calculate_position(active_sat, t_peri_unix), 1.0f/DRAW_SCALE);
                Vector3 draw_a = Vector3Scale(calculate_position(active_sat, t_apo_unix), 1.0f/DRAW_SCALE);
                
                if (IsOccludedByEarth(camera3d.position, draw_p, draw_earth_radius)) show_peri = false;
                if (IsOccludedByEarth(camera3d.position, draw_a, draw_earth_radius)) show_apo = false;
                
                periScreen = GetWorldToScreen(draw_p, camera3d);
                apoScreen = GetWorldToScreen(draw_a, camera3d);
            }
            
            float text_size = 16.0f * cfg.ui_scale;
            float x_offset = 20.0f * cfg.ui_scale;
            float y_offset = text_size / 2.2f;

            if (show_peri) DrawUIText(TextFormat("Peri: %.0f km", real_rp-EARTH_RADIUS_KM), periScreen.x + x_offset, periScreen.y - y_offset, text_size, cfg.periapsis);
            if (show_apo) DrawUIText(TextFormat("Apo: %.0f km", real_ra-EARTH_RADIUS_KM), apoScreen.x + x_offset, apoScreen.y - y_offset, text_size, cfg.apoapsis);
        }

        // bottom right debug info
        DrawUIText(TextFormat("%3i FPS", GetFPS()), GetScreenWidth() - (90*cfg.ui_scale), 10*cfg.ui_scale, 20*cfg.ui_scale, cfg.sat_selected);
        DrawUIText(TextFormat("%i Sats", sat_count), GetScreenWidth() - (90*cfg.ui_scale), 34*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);

        EndDrawing();
    }

    // unload all graphical resources
    UnloadTexture(logoTex);
    UnloadTexture(satIcon);
    UnloadTexture(markerIcon);
    UnloadTexture(periMark);
    UnloadTexture(apoMark);
    UnloadTexture(earthTexture);
    UnloadTexture(earthNightTexture);
    UnloadModel(earthModel);
    UnloadShader(shader3D);
    UnloadShader(shader2D);
    UnloadShader(shaderCloud);
    UnloadTexture(cloudTexture);
    UnloadModel(cloudModel);
    UnloadTexture(moonTexture);
    UnloadModel(moonModel);
    UnloadFont(customFont);
    CloseWindow();
    return 0;
}