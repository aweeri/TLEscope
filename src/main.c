#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "types.h"
#include "config.h"
#include "astro.h"

// GLSL 330 fragment shader magic to blend day and night textures based on the sun's position relative
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

const char* fsCloud3D = 
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n" // cloud map
    "uniform vec3 sunDir;\n"
    "void main() {\n"
    "    vec4 texel = texture(texture0, fragTexCoord);\n"
    "    float theta = (fragTexCoord.x - 0.5) * 6.28318530718;\n"
    "    float phi = fragTexCoord.y * 3.14159265359;\n"
    "    vec3 normal = vec3(cos(theta)*sin(phi), cos(phi), -sin(theta)*sin(phi));\n"
    "    float intensity = dot(normal, sunDir);\n"
    "    \n"
    "    // Fade clouds to transparent on the night side \n"
    "    float alpha = smoothstep(-0.15, 0.05, intensity);\n"
    "    finalColor = vec4(texel.rgb, texel.a * alpha) * fragColor;\n"
    "}\n";

// defaults
static AppConfig cfg = {
    .window_width = 1280, .window_height = 720, .target_fps = 120, .ui_scale = 1.0f,
    .show_clouds = false, .show_night_lights = true,
    .bg_color = {0, 0, 0, 255}, .text_main = {255, 255, 255, 255}, .theme = "default"
};

static Font customFont;
static Texture2D satIcon, markerIcon, earthTexture, moonTexture, cloudTexture, earthNightTexture;
static Texture2D periMark, apoMark;
static Model earthModel, moonModel, cloudModel;

// safely modifies the alpha channel without relying on raylib's Fade()
static Color ApplyAlpha(Color c, float alpha) {
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    c.a = (unsigned char)(c.a * alpha);
    return c;
}

// check if the earth is blocking the view
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

// make the 3d earth sphere
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

// update a specific satellite's background orbit cache sequentially
static void update_orbit_cache(Satellite* sat, double current_epoch) {
    double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
    double time_step = period_days / (ORBIT_CACHE_SIZE - 1);
    for (int i = 0; i < ORBIT_CACHE_SIZE; i++) {
        double t = current_epoch + (i * time_step);
        double t_unix = get_unix_from_epoch(t);
        sat->orbit_cache[i] = Vector3Scale(calculate_position(sat, t_unix), 1.0f / DRAW_SCALE);
    }
    sat->orbit_cached = true;
}

// draw the orbit path ring either live or from cache
static void draw_orbit_3d(Satellite* sat, double current_epoch, bool is_highlighted, float alpha) {
    Color orbitColor = ApplyAlpha(is_highlighted ? cfg.orbit_highlighted : cfg.orbit_normal, alpha);

    if (is_highlighted) {
        // High freq bypass for active/selected target
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
        // Fast rendering via memory cache for idle targets
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

typedef enum { LOCK_NONE, LOCK_EARTH, LOCK_MOON } TargetLock;

static void DrawLoadingScreen(float progress, const char* message) {
    BeginDrawing();
    ClearBackground(cfg.bg_color);
    
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float barW = 400 * cfg.ui_scale;
    float barH = 30 * cfg.ui_scale;
    
    Rectangle barOutline = { (screenW - barW)/2, (screenH - barH)/2, barW, barH };
    Rectangle barProgress = { barOutline.x + 5, barOutline.y + 5, (barW - 10) * progress, barH - 10 };
    
    DrawRectangleLinesEx(barOutline, 2, cfg.text_main);
    DrawRectangleRec(barProgress, cfg.text_secondary);
    
    Vector2 msgSize = MeasureTextEx(customFont, message, 20 * cfg.ui_scale, 1.0f);
    DrawUIText(message, (screenW - msgSize.x)/2, barOutline.y - 40 * cfg.ui_scale, 20 * cfg.ui_scale, cfg.text_main);
    
    EndDrawing();
}

int main(void) {
    /*
    -----------------------------------------------------------------------------
    INITIALIZATION AND LOADING SEQUENCE HERE
    loading crap into VRAM is slow so here we show a lil loading screen~
    -----------------------------------------------------------------------------
    */
    LoadAppConfig("settings.json", &cfg);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(cfg.window_width, cfg.window_height, "TLEscope");
    Image logo = LoadImage("logo.png");
    if (logo.data != NULL) {
        SetWindowIcon(logo);
        UnloadImage(logo);
    }
    // Load font first so we can use it for the loading text (from the active theme)
    customFont = LoadFontEx(TextFormat("themes/%s/font.ttf", cfg.theme), 64, 0, 0);
    GenTextureMipmaps(&customFont.texture);
    SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
    GuiSetFont(customFont);

    DrawLoadingScreen(0.1f, "Fetching TLE Data...");
    load_tle_data("data.tle");

    DrawLoadingScreen(0.25f, "Initializing Textures...");
    earthTexture = LoadTexture(TextFormat("themes/%s/earth.png", cfg.theme));
    earthNightTexture = LoadTexture(TextFormat("themes/%s/earth_night.png", cfg.theme));
    
    DrawLoadingScreen(0.4f, "Compiling Shaders...");
    Shader shader3D = LoadShaderFromMemory(NULL, fs3D);
    int sunDirLoc3D = GetShaderLocation(shader3D, "sunDir");
    shader3D.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader3D, "texture1");

    Shader shader2D = LoadShaderFromMemory(NULL, fs2D);
    int sunDirLoc2D = GetShaderLocation(shader2D, "sunDir");
    int nightTexLoc2D = GetShaderLocation(shader2D, "texture1");

    Shader shaderCloud = LoadShaderFromMemory(NULL, fsCloud3D);
    int sunDirLocCloud = GetShaderLocation(shaderCloud, "sunDir");
    
    DrawLoadingScreen(0.6f, "Generating Meshes...");
    float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
    Mesh sphereMesh = GenEarthMesh(draw_earth_radius, 64, 64);
    earthModel = LoadModelFromMesh(sphereMesh);
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
    earthModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = earthNightTexture;
    Shader defaultEarthShader = earthModel.materials[0].shader;
    
    DrawLoadingScreen(0.8f, "Loading Celestial Bodies...");
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

    DrawLoadingScreen(0.95f, "Finalizing UI...");
    satIcon = LoadTexture(TextFormat("themes/%s/sat_icon.png", cfg.theme));
    markerIcon = LoadTexture(TextFormat("themes/%s/marker_icon.png", cfg.theme));
    periMark = LoadTexture(TextFormat("themes/%s/smallmark.png", cfg.theme));
    apoMark = LoadTexture(TextFormat("themes/%s/smallmark.png", cfg.theme));
    
    SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(periMark, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(apoMark, TEXTURE_FILTER_BILINEAR);

    DrawLoadingScreen(1.0f, "Ready!");
    /*
    -----------------------------------------------------------------------------
    stuff after loading and initialization, like setting up the cameras and main loop variables
    -----------------------------------------------------------------------------
    */

    // set up the cameras for both views
    Camera camera3d = { 0 };
    camera3d.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera3d.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera3d.fovy = 45.0f;
    camera3d.projection = CAMERA_PERSPECTIVE;

    Camera2D camera2d = { 0 };
    camera2d.zoom = 1.0f;
    camera2d.offset = (Vector2){ cfg.window_width / 2.0f, cfg.window_height / 2.0f };
    camera2d.target = (Vector2){ 0.0f, 0.0f };

    // smooth camera targets for 2d
    float target_camera2d_zoom = camera2d.zoom;
    Vector2 target_camera2d_target = camera2d.target;

    float map_w = 2048.0f, map_h = 1024.0f;
    float camDistance = 35.0f, camAngleX = 0.785f, camAngleY = 0.5f;

    // smooth camera targets for 3d
    float target_camDistance = camDistance;
    float target_camAngleX = camAngleX;
    float target_camAngleY = camAngleY;
    Vector3 target_camera3d_target = camera3d.target;

    double current_epoch = get_current_real_time_epoch();
    double time_multiplier = 1.0; 
    bool paused = false, is_2d_view = false;
    
    // checkbox and fading state
    bool hide_unselected = false;
    float unselected_fade = 1.0f;

    Satellite* hovered_sat = NULL;
    Satellite* selected_sat = NULL;
    TargetLock active_lock = LOCK_EARTH;
    double last_left_click_time = 0.0;

    

    SetTargetFPS(cfg.target_fps);

    int current_update_idx = 0;

    while (!WindowShouldClose()) {
        /*
        -----------------------------------------------------------------------------
        main simulation loop starts here. 
        all the input handling, updating, and drawing happens in this loop, on each frame drawn
        -----------------------------------------------------------------------------
        */

        // keyboard stuff
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_PERIOD)) time_multiplier *= 2.0;
        if (IsKeyPressed(KEY_COMMA)) time_multiplier /= 2.0;
        if (IsKeyPressed(KEY_M)) is_2d_view = !is_2d_view;
        
        if (IsKeyPressed(KEY_SLASH)) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) current_epoch = get_current_real_time_epoch(); 
            else time_multiplier = 1.0; 
        }

        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) cfg.ui_scale += 0.1f;
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) cfg.ui_scale -= 0.1f;
        if (cfg.ui_scale < 0.5f) cfg.ui_scale = 0.5f;
        if (cfg.ui_scale > 4.0f) cfg.ui_scale = 4.0f;

        if (!paused) {
            current_epoch += (GetFrameTime() * time_multiplier) / 86400.0; 
            current_epoch = normalize_epoch(current_epoch);
        }

        // Sequential orbit cache updater
        if (sat_count > 0) {
            int updates_per_frame = 50; 
            for (int i = 0; i < updates_per_frame; i++) {
                update_orbit_cache(&satellites[current_update_idx], current_epoch);
                current_update_idx = (current_update_idx + 1) % sat_count;
            }
        }

        double current_unix = get_unix_from_epoch(current_epoch);

        // precalculate all satellite positions for the current frame
        for (int i = 0; i < sat_count; i++) {
            // don't waste time calculating if it's hidden and not selected
            if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;
            satellites[i].current_pos = calculate_position(&satellites[i], current_unix);
        }

        // fading math
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

        Vector3 moon_pos_km = calculate_moon_position(current_epoch);
        Vector3 draw_moon_pos = Vector3Scale(moon_pos_km, 1.0f / DRAW_SCALE);
        float moon_mx, moon_my;
        get_map_coordinates(moon_pos_km, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &moon_mx, &moon_my);

        // Apply tidal lock mapping for the moon texture pointing to origin 0,0,0
        Vector3 dirToEarth = Vector3Normalize(Vector3Negate(draw_moon_pos));
        float moon_yaw = atan2f(-dirToEarth.z, dirToEarth.x);
        float moon_pitch = asinf(dirToEarth.y);
        moonModel.transform = MatrixMultiply(MatrixRotateZ(moon_pitch), MatrixRotateY(moon_yaw));

        Vector2 mouseDelta = GetMouseDelta();
        hovered_sat = NULL;

        // moving the camera around smoothly via targets, split between 2d and 3d controls
        if (is_2d_view) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && IsKeyDown(KEY_LEFT_SHIFT))) {
                target_camera2d_target = Vector2Add(target_camera2d_target, Vector2Scale(mouseDelta, -1.0f / target_camera2d_zoom));
                active_lock = LOCK_NONE;
            }
            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera2d);
                camera2d.offset = GetMousePosition();
                camera2d.target = mouseWorldPos;
                target_camera2d_target = mouseWorldPos; // align target
                target_camera2d_zoom += wheel * 0.1f * target_camera2d_zoom;
                if (target_camera2d_zoom < 0.1f) target_camera2d_zoom = 0.1f;
                active_lock = LOCK_NONE;
            }

            Vector2 mousePos = GetMousePosition();
            float closest_dist = 9999.0f;
            float hit_radius_pixels = 12.0f * cfg.ui_scale; // hitbox size in screen pixels

            for (int i = 0; i < sat_count; i++) {
                // prevent raycasting on hidden satellites
                if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;
                
                float mx, my;
                get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &mx, &my);
                
                Vector2 screenPos = GetWorldToScreen2D((Vector2){mx, my}, camera2d);
                float dist = Vector2Distance(mousePos, screenPos);
                
                if (dist < hit_radius_pixels && dist < closest_dist) { closest_dist = dist; hovered_sat = &satellites[i]; }
            }
        } else {
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
            target_camDistance -= GetMouseWheelMove() * (target_camDistance * 0.1f);
            if (target_camDistance < draw_earth_radius + 1.0f) target_camDistance = draw_earth_radius + 1.0f;

            Ray mouseRay = GetMouseRay(GetMousePosition(), camera3d);
            float closest_dist = 9999.0f;

            for (int i = 0; i < sat_count; i++) {
                // Prevent raycasting on fading/hidden satellites
                if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;

                Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);

                // fast coarse culling via squared distance to avoid expensive sphere collision checks on distant satellites
                if (Vector3DistanceSqr(camera3d.target, draw_pos) > (camDistance * camDistance * 16.0f)) continue; 
                
                // adjust sphere radius based on distance to maintain consistent screen-size hitbox
                float distToCam = Vector3Distance(camera3d.position, draw_pos);
                float hit_radius_3d = 0.015f * distToCam * cfg.ui_scale;

                RayCollision col = GetRayCollisionSphere(mouseRay, draw_pos, hit_radius_3d); 
                if (col.hit && col.distance < closest_dist) { closest_dist = col.distance; hovered_sat = &satellites[i]; }
            }
        }

        // defining a hitbox area to cover the UI checkboxes + texts
        Rectangle cbHitbox = { 10 * cfg.ui_scale, (10 + 104) * cfg.ui_scale, 200 * cfg.ui_scale, 74 * cfg.ui_scale };
        

        // shooting left and right like its an american high school prom night
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            /* only update selection if the mouse is not clicking on the UI Checkboxes (wouldn't want to hurt them, 
            they're just trying to do their job) */
            if (!CheckCollisionPointRec(GetMousePosition(), cbHitbox)) {
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

        if (active_lock == LOCK_EARTH) {
            if (is_2d_view) target_camera2d_target = Vector2Zero();
            else target_camera3d_target = Vector3Zero();
        } else if (active_lock == LOCK_MOON) {
            if (is_2d_view) target_camera2d_target = (Vector2){moon_mx, moon_my};
            else target_camera3d_target = draw_moon_pos;
        }

        // apply the smooth lerping (is just linear interpolation for smoofness :D)
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

        // math to draw the footprint ring 
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

        // render time! yay
        BeginDrawing();
        ClearBackground(cfg.bg_color);

        float m_size_2d = 24.0f * cfg.ui_scale / camera2d.zoom;
        float m_text_2d = 16.0f * cfg.ui_scale / camera2d.zoom;
        float mark_size_2d = 32.0f * cfg.ui_scale / camera2d.zoom;

        /*
        -----------------------------------------------------------------------------
        drawing the 2d and 3d views depending on what's currently active;
        they share some of the same logic but are separate in how they render things
        -----------------------------------------------------------------------------
        */
        if (is_2d_view) {
            // drawing the flat map
            BeginMode2D(camera2d);
                if (cfg.show_night_lights) {
                    BeginShaderMode(shader2D);
                    SetShaderValueTexture(shader2D, nightTexLoc2D, earthNightTexture);
                    
                    Vector3 sunEci = calculate_sun_position(current_epoch);
                    float earth_rot_rad = (gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                    // rotate Sun vector by -GMST to get ECEF mapping for 2d 
                    Vector3 sunEcef = Vector3Transform(sunEci, MatrixRotateY(-earth_rot_rad));
                    SetShaderValue(shader2D, sunDirLoc2D, &sunEcef, SHADER_UNIFORM_VEC3);
                }

                DrawTexturePro(earthTexture, (Rectangle){0, 0, earthTexture.width, earthTexture.height}, 
                    (Rectangle){-map_w/2, -map_h/2, map_w, map_h}, (Vector2){0,0}, 0.0f, WHITE);

                if (cfg.show_night_lights) {
                    EndShaderMode();
                }

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

                    for (int i = 0; i < sat_count; i++) {
                        bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                        float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                        if (sat_alpha <= 0.0f) continue;

                        bool is_hl = (active_sat == &satellites[i]);
                        Color sCol = (selected_sat == &satellites[i]) ? cfg.sat_selected : (hovered_sat == &satellites[i]) ? cfg.sat_highlighted : cfg.sat_normal;
                        sCol = ApplyAlpha(sCol, sat_alpha);

                        if (is_hl) {
                            int segments = fmin(4000, fmax(50, (int)(400 * cfg.orbits_to_draw))); 
                            Vector2 track_pts[4001]; // must be segments + 1

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

                        // draw all the little satellite icons
                        float sat_mx, sat_my;
                        get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sat_mx, &sat_my);
                        for (int offset_i = -1; offset_i <= 1; offset_i++) {
                            DrawTexturePro(satIcon, (Rectangle){0,0,satIcon.width,satIcon.height}, 
                                (Rectangle){sat_mx+(offset_i*map_w), sat_my, m_size_2d, m_size_2d}, (Vector2){m_size_2d/2.f, m_size_2d/2.f}, 0.0f, sCol);
                        }
                    }

                    for (int m = 0; m < marker_count; m++) {
                        float mx = (markers[m].lon / 360.0f) * map_w;
                        float my = -(markers[m].lat / 180.0f) * map_h;
                        for (int offset_i = -1; offset_i <= 1; offset_i++) {
                            float x_off = offset_i * map_w;
                            DrawTexturePro(markerIcon, (Rectangle){0,0,markerIcon.width,markerIcon.height}, 
                                (Rectangle){mx+x_off, my, m_size_2d, m_size_2d}, (Vector2){m_size_2d/2.f, m_size_2d/2.f}, 0.0f, WHITE);
                            
                            // only draw text if zoomed in enough
                            if (camera2d.zoom > 0.1f) {
                                DrawUIText(markers[m].name, mx+x_off+(m_size_2d/2.f)+4.f, my-(m_size_2d/2.f), m_text_2d, WHITE);
                            }
                        }
                    }

                    EndScissorMode();
                }
            EndMode2D();
        } else {
            // drawing the 3d view
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

                // render Moon (not sure what for tbh but I'm sure someone will appreciate it :3)
                DrawModel(moonModel, draw_moon_pos, 1.0f, WHITE);

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
            
            // marker scale and occlusion

            float m_size_3d = 24.0f * cfg.ui_scale;
            float m_text_3d = 16.0f * cfg.ui_scale;
            float mark_size_3d = 32.0f * cfg.ui_scale;

            if (active_sat) {
                bool is_unselected = (selected_sat != NULL && active_sat != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                
                double delta_time_s_curr = (current_epoch - active_sat->epoch_days) * 86400.0;
                double M_curr = fmod(active_sat->mean_anomaly + active_sat->mean_motion * delta_time_s_curr, 2.0 * PI);
                if (M_curr < 0) M_curr += 2.0 * PI;
                
                double diff_peri = 0.0 - M_curr; if (diff_peri < 0) diff_peri += 2.0 * PI;
                double diff_apo = PI - M_curr; if (diff_apo < 0) diff_apo += 2.0 * PI;
                
                double t_peri = current_epoch + (diff_peri / active_sat->mean_motion) / 86400.0;
                double t_apo = current_epoch + (diff_apo / active_sat->mean_motion) / 86400.0;
                double t_peri_unix = get_unix_from_epoch(t_peri);
                double t_apo_unix = get_unix_from_epoch(t_apo);

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

            for (int m = 0; m < marker_count; m++) {
                float lat_rad = markers[m].lat * DEG2RAD, lon_rad = (markers[m].lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                Vector3 m_pos = { cosf(lat_rad)*cosf(lon_rad)*draw_earth_radius, sinf(lat_rad)*draw_earth_radius, -cosf(lat_rad)*sinf(lon_rad)*draw_earth_radius };
                Vector3 normal = Vector3Normalize(m_pos);
                Vector3 viewDir = Vector3Normalize(Vector3Subtract(camera3d.position, m_pos));
                Vector3 toTarget = Vector3Subtract(m_pos, camera3d.position);
                Vector3 camForward = Vector3Normalize(Vector3Subtract(camera3d.target, camera3d.position));

                if (Vector3DotProduct(normal, viewDir) > 0.0f && Vector3DotProduct(toTarget, camForward) > 0.0f) {
                    Vector2 sp = GetWorldToScreen(m_pos, camera3d);
                    DrawTexturePro(markerIcon, (Rectangle){0,0,markerIcon.width,markerIcon.height}, 
                        (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d/2.f, m_size_3d/2.f}, 0.0f, WHITE);
                    
                    // only draw text if zoomed in enough
                    if (camDistance < 50.0f) {
                        DrawUIText(markers[m].name, sp.x+(m_size_3d/2.f)+4.f, sp.y-(m_size_3d/2.f), m_text_3d, WHITE);
                    }
                }
            }
        }


        /*
        -----------------------------------------------------------------------------
        aaaand finally the UI layer!!!! this is easy, just some text and checkboxes, no big deal~
        also includes the logic for the "hide unselected" feature which fades out non-selected satellites when enabled,
        gonna move that out eventually TODO
        -----------------------------------------------------------------------------
        */

        // text overlays for the ui
        DrawUIText(is_2d_view ? "Controls: RMB to pan, Scroll to zoom. 'M' switches to 3D. Space: Pause." : "Controls: RMB to orbit, Shift+RMB to pan. 'M' switches to 2D. Space: Pause.", 10*cfg.ui_scale, 10*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_secondary);
        DrawUIText("Time: '.' (Faster 2x), ',' (Slower 0.5x), '/' (1x Speed), 'Shift+/' (Reset)", 10*cfg.ui_scale, (10+24)*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_secondary);
        DrawUIText(TextFormat("UI Scale: '-' / '+' (%.1fx)", cfg.ui_scale), 10*cfg.ui_scale, (10+48)*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_secondary);
        DrawUIText(TextFormat("%s | Speed: %.1fx %s", datetime_str, time_multiplier, paused ? "[PAUSED]" : ""), 10*cfg.ui_scale, (10+76)*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_main);

        // raygui configuration and rendering
        GuiSetStyle(DEFAULT, TEXT_SIZE, 16 * cfg.ui_scale);
        GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(cfg.text_main));
        GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(cfg.text_main));
        GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(cfg.text_main));
        
        Rectangle cbRec = { 10 * cfg.ui_scale, (10 + 104) * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
        GuiCheckBox(cbRec, "Hide Unselected", &hide_unselected);

        Rectangle cbRec2 = { 10 * cfg.ui_scale, (10 + 128) * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
        GuiCheckBox(cbRec2, "Show Clouds", &cfg.show_clouds);

        Rectangle cbRec3 = { 10 * cfg.ui_scale, (10 + 152) * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
        GuiCheckBox(cbRec3, "Night Lights", &cfg.show_night_lights);

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

            Vector2 periScreen, apoScreen;
            bool show_peri = true;
            bool show_apo = true;

            // calculate the time of periapsis and apoapsis passages and their current positions for display in the UI
            double delta_time_s_curr = (current_epoch - active_sat->epoch_days) * 86400.0;
            double M_curr = fmod(active_sat->mean_anomaly + active_sat->mean_motion * delta_time_s_curr, 2.0 * PI);
            if (M_curr < 0) M_curr += 2.0 * PI;
            
            double diff_peri = 0.0 - M_curr; if (diff_peri < 0) diff_peri += 2.0 * PI;
            double diff_apo = PI - M_curr; if (diff_apo < 0) diff_apo += 2.0 * PI;
            
            double t_peri = current_epoch + (diff_peri / active_sat->mean_motion) / 86400.0;
            double t_apo = current_epoch + (diff_apo / active_sat->mean_motion) / 86400.0;
            double t_peri_unix = get_unix_from_epoch(t_peri);
            double t_apo_unix = get_unix_from_epoch(t_apo);

            double real_rp = Vector3Length(calculate_position(active_sat, t_peri_unix));
            double real_ra = Vector3Length(calculate_position(active_sat, t_apo_unix));
            // hide the periapsis/apoapsis markers if they're currently behind the Earth in the 3D view, or just off the edge of the map in 2D
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

        // performance stats :3
        DrawUIText(TextFormat("%3i FPS", GetFPS()), GetScreenWidth() - (90*cfg.ui_scale), 10*cfg.ui_scale, 20*cfg.ui_scale, cfg.sat_selected);
        DrawUIText(TextFormat("%i Sats", sat_count), GetScreenWidth() - (90*cfg.ui_scale), 34*cfg.ui_scale, 16*cfg.ui_scale, cfg.text_secondary);

        EndDrawing();
    }

    // cleanup time before closing
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