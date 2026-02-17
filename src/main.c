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

// defaults
static AppConfig cfg = {
    .window_width = 1280, .window_height = 720, .target_fps = 120, .ui_scale = 1.0f,
    .bg_color = {0, 0, 0, 255}, .text_main = {255, 255, 255, 255}
};

static Font customFont;
static Texture2D satIcon, markerIcon, earthTexture, moonTexture;
static Model earthModel, moonModel;

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

// draw the orbit path ring
static void draw_orbit_3d(Satellite* sat, bool is_highlighted, float alpha) {
    Vector3 prev_pos = {0};
    int segments = 100;
    Color orbitColor = is_highlighted ? cfg.orbit_highlighted : cfg.orbit_normal;
    orbitColor = ApplyAlpha(orbitColor, alpha);

    for (int i = 0; i <= segments; i++) {
        double E = (2.0 * PI * i) / segments;
        double nu = 2.0 * atan2(sqrt(1.0 + sat->eccentricity) * sin(E / 2.0), 
                                sqrt(1.0 - sat->eccentricity) * cos(E / 2.0));
        double r = sat->semi_major_axis * (1.0 - sat->eccentricity * cos(E));
        
        double x_orb = r * cos(nu);
        double y_orb = r * sin(nu);
        double cw = cos(sat->arg_perigee), sw = sin(sat->arg_perigee);
        double cO = cos(sat->raan), sO = sin(sat->raan);
        double ci = cos(sat->inclination), si = sin(sat->inclination);

        Vector3 pos;
        pos.x = (cO * cw - sO * sw * ci) * x_orb + (-cO * sw - sO * cw * ci) * y_orb;
        pos.y = (sw * si) * x_orb + (cw * si) * y_orb;
        pos.z = -((sO * cw + cO * sw * ci) * x_orb + (-sO * sw + cO * cw * ci) * y_orb);
        pos = Vector3Scale(pos, 1.0f / DRAW_SCALE);

        if (i > 0) DrawLine3D(prev_pos, pos, orbitColor);
        prev_pos = pos;
    }
}

static void DrawUIText(const char* text, float x, float y, float size, Color color) {
    DrawTextEx(customFont, text, (Vector2){x, y}, size, 1.0f, color);
}

int main(void) {
    LoadAppConfig("settings.json", &cfg);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(cfg.window_width, cfg.window_height, "TLEScope 3.0.1");

    // load our font and icons
    customFont = LoadFontEx("resources/font.ttf", 64, 0, 0); 
    GenTextureMipmaps(&customFont.texture); 
    SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
    GuiSetFont(customFont); // Ensure raygui uses the custom font

    satIcon = LoadTexture("resources/sat_icon.png");
    markerIcon = LoadTexture("resources/marker_icon.png");
    SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);

    load_tle_data("resources/data.tle");

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

    float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
    Mesh sphereMesh = GenEarthMesh(draw_earth_radius, 64, 64);
    earthModel = LoadModelFromMesh(sphereMesh);
    earthTexture = LoadTexture("resources/earth.png");
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;

    float draw_moon_radius = MOON_RADIUS_KM / DRAW_SCALE;
    Mesh moonMesh = GenEarthMesh(draw_moon_radius, 32, 32); 
    moonModel = LoadModelFromMesh(moonMesh);
    moonTexture = LoadTexture("resources/moon.jpg");
    moonModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture;

    float map_w = 2048.0f, map_h = 1024.0f;
    float camDistance = 35.0f, camAngleX = 0.785f, camAngleY = 0.5f;

    double current_epoch = get_current_real_time_epoch();
    double time_multiplier = 1.0; 
    bool paused = false, is_2d_view = false;
    
    // checkbox and fading state
    bool hide_unselected = false;
    float unselected_fade = 1.0f;

    Satellite* hovered_sat = NULL;
    Satellite* selected_sat = NULL;

    SetTargetFPS(cfg.target_fps);

    while (!WindowShouldClose()) {
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

        // Apply tidal lock mapping for the moon texture pointing to origin 0,0,0
        Vector3 dirToEarth = Vector3Normalize(Vector3Negate(draw_moon_pos));
        float moon_yaw = atan2f(-dirToEarth.z, dirToEarth.x);
        float moon_pitch = asinf(dirToEarth.y);
        moonModel.transform = MatrixMultiply(MatrixRotateZ(moon_pitch), MatrixRotateY(moon_yaw));

        Vector2 mouseDelta = GetMouseDelta();
        hovered_sat = NULL;
        float hit_radius = 0.4f * cfg.ui_scale;

        // moving the camera around
        if (is_2d_view) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && IsKeyDown(KEY_LEFT_SHIFT))) {
                camera2d.target = Vector2Add(camera2d.target, Vector2Scale(mouseDelta, -1.0f / camera2d.zoom));
            }
            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera2d);
                camera2d.offset = GetMousePosition();
                camera2d.target = mouseWorldPos;
                camera2d.zoom += wheel * 0.1f * camera2d.zoom;
                if (camera2d.zoom < 0.1f) camera2d.zoom = 0.1f;
            }

            Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera2d);
            float closest_dist = 9999.0f, hit_radius_2d = 10.0f * cfg.ui_scale / camera2d.zoom; 

            for (int i = 0; i < sat_count; i++) {
                satellites[i].current_pos = calculate_position(&satellites[i], current_epoch);
                // prevent raycasting on hidden satellites satellites
                if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;
                
                float mx, my;
                get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &mx, &my);
                float dist = Vector2Distance(mouseWorld, (Vector2){mx, my});
                if (dist < hit_radius_2d && dist < closest_dist) { closest_dist = dist; hovered_sat = &satellites[i]; }
            }
        } else {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                if (IsKeyDown(KEY_LEFT_SHIFT)) {
                    Vector3 forward = Vector3Normalize(Vector3Subtract(camera3d.target, camera3d.position));
                    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera3d.up));
                    Vector3 upVector = Vector3Normalize(Vector3CrossProduct(right, forward));
                    float panSpeed = camDistance * 0.001f; 
                    camera3d.target = Vector3Add(camera3d.target, Vector3Scale(right, -mouseDelta.x * panSpeed));
                    camera3d.target = Vector3Add(camera3d.target, Vector3Scale(upVector, mouseDelta.y * panSpeed));
                } else {
                    camAngleX -= mouseDelta.x * 0.005f;
                    camAngleY += mouseDelta.y * 0.005f;
                    if (camAngleY > 1.5f) camAngleY = 1.5f;
                    if (camAngleY < -1.5f) camAngleY = -1.5f;
                }
            }
            camDistance -= GetMouseWheelMove() * (camDistance * 0.1f);
            if (camDistance < draw_earth_radius + 1.0f) camDistance = draw_earth_radius + 1.0f;

            camera3d.position.x = camera3d.target.x + camDistance * cosf(camAngleY) * sinf(camAngleX);
            camera3d.position.y = camera3d.target.y + camDistance * sinf(camAngleY);
            camera3d.position.z = camera3d.target.z + camDistance * cosf(camAngleY) * cosf(camAngleX);

            Ray mouseRay = GetMouseRay(GetMousePosition(), camera3d);
            float closest_dist = 9999.0f;

            for (int i = 0; i < sat_count; i++) {
                satellites[i].current_pos = calculate_position(&satellites[i], current_epoch);
                // Prevent raycasting on fading/hidden satellites
                if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat) continue;

                Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                RayCollision col = GetRayCollisionSphere(mouseRay, draw_pos, hit_radius); 
                if (col.hit && col.distance < closest_dist) { closest_dist = col.distance; hovered_sat = &satellites[i]; }
            }
        }

        // defining a hitbox area to cover the UI checkbox + its text
        Rectangle cbHitbox = { 10 * cfg.ui_scale, (10 + 104) * cfg.ui_scale, 200 * cfg.ui_scale, 24 * cfg.ui_scale };

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // only update selection if the mouse is not clicking on the UI Checkbox
            if (!CheckCollisionPointRec(GetMousePosition(), cbHitbox)) {
                selected_sat = hovered_sat; 
            }
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
        float marker_radius_2d = fmaxf(1.0f, 4.0f * cfg.ui_scale / camera2d.zoom);

        if (is_2d_view) {
            // drawing the flat map
            BeginMode2D(camera2d);
                DrawTexturePro(earthTexture, (Rectangle){0, 0, earthTexture.width, earthTexture.height}, 
                    (Rectangle){-map_w/2, -map_h/2, map_w, map_h}, (Vector2){0,0}, 0.0f, WHITE);

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

                            // grab the current mean and eccentric anomaly to start the track from the exact current position
                            double delta_time_s_curr = (current_epoch - satellites[i].epoch_days) * 86400.0;
                            double M_curr = fmod(satellites[i].mean_anomaly + satellites[i].mean_motion * delta_time_s_curr, 2.0 * PI);
                            if (M_curr < 0) M_curr += 2.0 * PI;

                            double E_curr = M_curr;
                            for (int k = 0; k < 10; k++) {
                                double delta = (E_curr - satellites[i].eccentricity * sin(E_curr) - M_curr) / (1.0 - satellites[i].eccentricity * cos(E_curr));
                                E_curr -= delta;
                                if (fabs(delta) < 1e-6) break;
                            }

                            // step through eccentric anomaly instead of time for smoother curves at periapsis
                            for (int j = 0; j <= segments; j++) {
                                double progress = (double)j / segments;
                                double E_target = E_curr + (progress * 2.0 * PI * cfg.orbits_to_draw);
                                
                                // work backward to mean anomaly and then to time
                                double M_target = E_target - satellites[i].eccentricity * sin(E_target);
                                double delta_M = M_target - M_curr;
                                double t = current_epoch + (delta_M / satellites[i].mean_motion) / 86400.0;
                                
                                get_map_coordinates(calculate_position(&satellites[i], t), epoch_to_gmst(t), cfg.earth_rotation_offset, map_w, map_h, &track_pts[j].x, &track_pts[j].y);
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
                                DrawCircleV((Vector2){peri2d.x + x_off, peri2d.y}, marker_radius_2d, ApplyAlpha(cfg.periapsis, sat_alpha));
                                DrawCircleV((Vector2){apo2d.x + x_off, apo2d.y}, marker_radius_2d, ApplyAlpha(cfg.apoapsis, sat_alpha));
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
                            DrawUIText(markers[m].name, mx+x_off+(m_size_2d/2.f)+4.f, my-(m_size_2d/2.f), m_text_2d, WHITE);
                        }
                    }

                    // Render sub-lunar point ground track
                    float moon_mx, moon_my;
                    get_map_coordinates(moon_pos_km, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &moon_mx, &moon_my);
                    for (int offset_i = -1; offset_i <= 1; offset_i++) {
                        float x_off = offset_i * map_w;
                        DrawCircleV((Vector2){moon_mx + x_off, moon_my}, 6.0f * cfg.ui_scale / camera2d.zoom, LIGHTGRAY);
                        DrawUIText("MOON", moon_mx + x_off + 10.f, moon_my - 8.f, 16.0f * cfg.ui_scale / camera2d.zoom, LIGHTGRAY);
                    }

                    EndScissorMode();
                }
            EndMode2D();
        } else {
            // drawing the 3d view
            BeginMode3D(camera3d);
                earthModel.transform = MatrixRotateY((gmst_deg + cfg.earth_rotation_offset) * DEG2RAD);
                DrawModel(earthModel, Vector3Zero(), 1.0f, WHITE);

                // Render Moon Model
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
                    draw_orbit_3d(&satellites[i], is_hl, sat_alpha);

                    if (is_hl) {
                        Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                        DrawLine3D(Vector3Zero(), draw_pos, ApplyAlpha(cfg.orbit_highlighted, sat_alpha));

                        double rp = satellites[i].semi_major_axis * (1.0 - satellites[i].eccentricity);
                        double ra = satellites[i].semi_major_axis * (1.0 + satellites[i].eccentricity);
                        double cw = cos(satellites[i].arg_perigee), sw = sin(satellites[i].arg_perigee);
                        double cO = cos(satellites[i].raan), sO = sin(satellites[i].raan);
                        double ci = cos(satellites[i].inclination), si = sin(satellites[i].inclination);

                        Vector3 periVec = { (cO*cw - sO*sw*ci)*rp, (sw*si)*rp, -((sO*cw + cO*sw*ci)*rp) };
                        Vector3 apoVec = { (cO*cw - sO*sw*ci)*(-ra), (sw*si)*(-ra), -((sO*cw + cO*sw*ci)*(-ra)) };

                        DrawSphere(Vector3Scale(periVec, 1.0f/DRAW_SCALE), 0.08f*cfg.ui_scale, ApplyAlpha(cfg.periapsis, sat_alpha)); 
                        DrawSphere(Vector3Scale(apoVec, 1.0f/DRAW_SCALE), 0.08f*cfg.ui_scale, ApplyAlpha(cfg.apoapsis, sat_alpha)); 
                    }
                }
            EndMode3D();

            float m_size_3d = 24.0f * cfg.ui_scale;
            float m_text_3d = 16.0f * cfg.ui_scale;

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
                    DrawUIText(markers[m].name, sp.x+(m_size_3d/2.f)+4.f, sp.y-(m_size_3d/2.f), m_text_3d, WHITE);
                }
            }
        }

        // text overlays for the ui
        DrawUIText(is_2d_view ? "Controls: RMB to pan, Scroll to zoom. 'M' switches to 3D. Space: Pause." : "Controls: RMB to orbit, Shift+RMB to pan. 'M' switches to 2D. Space: Pause.", 10*cfg.ui_scale, 10*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_secondary);
        DrawUIText("Time: '.' (Faster 2x), ',' (Slower 0.5x), '/' (1x Speed), 'Shift+/' (Reset)", 10*cfg.ui_scale, (10+24)*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_secondary);
        DrawUIText(TextFormat("UI Scale: '-' / '+' (%.1fx) | Offset Load: %.1f deg", cfg.ui_scale, cfg.earth_rotation_offset), 10*cfg.ui_scale, (10+48)*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_secondary);
        DrawUIText(TextFormat("%s | Speed: %.1fx %s", datetime_str, time_multiplier, paused ? "[PAUSED]" : ""), 10*cfg.ui_scale, (10+76)*cfg.ui_scale, 20*cfg.ui_scale, cfg.text_main);

        // raygui configuration and rendering
        GuiSetStyle(DEFAULT, TEXT_SIZE, 16 * cfg.ui_scale);
        GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(cfg.text_main));
        GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(cfg.text_main));
        GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(cfg.text_main));
        
        Rectangle cbRec = { 10 * cfg.ui_scale, (10 + 104) * cfg.ui_scale, 20 * cfg.ui_scale, 20 * cfg.ui_scale };
        GuiCheckBox(cbRec, "Hide Unselected", &hide_unselected);

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

            double rp = active_sat->semi_major_axis * (1.0 - active_sat->eccentricity);
            double ra = active_sat->semi_major_axis * (1.0 + active_sat->eccentricity);
            Vector2 periScreen, apoScreen;
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
                double cw = cos(active_sat->arg_perigee), sw = sin(active_sat->arg_perigee);
                double cO = cos(active_sat->raan), sO = sin(active_sat->raan);
                double ci = cos(active_sat->inclination), si = sin(active_sat->inclination);
                Vector3 periVec = { (cO*cw - sO*sw*ci)*rp, (sw*si)*rp, -((sO*cw + cO*sw*ci)*rp) };
                Vector3 apoVec = { (cO*cw - sO*sw*ci)*(-ra), (sw*si)*(-ra), -((sO*cw + cO*sw*ci)*(-ra)) };
                periScreen = GetWorldToScreen(Vector3Scale(periVec, 1.0f/DRAW_SCALE), camera3d);
                apoScreen = GetWorldToScreen(Vector3Scale(apoVec, 1.0f/DRAW_SCALE), camera3d);
            }
            DrawUIText(TextFormat("Peri: %.0f km", rp-EARTH_RADIUS_KM), periScreen.x, periScreen.y-(15*cfg.ui_scale), 16*cfg.ui_scale, cfg.periapsis);
            DrawUIText(TextFormat("Apo: %.0f km", ra-EARTH_RADIUS_KM), apoScreen.x, apoScreen.y-(15*cfg.ui_scale), 16*cfg.ui_scale, cfg.apoapsis);
        }

        DrawUIText(TextFormat("%3i FPS", GetFPS()), GetScreenWidth() - (90*cfg.ui_scale), 10*cfg.ui_scale, 20*cfg.ui_scale, cfg.sat_selected);
        EndDrawing();
    }

    // cleanup time before closing
    UnloadTexture(satIcon);
    UnloadTexture(markerIcon);
    UnloadTexture(earthTexture);
    UnloadModel(earthModel);
    UnloadTexture(moonTexture);
    UnloadModel(moonModel);
    UnloadFont(customFont);
    CloseWindow();
    return 0;
}