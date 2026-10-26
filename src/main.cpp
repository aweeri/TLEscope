#include <algorithm>
#include <vector>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

// edited with nano

#include <external/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/astro.h"
#include "core/propagator.h"
#include "core/config.h"
#include "core/theme.h"
#include "core/location.h"
#include "util/log.h"
#ifdef __APPLE__
#include "util/window_macos.h"
#endif
#include "core/types.h"
#include "ui/ui.h"
#include "ui/ui_layout.h"
#include "ui/notifications.h"
#include "ui/touch_gesture.h"
#include "ui/tutorial.h"
#include "ui/tools/tools.h"
#include "ui/tools/tools_common.h"
#include "ui/tools/tools_scene.h"
#include "ui/tools/tools_settings.h"
#include "ui/imgui_theme.h"
#include "io/rotator.h"
#include "data/async_fetch.h"
#include "data/storage.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"
#include "render/shaders.h"
#include "render/coverage_shaders.h"
#include "render/coverage_mesh.h"
#include "render/map_view.h"
#include "expo/expo_mode.h"

/* application state and resources */
static AppConfig cfg = []() -> AppConfig {
    AppConfig c = {};
    strcpy(c.theme, "default");
    c.window_width = 1280;
    c.window_height = 720;
    c.window_x = 0;
    c.window_y = 0;
    c.window_maximized = true;      /* open maximized by default on first run */
    c.has_window_placement = false; /* no saved restored rect yet */
    c.target_fps = 60;
    c.ui_scale = 1.0f;
    c.orbit_cache_drift_threshold_km = 50.0f;
    c.show_clouds = false;
    c.show_night_lights = true;
    c.show_markers = true;
    c.show_statistics = false;
    c.highlight_sunlit = false;
    c.show_slant_range = false;
    c.show_scattering = false;
    c.hint_vsync = false;
    c.night_mode = false;
    return c;
}();

static Font customFont;
static Texture2D satIcon, markerIcon, earthTexture, moonTexture, cloudTexture, earthNightTexture, skyboxTexture;
static Texture2D periMark, apoMark;
static Model earthModel, moonModel, cloudModel, atmosphereModel, skyboxModel;

/* Ground coverage shader resources */
static struct {
    Shader shader3D;
    Shader shader2D;
    
    /* Uniform locations for 3D shader */
    int satPosLoc;
    int colorLoc;
    int borderColorLoc;
    int depthBiasLoc;
    int edgeFalloffLoc;
    int cameraPosLoc;
    
    /* Uniform locations for 2D shader */
    int colorLoc2D;
    int borderColorLoc2D;
    int edgeFalloffLoc2D;
} g_coverage_shaders;

// blend fraction for the coverage highlight; alpha is preserved so the fill stays see-through
#define COVERAGE_SELECT_TINT 0.50f
#define COVERAGE_HOVER_TINT  0.50f

// blend tint into base, preserving base's alpha
static Color CoverageTintFill(Color base, Color tint, float amount)
{
    Color out;
    out.r = (unsigned char)(base.r + (tint.r - base.r) * amount);
    out.g = (unsigned char)(base.g + (tint.g - base.g) * amount);
    out.b = (unsigned char)(base.b + (tint.b - base.b) * amount);
    out.a = base.a;
    return out;
}

// selected-coverage fill (alpha preserved)
static Color CoverageSelectFill(Color base)
{
    return CoverageTintFill(base, g_theme.world.sat_selected, COVERAGE_SELECT_TINT);
}

// hovered-coverage fill (alpha preserved)
static Color CoverageHoverFill(Color base)
{
    return CoverageTintFill(base, g_theme.world.sat_hover, COVERAGE_HOVER_TINT);
}

/** manual mesh generation for the planetary spheres */
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

// future ground-track sample cache: 2D tracks re-propagate ~6000 SGP4 calls/frame, so cache on a miss
struct FutureTrackCacheEntry {
    Satellite *sat = NULL;
    double sat_epoch = 0.0;   /* invalidates the track on a TLE reload */
    int segments = 0;
    float span = 0.0f;
    float map_w = 0.0f;
    float map_h = 0.0f;
    float earth_offset = 0.0f;
    bool want_sunlit = false;
    double epoch_bucket = 0.0;
    std::vector<Vector2> pts;
    std::vector<double> times;
    std::vector<bool> sunlit;
    bool valid = false;
};

#define FUTURE_TRACK_CACHE_MAX 64
static std::vector<FutureTrackCacheEntry> g_future_track_cache;

/* coarse 10-minute sim-time bucket: refresh without per-frame propagation */
static double FutureTrackEpochBucket(double epoch)
{
    return floor(epoch * 144.0);
}

/** return cached ground-track samples for one satellite, rebuilding on a miss */
static const FutureTrackCacheEntry *GetFutureTrackSamples(
    Satellite *sat, int segments, float span, double epoch,
    float earth_offset, float map_w, float map_h, bool want_sunlit, Vector3 sun_dir)
{
    const double bucket = FutureTrackEpochBucket(epoch);

    for (const FutureTrackCacheEntry &e : g_future_track_cache)
    {
        if (e.valid && e.sat == sat && e.sat_epoch == sat->epoch_unix &&
            e.segments == segments && e.span == span &&
            e.map_w == map_w && e.map_h == map_h && e.earth_offset == earth_offset &&
            e.want_sunlit == want_sunlit && e.epoch_bucket == bucket)
            return &e;
    }

    if ((int)g_future_track_cache.size() >= FUTURE_TRACK_CACHE_MAX)
        g_future_track_cache.clear();

    /* reuse a slot for this sat/resolution to keep vector capacity */
    FutureTrackCacheEntry *slot = NULL;
    for (FutureTrackCacheEntry &e : g_future_track_cache)
    {
        if (e.sat == sat && e.segments == segments && e.span == span)
        {
            slot = &e;
            break;
        }
    }
    if (!slot)
    {
        g_future_track_cache.push_back(FutureTrackCacheEntry{});
        slot = &g_future_track_cache.back();
    }

    slot->sat = sat;
    slot->sat_epoch = sat->epoch_unix;
    slot->segments = segments;
    slot->span = span;
    slot->map_w = map_w;
    slot->map_h = map_h;
    slot->earth_offset = earth_offset;
    slot->want_sunlit = want_sunlit;
    slot->epoch_bucket = bucket;
    slot->pts.resize((size_t)segments + 1);
    slot->times.resize((size_t)segments + 1);
    if (want_sunlit)
        slot->sunlit.resize((size_t)segments + 1);

    orbit_adaptive_sample_times(sat, epoch, segments + 1, span, slot->times.data());
    for (int j = 0; j <= segments; j++)
    {
        double t = slot->times[j];
        double t_unix = get_unix_from_epoch(t);
        Vector3 raw_pos = calculate_position(sat, t_unix);
        get_map_coordinates(raw_pos, epoch_to_gmst(t), earth_offset, map_w, map_h,
                            &slot->pts[j].x, &slot->pts[j].y);
        if (want_sunlit)
            slot->sunlit[j] = !is_sat_eclipsed(raw_pos, sun_dir);
    }
    slot->valid = true;
    return slot;
}

/** Pick a theme-defined color for an additional 2D future ground track. */
static Color MultiGroundTrackColor(int index)
{
    return g_theme.ground_tracks.palette[index % GROUND_TRACK_PALETTE_SIZE];
}

/** apply the direction-of-motion alpha ramp; phase is the signed fraction of an orbit (positive = ahead) */
static Color ApplyOrbitDirection(Color c, double phase, float floor = ORBIT_DIR_MIN)
{
    if (!ToolSettingGetBool(&cfg, LAYERS_KEY_ORBIT_DIRECTION, true))
        return c;
    const bool anim = ToolSettingGetBool(&cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, false);
    const float k = OrbitDirectionIntensity(phase, anim, GetTime(), floor);
    c.a = (unsigned char)(c.a * k);
    return c;
}

/** line-width multiplier for the direction ramp (1.0 when the layer is off) */
static float OrbitDirectionWidthScale(double phase, float floor = ORBIT_DIR_MIN)
{
    if (!ToolSettingGetBool(&cfg, LAYERS_KEY_ORBIT_DIRECTION, true))
        return 1.0f;
    const bool anim = ToolSettingGetBool(&cfg, LAYERS_KEY_ORBIT_DIRECTION_ANIM, false);
    return OrbitDirectionWidth(phase, anim, GetTime(), floor);
}

/** render orbit lines in 3d space */
static void draw_orbit_3d(Satellite *sat, double current_epoch, bool is_highlighted, float alpha, int step, bool apply_sunlit, Vector3 cam_pos)
{
    Color orbitColor = ApplyAlpha(is_highlighted ? g_theme.world.orbit_active : g_theme.world.orbit, alpha);
    const float earth_r = EARTH_RADIUS_KM / DRAW_SCALE;

    if (is_highlighted)
    {
        Vector3 prev_pos = {0};
        double orbits_count = 1.0;
        int segments = fmin(4000, fmax(90, (int)(400 * orbits_count)));
        double period_days = (2.0 * PI / sat->mean_motion) / 86400.0;
        double time_step = (period_days * orbits_count) / segments;

        Vector3 base_sun_dir = {0};
        if (cfg.highlight_sunlit && apply_sunlit)
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
                if (cfg.highlight_sunlit && apply_sunlit)
                {
                    if (!is_sat_eclipsed(raw_pos, base_sun_dir))
                        drawCol = ApplyAlpha(g_theme.world.sat_hover, alpha);
                    else
                        drawCol = ApplyAlpha(g_theme.world.orbit, alpha);
                }
                drawCol = ApplyOrbitDirection(drawCol, (double)i / (double)segments,
                                              ORBIT_DIR_MIN_SELECTED);
                if (!IsOrbitSegmentOccluded(cam_pos, prev_pos, pos, earth_r))
                    DrawLine3D(prev_pos, pos, drawCol);
            }
            prev_pos = pos;
        }
    }
    else
    {
        if (!sat->orbit_cached)
            return;

        Vector3 base_sun_dir = {0};
        if (cfg.highlight_sunlit && apply_sunlit)
        {
            base_sun_dir = Vector3Normalize(calculate_sun_position(current_epoch));
        }

        Vector3 prev_pos = sat->orbit_cache[0];
        int cache_size = sat->orbit_cache_resolution;

        /* direction trail is a selected-orbit indicator only, so dimmed orbits stay flat */
        for (int i = step; i < cache_size; i += step)
        {
            Vector3 pos = sat->orbit_cache[i];
            Color drawCol = orbitColor;
            if (cfg.highlight_sunlit && apply_sunlit && !is_sat_eclipsed(Vector3Scale(pos, DRAW_SCALE), base_sun_dir))
                drawCol = ApplyAlpha(g_theme.world.sat_hover, alpha);
            if (!IsOrbitSegmentOccluded(cam_pos, prev_pos, pos, earth_r))
                DrawLine3D(prev_pos, pos, drawCol);
            prev_pos = pos;
        }

        /* draw final segment if needed */
        if ((cache_size - 1) % step != 0)
        {
            Color drawCol = orbitColor;
            int last = cache_size - 1;
            if (cfg.highlight_sunlit && apply_sunlit && !is_sat_eclipsed(Vector3Scale(sat->orbit_cache[last], DRAW_SCALE), base_sun_dir))
                drawCol = ApplyAlpha(g_theme.world.sat_hover, alpha);
            if (!IsOrbitSegmentOccluded(cam_pos, prev_pos, sat->orbit_cache[last], earth_r))
                DrawLine3D(prev_pos, sat->orbit_cache[last], drawCol);
        }
    }
}

/** draw a favorite satellite's orbit in 3d using a palette color (lit-up look). */
static void draw_orbit_3d_colored(Satellite *sat, double current_epoch, Color color, float alpha, int step, bool apply_sunlit, Vector3 cam_pos)
{
    Color orbitColor = ApplyAlpha(color, alpha);
    if (!sat->orbit_cached)
        return;

    const float earth_r = EARTH_RADIUS_KM / DRAW_SCALE;

    Vector3 base_sun_dir = {0};
    if (cfg.highlight_sunlit && apply_sunlit)
    {
        base_sun_dir = Vector3Normalize(calculate_sun_position(current_epoch));
    }

    rlSetLineWidth(3.0f);
    Vector3 prev_pos = sat->orbit_cache[0];
    int cache_size = sat->orbit_cache_resolution;

    for (int i = step; i < cache_size; i += step)
    {
        Vector3 pos = sat->orbit_cache[i];
        Color drawCol = orbitColor;
        if (cfg.highlight_sunlit && apply_sunlit && !is_sat_eclipsed(Vector3Scale(pos, DRAW_SCALE), base_sun_dir))
            drawCol = ApplyAlpha(g_theme.world.sat_hover, alpha);
        drawCol = ApplyOrbitDirection(drawCol, sat->orbit_cache_phase[i]);
        if (!IsOrbitSegmentOccluded(cam_pos, prev_pos, pos, earth_r))
            DrawLine3D(prev_pos, pos, drawCol);
        prev_pos = pos;
    }

    /* draw final segment if needed */
    if ((cache_size - 1) % step != 0)
    {
        Color drawCol = orbitColor;
        int last = cache_size - 1;
        if (cfg.highlight_sunlit && apply_sunlit && !is_sat_eclipsed(Vector3Scale(sat->orbit_cache[last], DRAW_SCALE), base_sun_dir))
            drawCol = ApplyAlpha(g_theme.world.sat_hover, alpha);
        drawCol = ApplyOrbitDirection(drawCol, sat->orbit_cache_phase[last]);
        if (!IsOrbitSegmentOccluded(cam_pos, prev_pos, sat->orbit_cache[last], earth_r))
            DrawLine3D(prev_pos, sat->orbit_cache[last], drawCol);
    }
    rlSetLineWidth(1.0f);
}

/** simple progress bar during init */
static void DrawLoadingScreen(float progress, const char *message, Texture2D logoTex)
{
    BeginDrawing();
    ClearBackground(g_theme.world.bg);

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float barW = 400 * cfg.ui_scale;
    float barH = 16 * cfg.ui_scale;

    float startY = (screenH / 2.0f) + (55.0f * cfg.ui_scale);

    if (logoTex.id != 0)
    {
        float logoScale = (120.0f * cfg.ui_scale) / logoTex.height;
        Vector2 logoPos = {(screenW - logoTex.width * logoScale) / 2.0f, startY - (logoTex.height * logoScale) - 40 * cfg.ui_scale};
        DrawTextureEx(logoTex, logoPos, 0.0f, logoScale, WHITE);
    }

    Rectangle barOutline = {(screenW - barW) / 2, startY, barW, barH};
    Rectangle barProgress = {barOutline.x + 3, barOutline.y + 3, (barW - 6) * progress, barH - 6};

    DrawRectangleRoundedLinesEx(barOutline, 0.5f, 16, 2.0f, g_theme.ui.text);
    if (progress > 0.0f)
        DrawRectangleRounded(barProgress, 0.5f, 16, g_theme.ui.text_dim);

    Vector2 msgSize = MeasureTextEx(customFont, message, 18 * cfg.ui_scale, 1.0f);
    DrawUIText(customFont, message, (screenW - msgSize.x) / 2, barOutline.y + barH + 20 * cfg.ui_scale, 18 * cfg.ui_scale, g_theme.ui.text);

    EndDrawing();
}

static void UpdateAutoWarpState(bool *is_auto_warping, double *auto_warp_target, double *auto_warp_initial_diff, double *current_epoch, double *time_multiplier, double *saved_multiplier)
{
    if (!*is_auto_warping)
        return;

    double diff_sec = (*auto_warp_target - *current_epoch) * 86400.0;
    double warp_dir = (*auto_warp_initial_diff >= 0.0) ? 1.0 : -1.0;

    if (diff_sec * warp_dir <= 0.0)
    {
        *current_epoch = *auto_warp_target;
        *time_multiplier = 1.0;
        *saved_multiplier = 1.0;
        *is_auto_warping = false;
    }
    else
    {
        double base_speed = fabs(*auto_warp_initial_diff) / 2.0;
        double eased_speed = fmin(base_speed, fabs(diff_sec) * 3.0);
        if (eased_speed < 1.0)
            eased_speed = 1.0;
        *time_multiplier = ClampTimeMultiplier(eased_speed * warp_dir);

        if (fabs(diff_sec) <= fabs(*time_multiplier) * GetFrameTime())
        {
            *current_epoch = *auto_warp_target;
            *time_multiplier = 1.0;
            *saved_multiplier = 1.0;
            *is_auto_warping = false;
        }
    }
}

static bool GetMouseEarthIntersection(Vector2 mouse, bool is_2d, Camera2D cam2d, Camera3D cam3d, double gmst_deg, float earth_offset, float map_w, float map_h, float *out_lat, float *out_lon)
{
    if (is_2d)
    {
        Vector2 world = GetScreenToWorld2D(mouse, cam2d);
        float mx = world.x;
        float my = world.y;
        // wrap x to [-map_w/2, map_w/2)
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
        Ray ray = ScreenToWorldRayViewport3D(mouse, cam3d);
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

static Satellite *FindSatByNorad(uint32_t norad)
{
    if (norad == 0)
        return NULL;
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].norad_id_num == norad && satellites[i].is_active)
            return &satellites[i];
    }
    return NULL;
}

static void StartTrackingSat(TargetLock *lock, Satellite **sat, uint32_t *norad, Satellite *target)
{
    *lock = LOCK_SAT;
    *sat = target;
    *norad = target->norad_id_num;
    NotifyPush(NOTIFY_INFO, ICON_FA_SATELLITE, "Currently Tracking: %s", target->name);
}

static void StartTrackingBody(TargetLock *lock, Satellite **sat, uint32_t *norad, TargetLock kind)
{
    *lock = kind;
    *sat = NULL;
    *norad = 0;
    if (kind == LOCK_MOON)
        NotifyPush(NOTIFY_INFO, ICON_FA_MOON, "Currently Tracking: Moon");
    else
        NotifyPush(NOTIFY_INFO, ICON_FA_GLOBE, "Currently Tracking: Earth");
}

/** user-initiated stop (pan / empty-space double-click); toasts only if tracking */
static void StopTrackingUser(TargetLock *lock, Satellite **sat, uint32_t *norad)
{
    if (*lock == LOCK_NONE)
        return;
    *lock = LOCK_NONE;
    *sat = NULL;
    *norad = 0;
    NotifyPush(NOTIFY_INFO, ICON_FA_CROSSHAIRS, "Tracking stopped");
}

/** target lost (decay / NaN / data reload); toasts the lost name if known */
static void StopTrackingLost(TargetLock *lock, Satellite **sat, uint32_t *norad, const char *name)
{
    if (*lock == LOCK_NONE)
        return;
    *lock = LOCK_NONE;
    *sat = NULL;
    *norad = 0;
    if (name && name[0])
        NotifyPush(NOTIFY_WARNING, ICON_FA_CROSSHAIRS, "Tracking stopped: %s lost", name);
    else
        NotifyPush(NOTIFY_INFO, ICON_FA_CROSSHAIRS, "Tracking stopped");
}

#define WINDOW_MIN_W 480
#define WINDOW_MIN_H 320

static int MonitorForPoint(int x, int y)
{
    int count = GetMonitorCount();
    for (int i = 0; i < count; i++)
    {
        Vector2 pos = GetMonitorPosition(i);
        int mw = GetMonitorWidth(i);
        int mh = GetMonitorHeight(i);
        if (x >= (int)pos.x && x < (int)pos.x + mw &&
            y >= (int)pos.y && y < (int)pos.y + mh)
            return i;
    }
    return GetCurrentMonitor();
}
static void ClampWindowRectToMonitor(int &x, int &y, int &w, int &h)
{
    int monitor = MonitorForPoint(x + w / 2, y + h / 2);
    Vector2 pos = GetMonitorPosition(monitor);
    int mw = GetMonitorWidth(monitor);
    int mh = GetMonitorHeight(monitor);

    if (w < WINDOW_MIN_W) w = WINDOW_MIN_W;
    if (h < WINDOW_MIN_H) h = WINDOW_MIN_H;
    if (w > mw) w = mw;
    if (h > mh) h = mh;

    int minX = (int)pos.x;
    int minY = (int)pos.y;
    int maxX = (int)pos.x + mw - w;
    int maxY = (int)pos.y + mh - h;

    if (x < minX || x > maxX) x = minX + (mw - w) / 2;
    if (y < minY || y > maxY) y = minY + (mh - h) / 2;
    if (x < minX) x = minX;
    if (y < minY) y = minY;
    if (x > maxX) x = maxX;
    if (y > maxY) y = maxY;
}

static Texture2D g_sunGlowTex = {0};

static Texture2D SunEnsureGlowTexture(void)
{
    if (g_sunGlowTex.id == 0)
    {
        const int SZ = 128;
        Image img = GenImageColor(SZ, SZ, BLANK);
        float c = (float)SZ * 0.5f;
        float maxR = c;

        for (int y = 0; y < SZ; y++)
        {
            for (int x = 0; x < SZ; x++)
            {
                float dx = (float)x - c;
                float dy = (float)y - c;
                float r = sqrtf(dx * dx + dy * dy) / maxR;
                if (r >= 1.0f)
                    continue;

                float halo = powf(1.0f - r, 2.4f);
                float core = powf(1.0f - r, 14.0f);

                unsigned char R = (unsigned char)(255.0f);
                unsigned char G = (unsigned char)((215.0f + (255.0f - 215.0f) * core) + 0.5f);
                unsigned char B = (unsigned char)((140.0f + (245.0f - 140.0f) * core) + 0.5f);
                unsigned char A = (unsigned char)(Clamp(halo, 0.0f, 1.0f) * 255.0f + 0.5f);
                ImageDrawPixel(&img, x, y, (Color){R, G, B, A});
            }
        }

        g_sunGlowTex = LoadTextureFromImage(img);
        SetTextureFilter(g_sunGlowTex, TEXTURE_FILTER_BILINEAR);
        UnloadImage(img);
    }
    return g_sunGlowTex;
}

static void DrawSunOverlay(Vector3 sunDirWorld, Vector3 camForward, Camera cam, const AppConfig &cfg)
{
    if (!cfg.show_sun)
        return;
    if (g_view3d_w <= 0.0f || g_view3d_h <= 0.0f)
        return;

    if (Vector3DotProduct(sunDirWorld, camForward) <= 0.0f)
        return;

    const float sunDist = 1200.0f;
    Vector3 sunWorld = Vector3Add(cam.position, Vector3Scale(sunDirWorld, sunDist));
    Vector2 sp = WorldToScreenViewport3D(sunWorld, cam);

    float margin_x = g_view3d_w * 0.6f;
    float margin_y = g_view3d_h * 0.6f;
    if (sp.x < g_view3d_x - margin_x || sp.x > g_view3d_x + g_view3d_w + margin_x ||
        sp.y < g_view3d_y - margin_y || sp.y > g_view3d_y + g_view3d_h + margin_y)
        return;

    const float rEarth = EARTH_RADIUS_KM / DRAW_SCALE;
    const float rAtmo  = (EARTH_RADIUS_KM + 80.0f) / DRAW_SCALE;
    float occlusion = 1.0f;
    {
        Vector3 o = cam.position;
        float od = Vector3DotProduct(o, sunDirWorld);
        float b2 = Vector3DotProduct(o, o) - od * od;
        float b  = sqrtf(fmaxf(b2, 0.0f));
        bool  in_front = (-od) > 0.0f;
        if (in_front)
        {
            if (b <= rEarth)
                occlusion = 0.0f;
            else if (b <= rAtmo)
                occlusion = (b - rEarth) / (rAtmo - rEarth);
        }
    }
    if (occlusion <= 0.0f)
        return;

    Vector2 center = { g_view3d_x + g_view3d_w * 0.5f, g_view3d_y + g_view3d_h * 0.5f };
    float short_side = fminf((float)GetScreenWidth(), (float)GetScreenHeight());
    if (short_side <= 0.0f)
        short_side = fminf(g_view3d_w, g_view3d_h);

    float disk = short_side * 0.045f;
    if (disk < 8.0f)
        disk = 8.0f;
    float glowR = disk * 6.0f;

    Vector2 d = { (sp.x - center.x) / (g_view3d_w * 0.5f), (sp.y - center.y) / (g_view3d_h * 0.5f) };
    float centreDist = sqrtf(d.x * d.x + d.y * d.y);
    float centreness = Clamp(1.0f - centreDist, 0.0f, 1.0f);

    Texture2D glow = SunEnsureGlowTexture();
    Rectangle src = { 0.0f, 0.0f, (float)glow.width, (float)glow.height };

    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();

    BeginScissorMode((int)g_view3d_x, (int)g_view3d_y, (int)g_view3d_w, (int)g_view3d_h);

    BeginBlendMode(BLEND_ADDITIVE);

    DrawTexturePro(glow, src, (Rectangle){ sp.x, sp.y, glowR * 2.0f, glowR * 2.0f },
                   (Vector2){ glowR, glowR }, 0.0f,
                   (Color){ 255, 220, 150, (unsigned char)(200.0f * occlusion) });
    DrawTexturePro(glow, src, (Rectangle){ sp.x, sp.y, disk * 2.0f, disk * 2.0f },
                   (Vector2){ disk, disk }, 0.0f,
                   (Color){ 255, 255, 245, (unsigned char)(255.0f * occlusion) });

    if (cfg.show_sun_flare)
    {
        float flare = centreness * occlusion;
        if (flare > 0.001f)
        {
            const float ts[7] = { -0.5f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f };
            const float sizes[7] = { 0.18f, 0.10f, 0.14f, 0.08f, 0.12f, 0.07f, 0.16f };
            const Color tints[7] = {
                { 255, 200, 140, 255 }, { 255, 170, 120, 255 }, { 180, 210, 255, 255 },
                { 255, 160, 200, 255 }, { 200, 230, 255, 255 }, { 255, 180, 120, 255 },
                { 255, 200, 140, 255 }
            };

            for (int i = 0; i < 7; i++)
            {
                float t = ts[i];
                Vector2 gpos = { center.x + (sp.x - center.x) * t, center.y + (sp.y - center.y) * t };
                float gr = short_side * sizes[i] * (0.7f + 0.3f * centreness);
                if (gr < 2.0f)
                    continue;
                unsigned char a = (unsigned char)(tints[i].a * flare * 0.55f);
                DrawTexturePro(glow, src, (Rectangle){ gpos.x, gpos.y, gr * 2.0f, gr * 2.0f },
                               (Vector2){ gr, gr }, 0.0f, (Color){ tints[i].r, tints[i].g, tints[i].b, a });
            }

            float streak_len = glowR * 7.0f;
            float streak_h = disk * 0.6f;
            unsigned char sa = (unsigned char)(140.0f * flare);
            DrawTexturePro(glow, src, (Rectangle){ sp.x, sp.y, streak_len, streak_h * 2.0f },
                           (Vector2){ streak_len * 0.5f, streak_h }, 0.0f, (Color){ 180, 210, 255, sa });
        }
    }

    EndBlendMode();
    EndScissorMode();

    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlEnableDepthMask();
}

int main(void)
{
    LogInit();
    AsyncFetchInit();
    SimScanInit();

    LoadAppConfig("settings.json", &cfg);
    SetUseLocalTime(cfg.use_local_time);
    sat_prop_set_short_period(cfg.prop_use_short_period);
    RotatorLoadSettings(&cfg); /* restore persisted rotator config */
    NotifyLoadSettings(&cfg);  /* restore persisted notification toggles */

    /* window setup and msaa */
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);

#ifndef TLESCOPE_VERSION
#define TLESCOPE_VERSION "vUnknown"
#endif

    /* Show the full version string (e.g. "v3.9.2-12-g4f2a1c9-dirty") */
    LOG_INFO("TLEscope %s starting", TLESCOPE_VERSION);

    char window_title[128];
    snprintf(window_title, sizeof(window_title), "TLEscope %s", TLESCOPE_VERSION);
    InitWindow(cfg.window_width, cfg.window_height, window_title);
    LOG_INFO("Window created: %dx%d, theme=%s", cfg.window_width, cfg.window_height, cfg.theme);

#ifdef __APPLE__
    ConfigureMacWindowFrameAutosave(GetWindowHandle());
#endif

    /* install the Windows touch observer (no-op elsewhere) and restore fullscreen */
    TouchGestureInit();
    if (cfg.fullscreen) ToggleFullscreen();

    // restore the persisted window placement before maximizing so Restore Down returns on-screen
    if (!cfg.fullscreen)
    {
        if (!cfg.has_window_placement)
        {
            // first run: pick a centered restored rect on the current monitor
            int monitor = GetCurrentMonitor();
            Vector2 pos = GetMonitorPosition(monitor);
            int mw = GetMonitorWidth(monitor);
            int mh = GetMonitorHeight(monitor);

            if (cfg.window_width <= 0) cfg.window_width = 1280;
            if (cfg.window_height <= 0) cfg.window_height = 720;
            if (cfg.window_width > mw - 100) cfg.window_width = mw - 100;
            if (cfg.window_height > mh - 100) cfg.window_height = mh - 100;
            cfg.window_x = (int)pos.x + (mw - cfg.window_width) / 2;
            cfg.window_y = (int)pos.y + (mh - cfg.window_height) / 2;
        }

        ClampWindowRectToMonitor(cfg.window_x, cfg.window_y, cfg.window_width, cfg.window_height);
        SetWindowSize(cfg.window_width, cfg.window_height);
        SetWindowPosition(cfg.window_x, cfg.window_y);

        if (cfg.window_maximized)
            SetWindowState(FLAG_WINDOW_MAXIMIZED);

        cfg.has_window_placement = true;
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
    customFont = LoadFontEx(ThemeAssetPath(g_theme.font.file), (int)g_theme.font.raylib_size, glyphs, glyphsCount);
    GenTextureMipmaps(&customFont.texture);
    SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
    UnloadCodepoints(glyphs);

    /* resource loading phase */
    DrawLoadingScreen(0.1f, "Loading Orbital Data...", logoLTex);
    LOG_INFO("Loading orbital data...");
    load_orbital_data("data.json");
    LOG_INFO("Loaded %d satellites from storage", sat_count);
    load_manual_entries(&cfg);
    LOG_INFO("Loaded %d manual entries", cfg.manual_entry_count);
    LoadSatSelection(&cfg); // restore active satellites
    LOG_INFO("Satellite selection restored");
    LoadFavorites("favorites.json"); // restore favorite satellites
    LOG_INFO("Favorites loaded");
    LoadDataSelections(); // restore data-source shopping-cart selections
    LOG_INFO("Data source selections restored");

    DrawLoadingScreen(0.25f, "Initializing Textures...", logoTex);
    LOG_INFO("Loading textures...");
    earthTexture = LoadTexture(ThemeAssetPath(g_theme.textures.earth));
    earthNightTexture = LoadTexture(ThemeAssetPath(g_theme.textures.earth_night));
    skyboxTexture = LoadTexture(ThemeAssetPath(g_theme.textures.skybox));

    /* make textures not blocky when zoomed in on */
    GenTextureMipmaps(&earthTexture);
    SetTextureFilter(earthTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
    GenTextureMipmaps(&earthNightTexture);
    SetTextureFilter(earthNightTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureFilter(skyboxTexture, TEXTURE_FILTER_BILINEAR);

    DrawLoadingScreen(0.4f, "Compiling Shaders...", logoTex);
    LOG_INFO("Compiling shaders...");
    Shader shader3D = LoadShaderFromMemory(NULL, Shaders::fs3D);
    int sunDirLoc3D = GetShaderLocation(shader3D, "sunDir");
    shader3D.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(shader3D, "texture1");
    shader3D.locs[SHADER_LOC_MAP_SPECULAR] = GetShaderLocation(shader3D, "texture2");
    int viewPosLoc3D = GetShaderLocation(shader3D, "viewPos");
    int cloudUVOffsetLoc3D = GetShaderLocation(shader3D, "cloudUVOffset");
    int advScatLoc3D = GetShaderLocation(shader3D, "advancedScatter");
    int showCloudsLoc3D = GetShaderLocation(shader3D, "showClouds");

    /* shaded solid-body shader for the textureless Earth */
    Shader earthSolidShader = LoadShaderFromMemory(NULL, Shaders::fsEarthSolid3D);
    int earthSolidSunLoc = GetShaderLocation(earthSolidShader, "sunDir");
    int earthSolidBaseLoc = GetShaderLocation(earthSolidShader, "baseColor");

    Shader shader2D = LoadShaderFromMemory(NULL, Shaders::fs2D);
    int sunDirLoc2D = GetShaderLocation(shader2D, "sunDir");
    int nightTexLoc2D = GetShaderLocation(shader2D, "texture1");

    // night mode: single screen-space red pass over a GPU copy of the MSAA backbuffer
    Shader shaderNight = LoadShaderFromMemory(NULL, Shaders::fsNight);
    int nightIntensityLoc = GetShaderLocation(shaderNight, "intensity");
    Texture2D nightTex = {0};

    Shader shaderCloud = LoadShaderFromMemory(NULL, Shaders::fsCloud3D);
    int sunDirLocCloud = GetShaderLocation(shaderCloud, "sunDir");

    Shader shaderMoon = LoadShaderFromMemory(NULL, Shaders::fsMoon3D);
    int sunDirLocMoon = GetShaderLocation(shaderMoon, "sunDir");
    int moonPosLocMoon = GetShaderLocation(shaderMoon, "moonPos");
    int moonRotLocMoon = GetShaderLocation(shaderMoon, "moonRot");
    int moonRadiusLocMoon = GetShaderLocation(shaderMoon, "moonRadius");
    int earthRadiusSqLocMoon = GetShaderLocation(shaderMoon, "earthRadiusSq");

    Shader shaderAtmosphere = LoadShaderFromMemory(NULL, Shaders::fsAtmosphere3D);
    int sunDirLocAtmosphere = GetShaderLocation(shaderAtmosphere, "sunDir");
    int viewPosLocAtmosphere = GetShaderLocation(shaderAtmosphere, "viewPos");

    DrawLoadingScreen(0.6f, "Generating Meshes...", logoTex);
    LOG_INFO("Generating 3D meshes...");
    float draw_earth_radius = EARTH_RADIUS_KM / DRAW_SCALE;
    Mesh sphereMesh = GenEarthMesh(draw_earth_radius, 80, 80);
    earthModel = LoadModelFromMesh(sphereMesh);
    earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
    earthModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = earthNightTexture;
    Shader defaultEarthShader = earthModel.materials[0].shader;

    DrawLoadingScreen(0.8f, "Loading Celestial Bodies...", logoTex);
    LOG_INFO("Loading celestial bodies (Earth, Moon, skybox)...");

    Mesh skyboxMesh = GenEarthMesh(-500.0f, 40, 40); /* negative radius flips normals inward */
    skyboxModel = LoadModelFromMesh(skyboxMesh);
    skyboxModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skyboxTexture;

    float draw_cloud_radius = (EARTH_RADIUS_KM + 25.0f) / DRAW_SCALE;
    Mesh cloudMesh = GenEarthMesh(draw_cloud_radius, 80, 80);
    cloudModel = LoadModelFromMesh(cloudMesh);
    cloudTexture = LoadTexture(ThemeAssetPath(g_theme.textures.clouds));
    SetTextureFilter(cloudTexture, TEXTURE_FILTER_BILINEAR);
    cloudModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudTexture;
    earthModel.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = cloudTexture;
    Shader defaultCloudShader = cloudModel.materials[0].shader;

    float draw_atmosphere_radius = (EARTH_RADIUS_KM + 80.0f) / DRAW_SCALE;
    Mesh atmosphereMesh = GenEarthMesh(draw_atmosphere_radius, 80, 80);
    atmosphereModel = LoadModelFromMesh(atmosphereMesh);
    atmosphereModel.materials[0].shader = shaderAtmosphere;
    SetShaderValue(shaderAtmosphere, GetShaderLocation(shaderAtmosphere, "atmRadius"), &draw_atmosphere_radius, SHADER_UNIFORM_FLOAT);

    float draw_moon_radius = MOON_RADIUS_KM / DRAW_SCALE;
    Mesh moonMesh = GenEarthMesh(draw_moon_radius, 48, 48);
    moonModel = LoadModelFromMesh(moonMesh);
    moonTexture = LoadTexture(ThemeAssetPath(g_theme.textures.moon));
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

    /* Ground coverage shaders */
    LOG_INFO("Compiling ground coverage shaders...");
    g_coverage_shaders.shader3D = LoadShaderFromMemory(CoverageShaders::vsCoverage3D, CoverageShaders::fsCoverage3D);
    g_coverage_shaders.shader2D = LoadShaderFromMemory(NULL, CoverageShaders::fsCoverage2D);
    
    g_coverage_shaders.satPosLoc = GetShaderLocation(g_coverage_shaders.shader3D, "satPosition");
    g_coverage_shaders.colorLoc = GetShaderLocation(g_coverage_shaders.shader3D, "coverageColor");
    g_coverage_shaders.borderColorLoc = GetShaderLocation(g_coverage_shaders.shader3D, "borderColor");
    g_coverage_shaders.depthBiasLoc = GetShaderLocation(g_coverage_shaders.shader3D, "depthBias");
    g_coverage_shaders.edgeFalloffLoc = GetShaderLocation(g_coverage_shaders.shader3D, "edgeFalloff");
    g_coverage_shaders.cameraPosLoc = GetShaderLocation(g_coverage_shaders.shader3D, "cameraPos");
    
    g_coverage_shaders.colorLoc2D = GetShaderLocation(g_coverage_shaders.shader2D, "coverageColor");
    g_coverage_shaders.borderColorLoc2D = GetShaderLocation(g_coverage_shaders.shader2D, "borderColor");
    g_coverage_shaders.edgeFalloffLoc2D = GetShaderLocation(g_coverage_shaders.shader2D, "edgeFalloff");
    
    /* Set static uniforms */
    float depthBias = 0.00001f;
    /* Hard, crisp edge by default (0 = pure ~1 px fwidth()-based anti-aliasing). */
    float edgeFalloff = 0.0f;
    SetShaderValue(g_coverage_shaders.shader3D, g_coverage_shaders.depthBiasLoc, &depthBias, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_coverage_shaders.shader3D, g_coverage_shaders.edgeFalloffLoc, &edgeFalloff, SHADER_UNIFORM_FLOAT);
    SetShaderValue(g_coverage_shaders.shader2D, g_coverage_shaders.edgeFalloffLoc2D, &edgeFalloff, SHADER_UNIFORM_FLOAT);

    DrawLoadingScreen(0.95f, "Finalizing UI...", logoTex);
    LOG_INFO("Finalizing UI textures...");
    satIcon = LoadTexture(ThemeAssetPath(g_theme.textures.sat_icon));
    markerIcon = LoadTexture(ThemeAssetPath(g_theme.textures.marker_icon));
    periMark = LoadTexture(ThemeAssetPath(g_theme.textures.smallmark));
    apoMark = LoadTexture(ThemeAssetPath(g_theme.textures.smallmark));

    SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(periMark, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(apoMark, TEXTURE_FILTER_BILINEAR);

    DrawLoadingScreen(1.0f, "Ready!", logoTex);
    LOG_INFO("Initialization complete - entering main loop");

    /* camera defaults */
    Camera Camera3DParams = {0};
    Camera3DParams.target = (Vector3){0.0f, 0.0f, 0.0f};
    Camera3DParams.up = (Vector3){0.0f, 1.0f, 0.0f};
    Camera3DParams.fovy = 45.0f;
    Camera3DParams.projection = CAMERA_PERSPECTIVE;

    float map_w = 2048.0f, map_h = 1024.0f;

    Camera2D Camera2DParams = {0};
    Camera2DParams.zoom = MapFillZoom(map_w, map_h);
    Camera2DParams.offset = (Vector2){floorf(GetScreenWidth() / 2.0f), floorf(GetScreenHeight() / 2.0f)};
    Camera2DParams.target = (Vector2){0.0f, 0.0f};

    float target_camera2d_zoom = Camera2DParams.zoom;
    Vector2 target_camera2d_target = Camera2DParams.target;
    float fill_zoom = Camera2DParams.zoom;
    float camDistance = 10.0f, camAngleX = 0.785f, camAngleY = 0.5f;

    float target_camDistance = camDistance;
    float target_camAngleX = camAngleX;
    float target_camAngleY = camAngleY;
    Vector3 target_camera3d_target = Camera3DParams.target;

    /* accumulated Earth->body radial azimuth for co-rotating the orbit offset */
    float track_follow_angle = 0.0f;
    bool track_follow_valid = false;
    float track_follow_last_phi = 0.0f;
    TargetLock track_follow_lock = LOCK_NONE;
    Satellite *track_follow_sat = NULL;

    double current_epoch = get_current_real_time_epoch();
    bool live_follow = true;   /* sim clock slaved to wall clock while live */
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
    bool is_ecliptic_frame = cfg.show_ecliptic_frame;
    float current_ecliptic_angle = 0.0f;
    bool is_pov_mode = false;

    Satellite *hovered_sat = NULL;
    Satellite *selected_sat = NULL;
    TargetLock active_lock = LOCK_EARTH;
    Satellite *tracked_sat = NULL;
    uint32_t tracked_norad = 0;
    double last_left_click_time = 0.0;
    Vector2 left_press_pos = {0};
    bool left_press_over_ui = false;
    bool left_drag_active = false;

    /* apply vsync / fps limit at startup so the window state matches the config */
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
    
    int current_update_idx = 0;

    /* main loop */
    while (!WindowShouldClose() && !exit_app)
    {
        if (!IsWindowFullscreen() && !IsWindowMaximized() && !IsWindowMinimized())
        {
            Vector2 wp = GetWindowPosition();
            cfg.window_x = (int)wp.x;
            cfg.window_y = (int)wp.y;
            cfg.window_width = GetScreenWidth();
            cfg.window_height = GetScreenHeight();
            cfg.has_window_placement = true;
        }
        cfg.window_maximized = IsWindowMaximized();

        if (cfg.reload_theme)
        {
            cfg.reload_theme = false;
            LOG_INFO("Reloading theme: %s", cfg.theme);

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

            // reload theme data + ImGui presentation; do NOT call LoadAppConfig (it would overwrite cfg.theme)
            ThemeInitDefaults(&g_theme);
            ThemeLoad(cfg.theme, &g_theme);
            ThemeRebuildImGuiFonts(&g_theme, cfg.ui_scale);
            ThemeApplyToImGui(&g_theme, cfg.ui_scale);

            /* reload raylib font + textures from the new theme */
            int glyphsCount = 0;
            int *glyphs = LoadCodepoints(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", &glyphsCount);
            customFont = LoadFontEx(ThemeAssetPath(g_theme.font.file), (int)g_theme.font.raylib_size, glyphs, glyphsCount);
            GenTextureMipmaps(&customFont.texture);
            SetTextureFilter(customFont.texture, TEXTURE_FILTER_BILINEAR);
            UnloadCodepoints(glyphs);

            earthTexture = LoadTexture(ThemeAssetPath(g_theme.textures.earth));
            earthNightTexture = LoadTexture(ThemeAssetPath(g_theme.textures.earth_night));
            GenTextureMipmaps(&earthTexture);
            SetTextureFilter(earthTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
            GenTextureMipmaps(&earthNightTexture);
            SetTextureFilter(earthNightTexture, TEXTURE_FILTER_ANISOTROPIC_16X);
            earthModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = earthTexture;
            earthModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = earthNightTexture;

            cloudTexture = LoadTexture(ThemeAssetPath(g_theme.textures.clouds));
            SetTextureFilter(cloudTexture, TEXTURE_FILTER_BILINEAR);
            cloudModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = cloudTexture;
            earthModel.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = cloudTexture;

            skyboxTexture = LoadTexture(ThemeAssetPath(g_theme.textures.skybox));
            SetTextureFilter(skyboxTexture, TEXTURE_FILTER_BILINEAR);
            skyboxModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = skyboxTexture;

            moonTexture = LoadTexture(ThemeAssetPath(g_theme.textures.moon));
            SetTextureFilter(moonTexture, TEXTURE_FILTER_BILINEAR);
            moonModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = moonTexture;

            satIcon = LoadTexture(ThemeAssetPath(g_theme.textures.sat_icon));
            markerIcon = LoadTexture(ThemeAssetPath(g_theme.textures.marker_icon));
            periMark = LoadTexture(ThemeAssetPath(g_theme.textures.smallmark));
            apoMark = LoadTexture(ThemeAssetPath(g_theme.textures.smallmark));

            SetTextureFilter(satIcon, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(markerIcon, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(periMark, TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(apoMark, TEXTURE_FILTER_BILINEAR);
        }

        // 3-finger double-tap toggles clean view (same as H); consumed even while typing
        if (TouchGestureConsumeThreeFingerDoubleTap())
            LayoutToggleCleanView();

        bool is_typing = IsUITyping();
        bool over_ui = IsMouseOverUI(&cfg);
        const bool tutorial_up = TutorialActive(&cfg);

        float vp_x, vp_y, vp_w, vp_h;
        LayoutGetViewportRect(&vp_x, &vp_y, &vp_w, &vp_h);

        Viewport3DSetRect(vp_x, vp_y, vp_w, vp_h);

        // keep the 2D map pixel-centered and preserve relative zoom on window resize
        {
            Camera2DParams.offset = (Vector2){floorf(vp_x + vp_w * 0.5f), floorf(vp_y + vp_h * 0.5f)};
            const float new_fill = MapFillZoomFor(map_w, map_h, vp_w, vp_h);
            if (new_fill != fill_zoom)
            {
                target_camera2d_zoom *= new_fill / fill_zoom;
                Camera2DParams.zoom *= new_fill / fill_zoom;
                fill_zoom = new_fill;
            }
        }

        /* remember the scale so the +/- keybind can toast only on a real change */
        const float prev_ui_scale = cfg.ui_scale;

        /* input handling */
        if (!is_typing && !tutorial_up)
        {
            if (IsKeyPressed(KEY_SPACE))
            {
                is_auto_warping = false;
                if (time_multiplier != 0.0)
                {
                    saved_multiplier = time_multiplier;
                    time_multiplier = 0.0;
                    NotifyPush(NOTIFY_INFO, ICON_FA_PAUSE, "Time paused");
                }
                else
                {
                    time_multiplier = saved_multiplier != 0.0 ? saved_multiplier : 1.0;
                    NotifyPush(NOTIFY_INFO, ICON_FA_PLAY, "Time resumed");
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
                /* only toast on the initial press, not on hold-repeat */
                if (IsKeyPressed(KEY_PERIOD))
                    NotifyPush(NOTIFY_INFO, ICON_FA_FORWARD, "Time sped up");
            }
            if (step_bwd)
            {
                is_auto_warping = false;
                time_multiplier = StepTimeMultiplier(time_multiplier, false);
                if (IsKeyPressed(KEY_COMMA))
                    NotifyPush(NOTIFY_INFO, ICON_FA_BACKWARD, "Time slowed");
            }
            if (IsKeyPressed(KEY_M)) {
                is_2d_view = !is_2d_view;
                LOG_INFO("View switched to %s", is_2d_view ? "2D map" : "3D globe");
            }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
                ToggleTLEWarning();
                LOG_DEBUG("TLE warning toggled");
            }

            if (IsKeyPressed(KEY_C)) {
                cfg.show_clouds = !cfg.show_clouds;
                LOG_DEBUG("Clouds: %s", cfg.show_clouds ? "ON" : "OFF");
            }
            if (IsKeyPressed(KEY_N)) {
                cfg.show_night_lights = !cfg.show_night_lights;
                LOG_DEBUG("Night lights: %s", cfg.show_night_lights ? "ON" : "OFF");
            }
            if (IsKeyPressed(KEY_L)) {
                cfg.show_markers = !cfg.show_markers;
                LOG_DEBUG("Markers: %s", cfg.show_markers ? "ON" : "OFF");
            }
            if (IsKeyPressed(KEY_F10)) {
                cfg.night_mode = !cfg.night_mode;
                LOG_INFO("Night mode: %s", cfg.night_mode ? "ON" : "OFF");
                NotifyPush(NOTIFY_INFO, ICON_FA_MOON, cfg.night_mode ? "Night mode ON" : "Night mode OFF");
            }

            if (IsKeyPressed(KEY_HOME))
            {
                target_camDistance = 10.0f;
                target_camAngleX = 0.785f;
                target_camAngleY = 0.5f;
                Camera3DParams.fovy = 45.0f;

                if (is_2d_view)
                {
                    active_lock = LOCK_NONE;
                    tracked_sat = NULL;
                    tracked_norad = 0;

                    Location *home = GetHomeLocation();
                    if (home)
                    {
                        const float home_x = (home->lon / 360.0f) * map_w;
                        const float home_y = -(home->lat / 180.0f) * map_h;

                        /* Preserve zoom when centering on home. */
                        target_camera2d_target = (Vector2){home_x, home_y};
                    }
                    else
                    {
                        target_camera2d_zoom = fill_zoom;
                        target_camera2d_target = Vector2Zero();
                    }
                }
                else
                {
                    StartTrackingBody(&active_lock, &tracked_sat, &tracked_norad, LOCK_EARTH);
                }
            }

            if (IsKeyPressed(KEY_SLASH))
            {
                is_auto_warping = false;
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                {
                    current_epoch = get_current_real_time_epoch();
                    NotifyPush(NOTIFY_INFO, ICON_FA_CLOCK, "Time reset to now");
                }
                else
                {
                    time_multiplier = 1.0;
                    saved_multiplier = 1.0;
                    NotifyPush(NOTIFY_INFO, ICON_FA_CLOCK, "Time speed reset to 1x");
                }
            }

            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
                cfg.ui_scale += 0.1f;
                LOG_DEBUG("UI scale: %.2f", cfg.ui_scale);
            }
            if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
                cfg.ui_scale -= 0.1f;
                LOG_DEBUG("UI scale: %.2f", cfg.ui_scale);
            }
            if (IsKeyPressed(KEY_F11)) {
                ToggleFullscreen();
                cfg.fullscreen = IsWindowFullscreen();
                SaveAppConfig("settings.json", &cfg);
                LOG_INFO("Fullscreen toggled");
            }

            /* sidebar show/hide shortcuts */
            if (IsKeyPressed(KEY_ONE))
                LayoutSetSidebarVisible(SIDEBAR_LEFT, !LayoutSidebarVisible(SIDEBAR_LEFT));
            if (IsKeyPressed(KEY_TWO))
                LayoutSetSidebarVisible(SIDEBAR_RIGHT, !LayoutSidebarVisible(SIDEBAR_RIGHT));

        }

        if (cfg.ui_scale < 0.5f)
            cfg.ui_scale = 0.5f;
        if (cfg.ui_scale > 4.0f)
            cfg.ui_scale = 4.0f;

        if (cfg.ui_scale != prev_ui_scale)
            NotifyPush(NOTIFY_INFO, ICON_FA_MAGNIFYING_GLASS, "UI scale: %.0f%%", cfg.ui_scale * 100.0f);

        /* time warp logic for jumping to specific dates */
        UpdateAutoWarpState(&is_auto_warping, &auto_warp_target, &auto_warp_initial_diff, &current_epoch, &time_multiplier, &saved_multiplier);

        // live-mode time lock: at 1x with no auto-warp, slave the sim clock to the wall clock; else accumulate frame time
        double real_now = get_current_real_time_epoch_hr();
        bool running_live = (!is_auto_warping) && (time_multiplier == 1.0);

        if (running_live && live_follow)
            current_epoch = real_now;
        else
            current_epoch = normalize_epoch(current_epoch + (GetFrameTime() * time_multiplier) / 86400.0);

        live_follow = running_live && (fabs(current_epoch - real_now) * 86400.0 < LIVE_WINDOW_SEC);

        /* distance-based invalidation */
        if (sat_count > 0)
        {
            int updates_per_frame = 20;  // only caches that are invalid get updated
            for (int i = 0; i < updates_per_frame; i++)
            {
                if (satellites[current_update_idx].is_active)
                {
                    // only update if satellite drifted
                    if (!is_orbit_cache_valid(&satellites[current_update_idx], 
                                              satellites[current_update_idx].current_pos,
                                              cfg.orbit_cache_drift_threshold_km))
                    {
                        update_orbit_cache(&satellites[current_update_idx], current_epoch);
                    }
                }
                current_update_idx = (current_update_idx + 1) % sat_count;
            }
        }

        double current_unix = get_unix_from_epoch(current_epoch);

        /* update current positions of all active satellites */
        int active_render_count = 0;
        for (int i = 0; i < sat_count; i++)
        {
            if (!satellites[i].is_active)
                continue;
            if (hide_unselected && selected_sat != NULL && &satellites[i] != selected_sat)
                continue;
            satellites[i].current_pos = calculate_position(&satellites[i], current_unix);

            /* check for NaN/Inf positions (SGP4 propagation failure) */
            if (isnan(satellites[i].current_pos.x) || isnan(satellites[i].current_pos.y) || isnan(satellites[i].current_pos.z) ||
                isinf(satellites[i].current_pos.x) || isinf(satellites[i].current_pos.y) || isinf(satellites[i].current_pos.z))
            {
                LOG_WARN("Sat %s deactivated - NaN/Inf position (SGP4 error %d)", satellites[i].name, satellites[i].satrec.error);
                satellites[i].is_active = false;
                if (selected_sat == &satellites[i])
                    selected_sat = NULL;
                if (active_lock == LOCK_SAT && tracked_sat == &satellites[i])
                    StopTrackingLost(&active_lock, &tracked_sat, &tracked_norad, satellites[i].name);
                continue;
            }

            // below 80% of Earth's radius: about to hit the singularity, disable it
            if (Vector3Length(satellites[i].current_pos) < EARTH_RADIUS_KM * 0.8f)
            {
                LOG_WARN("Sat %s deactivated - orbital decay (pos < 0.8x Earth radius)", satellites[i].name);
                satellites[i].is_active = false;
                if (selected_sat == &satellites[i])
                    selected_sat = NULL;
                if (active_lock == LOCK_SAT && tracked_sat == &satellites[i])
                    StopTrackingLost(&active_lock, &tracked_sat, &tracked_norad, satellites[i].name);
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

        // left-drag is the only pan/orbit gesture on trackpads; past the threshold the click is swallowed on release
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            left_press_pos = GetMousePosition();
            left_press_over_ui = over_ui;
            left_drag_active = false;
        }
        if (!left_press_over_ui && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            Vector2Distance(GetMousePosition(), left_press_pos) > 4.0f * cfg.ui_scale)
            left_drag_active = true;
        const bool left_dragging = left_drag_active && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

        /* vsync config check */
        if (cfg.hint_vsync != IsWindowState(FLAG_VSYNC_HINT))
        {
            if (cfg.hint_vsync)
            {
                SetWindowState(FLAG_VSYNC_HINT);
                SetTargetFPS(0);
                LOG_INFO("VSync enabled");
            }
            else
            {
                ClearWindowState(FLAG_VSYNC_HINT);
                SetTargetFPS(cfg.target_fps);
                LOG_INFO("VSync disabled, target FPS: %d", cfg.target_fps);
            }
        }
        
        /* 2D picking/camera */
        if (is_2d_view)
        {
            if (!over_ui)
            {
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || left_dragging || (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && IsKeyDown(KEY_LEFT_SHIFT)))
                {
                    target_camera2d_target = Vector2Add(target_camera2d_target, Vector2Scale(mouseDelta, -1.0f / target_camera2d_zoom));
                    StopTrackingUser(&active_lock, &tracked_sat, &tracked_norad);
                }
                float wheel = GetMouseWheelMove();
                if (wheel != 0 && !is_typing)
                {
                    target_camera2d_zoom += wheel * 0.1f * target_camera2d_zoom;
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
                if (moved) StopTrackingUser(&active_lock, &tracked_sat, &tracked_norad);
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
                    mx += map_w * roundf((Camera2DParams.target.x - mx) / map_w);

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
                if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || left_dragging)
                {
                    if (IsKeyDown(KEY_LEFT_SHIFT))
                    {
                        Vector3 forward = Vector3Normalize(Vector3Subtract(Camera3DParams.target, Camera3DParams.position));
                        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, Camera3DParams.up));
                        Vector3 upVector = Vector3Normalize(Vector3CrossProduct(right, forward));
                        float panSpeed = target_camDistance * 0.001f;
                        target_camera3d_target = Vector3Add(target_camera3d_target, Vector3Scale(right, -mouseDelta.x * panSpeed));
                        target_camera3d_target = Vector3Add(target_camera3d_target, Vector3Scale(upVector, mouseDelta.y * panSpeed));
                        StopTrackingUser(&active_lock, &tracked_sat, &tracked_norad);
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
                }
            }

            if (!over_ui)
            {
                Ray mouseRay = ScreenToWorldRayViewport3D(GetMousePosition(), Camera3DParams);
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
                    /* cull satellites behind the globe so they cannot be hovered/clicked */
                    if (IsOccludedByEarth(Camera3DParams.position, draw_pos, draw_earth_radius))
                        continue;

                    Vector3 to_sat = Vector3Subtract(draw_pos, Camera3DParams.position);
                    float distToCamSqr = Vector3LengthSqr(to_sat);

                    if (distToCamSqr > 0.00001f)
                    {
                        float proj = Vector3DotProduct(to_sat, mouseRay.direction);
                        if (proj > 0.0f) // only check if in front of camera
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

        /* selection/double-click planet lock on release */
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !left_drag_active && !tutorial_up)
        {
            if (!over_ui)
            {
                if (picking_home)
                {
                    float lat, lon;
                    if (GetMouseEarthIntersection(GetMousePosition(), is_2d_view, Camera2DParams, Camera3DParams, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &lat, &lon))
                    {
                        // picking updates the targeted location, or home if none was targeted
                        if (pick_location_index >= 0 && pick_location_index < location_count)
                        {
                            UpdateLocation(pick_location_index, NULL, lat, lon, 0.0f);
                        }
                        else
                        {
                            int home_idx = GetHomeLocationIndex();
                            if (home_idx >= 0)
                            {
                                UpdateLocation(home_idx, NULL, lat, lon, 0.0f);
                            }
                            else
                            {
                                home_idx = AddLocation("Home", lat, lon, 0.0f);
                                if (home_idx >= 0)
                                    SetHomeLocation(home_idx);
                            }
                        }
                        pick_location_index = -1;
                        picking_home = false; // exit picking after successful set
                        LayoutOpenSettings(); // return to the settings modal
                    }
                    // if click is not on earth, do nothing
                }
                else
                {
                    // normal satellite selection
                    double current_time = GetTime();
                    bool is_double_click = (current_time - last_left_click_time < 0.3);

                    if (is_double_click)
                    {
                        /* double-click selects the satellite under the cursor, or clears on empty space */
                        selected_sat = hovered_sat;
                    }
                    else if (hovered_sat != NULL)
                    {
                        /* re-tap the already-selected satellite toggles it off */
                        selected_sat = (selected_sat == hovered_sat) ? NULL : hovered_sat;
                    }

                    if (is_double_click)
                    {
                        /* double-click a satellite to track it (independent of selection) */
                        if (hovered_sat != NULL)
                        {
                            if (!(active_lock == LOCK_SAT && tracked_sat == hovered_sat))
                                StartTrackingSat(&active_lock, &tracked_sat, &tracked_norad, hovered_sat);
                        }
                        else
                        {
                            TargetLock resolved = LOCK_NONE;
                            if (is_2d_view)
                            {
                                Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), Camera2DParams);
                                const float moon_x = moon_mx + map_w * roundf((mouseWorld.x - moon_mx) / map_w);
                                bool hit_moon = Vector2Distance(mouseWorld, (Vector2){moon_x, moon_my}) <
                                                (15.0f * cfg.ui_scale / Camera2DParams.zoom);
                                bool on_map = mouseWorld.y >= -map_h * 0.5f && mouseWorld.y <= map_h * 0.5f;
                                resolved = hit_moon ? LOCK_MOON : (on_map ? LOCK_EARTH : LOCK_NONE);
                            }
                            else
                            {
                                Ray mouseRay = ScreenToWorldRayViewport3D(GetMousePosition(), Camera3DParams);
                                RayCollision earthCol = GetRayCollisionSphere(mouseRay, Vector3Zero(), draw_earth_radius);
                                RayCollision moonCol = GetRayCollisionSphere(mouseRay, draw_moon_pos, draw_moon_radius);
                                if (moonCol.hit && (!earthCol.hit || moonCol.distance < earthCol.distance))
                                    resolved = LOCK_MOON;
                                else if (earthCol.hit)
                                    resolved = LOCK_EARTH;
                                else
                                    resolved = LOCK_NONE;
                            }

                            if (resolved != active_lock)
                            {
                                if (resolved == LOCK_MOON)
                                    StartTrackingBody(&active_lock, &tracked_sat, &tracked_norad, LOCK_MOON);
                                else if (resolved == LOCK_EARTH)
                                    StartTrackingBody(&active_lock, &tracked_sat, &tracked_norad, LOCK_EARTH);
                                else
                                    StopTrackingUser(&active_lock, &tracked_sat, &tracked_norad);
                            }
                        }
                    }
                    last_left_click_time = current_time;
                }
            }
        }

        /* camera interpolation targets */
        {
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
            else if (active_lock == LOCK_SAT)
            {
                char lost_name[32];
                lost_name[0] = '\0';
                if (tracked_sat != NULL)
                    snprintf(lost_name, sizeof(lost_name), "%s", tracked_sat->name);

                /* a data reload can recycle the array; re-find the sat by NORAD */
                if (tracked_sat == NULL || !tracked_sat->is_active)
                    tracked_sat = FindSatByNorad(tracked_norad);

                if (tracked_sat == NULL)
                {
                    StopTrackingLost(&active_lock, &tracked_sat, &tracked_norad, lost_name);
                }
                else if (is_2d_view)
                {
                    float sat_mx, sat_my;
                    get_map_coordinates(tracked_sat->current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sat_mx, &sat_my);
                    target_camera2d_target = (Vector2){sat_mx, sat_my};
                }
                else
                {
                    target_camera3d_target = Vector3Scale(tracked_sat->current_pos, 1.0f / DRAW_SCALE);
                }
            }

            // keep the horizontal target on the nearest map copy so panning crosses the antimeridian without a seam
            {
                const float shift = -map_w * floorf((target_camera2d_target.x + map_w * 0.5f) / map_w);
                target_camera2d_target.x += shift;
                Camera2DParams.target.x += shift;
            }

            // keep the map covering the viewport; clamp vertical panning to the poles
            if (cfg.limit_map_zoomout && target_camera2d_zoom < fill_zoom)
                target_camera2d_zoom = fill_zoom;
            auto clamp_map_y = [&](float zoom, float y) {
                const float lim = fmaxf(map_h * 0.5f - vp_h / (2.0f * zoom), 0.0f);
                return Clamp(y, -lim, lim);
            };
            target_camera2d_target.y = clamp_map_y(target_camera2d_zoom, target_camera2d_target.y);

            float smooth_speed = 10.0f * GetFrameTime();
            if (smooth_speed > 1.0f) smooth_speed = 1.0f; // clamp it so the camera doesnt spin out when alt tabbed

            Camera2DParams.zoom = Lerp(Camera2DParams.zoom, target_camera2d_zoom, smooth_speed);

            camAngleX = Lerp(camAngleX, target_camAngleX, smooth_speed);
            camAngleY = Lerp(camAngleY, target_camAngleY, smooth_speed);
            camDistance = Lerp(camDistance, target_camDistance, smooth_speed);

            /* follow tracked targets rigidly: lerp lag grows with the time multiplier */
            if (active_lock != LOCK_NONE)
            {
                Camera2DParams.target = target_camera2d_target;
                Camera3DParams.target = target_camera3d_target;
            }
            else
            {
                Camera2DParams.target = Vector2Lerp(Camera2DParams.target, target_camera2d_target, smooth_speed);
                Camera3DParams.target = Vector3Lerp(Camera3DParams.target, target_camera3d_target, smooth_speed);
            }
            Camera2DParams.target.y = clamp_map_y(Camera2DParams.zoom, Camera2DParams.target.y);

            float target_ecliptic_angle = is_ecliptic_frame ? (23.439f * DEG2RAD) : 0.0f;
            current_ecliptic_angle = Lerp(current_ecliptic_angle, target_ecliptic_angle, smooth_speed);

            /* keep Earth framed by co-rotating the orbit offset with the target's azimuth */
            bool follow_active = !is_2d_view && !is_pov_mode &&
                                 (active_lock == LOCK_MOON || active_lock == LOCK_SAT);
            if (follow_active)
            {
                if (active_lock != track_follow_lock || tracked_sat != track_follow_sat)
                {
                    track_follow_valid = false; /* target changed: re-baseline next frame, no jump */
                }
                /* radial from Earth (origin) to the tracked body */
                Vector3 r = Vector3Normalize(target_camera3d_target);
                float horiz = sqrtf(r.x * r.x + r.z * r.z);
                if (horiz > 0.001f) /* azimuth is undefined near the pole: hold */
                {
                    float phi = atan2f(r.x, r.z);
                    if (track_follow_valid)
                    {
                        float d = phi - track_follow_last_phi;
                        while (d > PI) d -= 2.0f * PI;   /* unwrap into (-PI, PI] */
                        while (d < -PI) d += 2.0f * PI;
                        track_follow_angle += d;
                    }
                    else
                    {
                        track_follow_angle = 0.0f;
                        track_follow_valid = true;
                    }
                    track_follow_last_phi = phi;
                }
            }
            else
            {
                track_follow_valid = false;
                track_follow_angle = 0.0f;
            }
            track_follow_lock = active_lock;
            track_follow_sat = tracked_sat;

            if (!is_2d_view)
            {
                Vector3 offset = {
                    camDistance * cosf(camAngleY) * sinf(camAngleX),
                    camDistance * sinf(camAngleY),
                    camDistance * cosf(camAngleY) * cosf(camAngleX)
                };
                if (follow_active && track_follow_valid)
                    offset = Vector3Transform(offset, MatrixRotateY(track_follow_angle)); /* co-rotate with the body */
                Vector3 upVec = {0.0f, 1.0f, 0.0f};

                if (current_ecliptic_angle > 0.0001f)
                {
                    Matrix rot = MatrixRotateX(current_ecliptic_angle);
                    offset = Vector3Transform(offset, rot);
                    upVec = Vector3Transform(upVec, rot);
                }

                Camera3DParams.position = Vector3Add(Camera3DParams.target, offset);

                /* world vertical up avoids roll with the target; POV overrides below */
                if ((active_lock == LOCK_MOON || active_lock == LOCK_SAT) && !is_pov_mode)
                {
                    Vector3 world_up = {0.0f, 1.0f, 0.0f};
                    Vector3 forward = Vector3Normalize(Vector3Subtract(Camera3DParams.target, Camera3DParams.position));
                    if (fabsf(Vector3DotProduct(forward, world_up)) > 0.9999f)
                        world_up = (Vector3){0.0f, 0.0f, 1.0f}; /* view parallel to up: stable fallback */
                    Camera3DParams.up = world_up;
                }
                else
                {
                    Camera3DParams.up = upVec;
                }
            }
        }

        Satellite *active_sat = hovered_sat ? hovered_sat : selected_sat;

        if (!is_2d_view && is_pov_mode && selected_sat && selected_sat->is_active)
        {
            Vector3 sat_pos_3d = Vector3Scale(selected_sat->current_pos, 1.0f / DRAW_SCALE);
            Camera3DParams.position = sat_pos_3d;
            
            /* create an LVLH local coordinate frame */
            double t_unix = get_unix_from_epoch(current_epoch);
            Vector3 vel = Vector3Normalize(calculate_velocity(selected_sat, t_unix));
            
            Vector3 nadir = Vector3Normalize(Vector3Negate(sat_pos_3d));
            
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


/* maximum cached coverage cap tessellation (matches COVERAGE_LOD_HIGH) */
#define FP2D_MAX_RINGS 12
#define FP2D_MAX_SEGS 60

        /* ground coverage scope: Sel (active satellite only) or All (every active satellite) */
        int gc_mode = ToolSettingGetInt(&cfg, LAYERS_KEY_GC_MODE, LAYERS_GC_MODE_SELECTED);

        // frame renders normally to the MSAA framebuffer; night mode is a post-process pass before EndDrawing()
        BeginDrawing();
        ClearBackground(g_theme.world.bg);

        /* scene context for tool draw_scene hooks (see tools_scene.h) */
        SceneContext sctx = {
            .is_2d_view = is_2d_view,
            .camera2d = &Camera2DParams,
            .camera3d = &Camera3DParams,
            .current_epoch = current_epoch,
            .gmst_deg = gmst_deg,
            .earth_rotation_offset = cfg.earth_rotation_offset,
            .draw_earth_radius = draw_earth_radius,
            .map_w = map_w,
            .map_h = map_h,
            .sun_dir_world = Vector3Normalize(calculate_sun_position(current_epoch)),
            .moon_pos_world = draw_moon_pos,
            .active_sat = active_sat,
            .selected_sat = selected_sat,
            .is_pov_mode = is_pov_mode
        };

        float m_size_2d = 24.0f * cfg.ui_scale / Camera2DParams.zoom;
        float mark_size_2d = 32.0f * cfg.ui_scale / Camera2DParams.zoom;

        /* Earth Texture toggle (Layers panel); off = theme-colored solid body */
        const bool show_earth = cfg.show_earth_texture;

        /* 2d projection rendering */
        if (is_2d_view)
        {
            int first_copy = 0, last_copy = 0;
            MapVisibleCopyRange(Camera2DParams, map_w, &first_copy, &last_copy);

            BeginMapMode2D(Camera2DParams);
            if (show_earth)
            {
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

                for (int k = first_copy; k <= last_copy; k++)
                    DrawTexturePro(earthTexture, (Rectangle){0, 0, earthTexture.width, earthTexture.height}, (Rectangle){k * map_w - map_w / 2, -map_h / 2, map_w, map_h}, (Vector2){0, 0}, 0.0f, WHITE);

                if (cfg.show_night_lights)
                    EndShaderMode();
            }
            else
            {
                /* texture off: theme-colored map surface */
                for (int k = first_copy; k <= last_copy; k++)
                    DrawRectangle((int)(k * map_w - map_w / 2.0f), (int)(-map_h / 2.0f), (int)map_w, (int)map_h,
                                  ThemeEarthColor());
            }

            // horizontal wrapping fills the viewport; vertically the map ends at the poles
            Vector2 mapMin = GetWorldToScreen2D((Vector2){-map_w / 2.0f, -map_h / 2.0f}, Camera2DParams);
            Vector2 mapMax = GetWorldToScreen2D((Vector2){map_w / 2.0f, map_h / 2.0f}, Camera2DParams);

            int vp_ix = (int)vp_x, vp_iy = (int)vp_y;
            int vp_ix1 = (int)(vp_x + vp_w), vp_iy1 = (int)(vp_y + vp_h);
            int sc_x = vp_ix, sc_w = vp_ix1 - vp_ix;
            int sc_y = (int)mapMin.y, sc_h = (int)(mapMax.y - mapMin.y);

            if (sc_y < vp_iy)
            {
                sc_h += sc_y - vp_iy;
                sc_y = vp_iy;
            }
            if (sc_y + sc_h > vp_iy1)
                sc_h = vp_iy1 - sc_y;

            if (sc_w > 0 && sc_h > 0)
            {
                BeginScissorMode(sc_x, sc_y, sc_w, sc_h);

                if (cfg.show_ground_coverage && !(is_pov_mode && selected_sat != NULL))
                {
                    Vector4 gc_color_2d = {
                        g_theme.world.footprint_fill.r / 255.0f,
                        g_theme.world.footprint_fill.g / 255.0f,
                        g_theme.world.footprint_fill.b / 255.0f,
                        g_theme.world.footprint_fill.a / 255.0f
                    };
                    SetShaderValue(g_coverage_shaders.shader2D, g_coverage_shaders.colorLoc2D,
                                   &gc_color_2d, SHADER_UNIFORM_VEC4);

                    Vector4 gc_border_2d = {
                        g_theme.world.footprint_border.r / 255.0f,
                        g_theme.world.footprint_border.g / 255.0f,
                        g_theme.world.footprint_border.b / 255.0f,
                        g_theme.world.footprint_border.a / 255.0f
                    };
                    SetShaderValue(g_coverage_shaders.shader2D, g_coverage_shaders.borderColorLoc2D,
                                   &gc_border_2d, SHADER_UNIFORM_VEC4);

                    /* visible map region (viewport ∩ map rect) for culling */
                    Vector2 vis_a = GetScreenToWorld2D((Vector2){vp_x, vp_y}, Camera2DParams);
                    Vector2 vis_b = GetScreenToWorld2D((Vector2){vp_x + vp_w, vp_y + vp_h}, Camera2DParams);
                    float clip_min_x = fminf(vis_a.x, vis_b.x);
                    float clip_max_x = fmaxf(vis_a.x, vis_b.x);
                    float clip_min_y = fmaxf(fminf(vis_a.y, vis_b.y), -map_h * 0.5f);
                    float clip_max_y = fminf(fmaxf(vis_a.y, vis_b.y), map_h * 0.5f);

                    if (clip_min_x < clip_max_x && clip_min_y < clip_max_y)
                    {
                        int gc_first = 0;
                        int gc_last = sat_count;

                        /* emit one satellite's cap (all visible wrap copies) */
                        auto draw_coverage_2d = [&](const Satellite *sat) {
                            if (!sat->is_active)
                                return;

                            float sat_r = Vector3Length(sat->current_pos);
                            if (sat_r <= EARTH_RADIUS_KM)
                                return;

                            float theta = acosf(EARTH_RADIUS_KM / sat_r);
                            Vector3 s_norm = Vector3Normalize(sat->current_pos);

                            /* cap centre on the map (sub-satellite point) */
                            float cx, cy;
                            get_map_coordinates(sat->current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &cx, &cy);

 
                            float phi_c = acosf(fminf(fmaxf(s_norm.y, -1.0f), 1.0f));
                            float dlon_max = PI;
                            if (phi_c >= theta && phi_c <= PI - theta)
                                dlon_max = asinf(fminf(1.0f, sinf(theta) / sinf(phi_c)));
                            float rx = (dlon_max / (2.0f * PI)) * map_w + 1.0f;
                            float ry = (theta / PI) * map_h + 1.0f;

                            bool any_visible = false;
                            for (int oi = first_copy; oi <= last_copy; oi++)
                            {
                                float x_off = oi * map_w;
                                if (cx + x_off - rx < clip_max_x && cx + x_off + rx > clip_min_x &&
                                    cy - ry < clip_max_y && cy + ry > clip_min_y)
                                {
                                    any_visible = true;
                                    break;
                                }
                            }
                            if (!any_visible)
                                return;

                            /* cached cap geometry + 2D LOD (no per-frame rebuild) */
                            CoverageMeshLOD lod = SelectCoverageLOD2D(sat, Camera2DParams.zoom, map_w);
                            const CoverageCapData *cap = GetCachedCoverageCap(sat_r - EARTH_RADIUS_KM, lod);
                            if (!cap || cap->rings < 1 || cap->segments < 3)
                                return;

                            /* basis matching the cached mesh (local +Y = sub-satellite) */
                            Vector3 basis_up = fabsf(s_norm.y) > 0.99f ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
                            Vector3 basis_u = Vector3Normalize(Vector3CrossProduct(basis_up, s_norm));
                            Vector3 basis_v = Vector3CrossProduct(s_norm, basis_u);

                            int rings = cap->rings;
                            int segments = cap->segments;
                            float proj_x[FP2D_MAX_RINGS + 1][FP2D_MAX_SEGS];
                            float proj_y[FP2D_MAX_RINGS + 1][FP2D_MAX_SEGS];

                            /* project each cap vertex once, not four times per quad */
                            for (int ring = 0; ring <= rings; ring++)
                            {
                                for (int seg = 0; seg < segments; seg++)
                                {
                                    const float *L = &cap->vertices[(ring * segments + seg) * 3];
                                    Vector3 world_draw = Vector3Add(
                                        Vector3Add(Vector3Scale(basis_u, L[0]), Vector3Scale(s_norm, L[1])),
                                        Vector3Scale(basis_v, L[2]));
                                    Vector3 pos_km = Vector3Scale(world_draw, DRAW_SCALE);
                                    get_map_coordinates(pos_km, gmst_deg, cfg.earth_rotation_offset, map_w, map_h,
                                                        &proj_x[ring][seg], &proj_y[ring][seg]);
                                }
                            }

                            for (int ring = 0; ring < rings; ring++)
                            {
                                float r_inner = (float)ring / rings;
                                float r_outer = (float)(ring + 1) / rings;
                                for (int seg = 0; seg < segments; seg++)
                                {
                                    int next = (seg + 1) % segments;
                                    float x1 = proj_x[ring][seg],      y1 = proj_y[ring][seg];
                                    float x2 = proj_x[ring][next],     y2 = proj_y[ring][next];
                                    float x3 = proj_x[ring + 1][seg],  y3 = proj_y[ring + 1][seg];
                                    float x4 = proj_x[ring + 1][next], y4 = proj_y[ring + 1][next];

                                    /* anchor to x1 so the antimeridian seam stays contiguous */
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


                                    float qmin_x = fminf(fminf(x1, x2), fminf(x3, x4));
                                    float qmax_x = fmaxf(fmaxf(x1, x2), fmaxf(x3, x4));
                                    float qmin_y = fminf(fminf(y1, y2), fminf(y3, y4));
                                    float qmax_y = fmaxf(fmaxf(y1, y2), fmaxf(y3, y4));

                                    for (int oi = first_copy; oi <= last_copy; oi++)
                                    {
                                        float x_off = oi * map_w;
                                        if (qmin_x + x_off >= clip_max_x || qmax_x + x_off <= clip_min_x ||
                                            qmin_y >= clip_max_y || qmax_y <= clip_min_y)
                                            continue;
                                        rlColor4ub(255, 255, 255, 255);
                                        rlTexCoord2f(r_inner, 0.0f); rlVertex2f(x1 + x_off, y1);
                                        rlTexCoord2f(r_outer, 0.0f); rlVertex2f(x3 + x_off, y3);
                                        rlTexCoord2f(r_inner, 0.0f); rlVertex2f(x2 + x_off, y2);
                                        rlTexCoord2f(r_inner, 0.0f); rlVertex2f(x2 + x_off, y2);
                                        rlTexCoord2f(r_outer, 0.0f); rlVertex2f(x3 + x_off, y3);
                                        rlTexCoord2f(r_outer, 0.0f); rlVertex2f(x4 + x_off, y4);
                                    }
                                }
                            }
                        };

                        auto draw_highlighted_2d = [&](const Satellite *sat, Color fill, Color border) {
                            Vector4 gc_fill = {
                                fill.r / 255.0f,
                                fill.g / 255.0f,
                                fill.b / 255.0f,
                                fill.a / 255.0f
                            };
                            SetShaderValue(g_coverage_shaders.shader2D,
                                           g_coverage_shaders.colorLoc2D,
                                           &gc_fill, SHADER_UNIFORM_VEC4);

                            Vector4 gc_border = {
                                border.r / 255.0f,
                                border.g / 255.0f,
                                border.b / 255.0f,
                                border.a / 255.0f
                            };
                            SetShaderValue(g_coverage_shaders.shader2D,
                                           g_coverage_shaders.borderColorLoc2D,
                                           &gc_border, SHADER_UNIFORM_VEC4);

                            BeginShaderMode(g_coverage_shaders.shader2D);
                            rlBegin(RL_TRIANGLES);
                            draw_coverage_2d(sat);
                            rlEnd();
                            EndShaderMode();
                        };

                        if (gc_mode == LAYERS_GC_MODE_ALL || gc_mode == LAYERS_GC_MODE_FAV)
                        {
                            /* static: keeps this large buffer off the stack */
                            static uint32_t fav_ids[MAX_SATELLITES_HARD_CAP];
                            int fav_count = (gc_mode == LAYERS_GC_MODE_FAV)
                                ? GetFavoriteIds(fav_ids, MAX_SATELLITES_HARD_CAP) : 0;

                            BeginShaderMode(g_coverage_shaders.shader2D);
                            rlBegin(RL_TRIANGLES);
                            for (int i = gc_first; i < gc_last; i++)
                            {
                                const Satellite *sat = &satellites[i];
                                if (sat == selected_sat || sat == hovered_sat)
                                    continue; /* highlighted in a batch below */
                                if (gc_mode == LAYERS_GC_MODE_FAV)
                                {
                                    bool is_fav = false;
                                    for (int k = 0; k < fav_count; k++)
                                    {
                                        if (sat->norad_id_num == fav_ids[k])
                                        {
                                            is_fav = true;
                                            break;
                                        }
                                    }
                                    if (!is_fav && sat != selected_sat)
                                        continue;
                                }
                                draw_coverage_2d(sat);
                            }
                            rlEnd();
                            EndShaderMode();
                        }

                        if (hovered_sat != NULL && hovered_sat != selected_sat && hovered_sat->is_active)
                        {
                            draw_highlighted_2d(hovered_sat,
                                                CoverageHoverFill(g_theme.world.footprint_fill),
                                                g_theme.world.sat_hover);
                        }

                        if (selected_sat != NULL && selected_sat->is_active)
                        {
                            draw_highlighted_2d(selected_sat,
                                                CoverageSelectFill(g_theme.world.footprint_fill),
                                                g_theme.world.sat_selected);
                        }
                    }
                }

                // future ground tracks share a fixed propagation budget (focused sat keeps full resolution, the rest split the remainder); unified Orbits layer: master kills all, dimmed/fav-color toggles pick the extra tracks
                const bool future_orbits_enabled =
                    ToolSettingGetBool(&cfg, LAYERS_KEY_ORBITS, true);
                const bool orbits_dimmed = ToolSettingGetBool(&cfg, LAYERS_KEY_ORBITS_DIMMED, true);
                const bool orbits_fav_colored = ToolSettingGetBool(&cfg, LAYERS_KEY_FAV_ORBITS_3D, false);
                const int sunlit_scope =
                    ToolSettingGetInt(&cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE,
                                      LAYERS_ORBITS_SUNLIT_SELECTED);

                const bool extra_orbits_enabled = orbits_dimmed || orbits_fav_colored;
                const bool focused_track_valid =
                    active_sat && active_sat->is_active && active_sat->mean_motion > 0.0 &&
                    !(is_pov_mode && active_sat == selected_sat);

                const float future_orbit_span =
                    fmaxf(ToolSettingGetFloat(&cfg, LAYERS_KEY_FUTURE_ORBITS_STEPS,
                                              LAYERS_FUTURE_ORBITS_STEPS_DEFAULT),
                          LAYERS_FUTURE_ORBITS_STEPS_MIN);
                const int requested_future_segments =
                    (int)fminf(4000.0f, fmaxf(50.0f, 400.0f * future_orbit_span));
                const int future_segment_budget = 6000;

                // extra (non-focused) sats to draw future tracks for; extra_track_is_fav keeps the two tiers in lockstep
                std::vector<const Satellite*> extra_track_sats;
                std::vector<bool> extra_track_is_fav;
                if (future_orbits_enabled && extra_orbits_enabled)
                {
                    extra_track_sats.reserve(sat_count);
                    extra_track_is_fav.reserve(sat_count);
                    for (int i = 0; i < sat_count; i++)
                    {
                        const Satellite *sat = &satellites[i];
                        if (!sat->is_active || sat->mean_motion <= 0.0)
                            continue;
                        if (sat == active_sat)
                            continue; /* the focused highlight track is drawn separately */
                        if (is_pov_mode && sat == selected_sat)
                            continue;
                        const bool is_fav = IsFavorite(sat->norad_id_num);
                        /* fav-colored extras: only when the fav-color toggle is on */
                        if (orbits_fav_colored && is_fav)
                        {
                            extra_track_sats.push_back(sat);
                            extra_track_is_fav.push_back(true);
                            continue; /* fav wins over dimmed; skip dimmed for this sat */
                        }
                        // dimmed unselected orbits: driven directly by the dimmed toggle
                        if (orbits_dimmed)
                        {
                            extra_track_sats.push_back(sat);
                            extra_track_is_fav.push_back(false);
                        }
                    }
                }
                const int extra_track_count = (int)extra_track_sats.size();

                int extra_future_segments = requested_future_segments;
                if (extra_track_count > 0)
                {
                    const int focused_cost = focused_track_valid ? requested_future_segments : 0;
                    const int remaining_budget = future_segment_budget - focused_cost;
                    extra_future_segments = remaining_budget / extra_track_count;
                    if (extra_future_segments > requested_future_segments)
                        extra_future_segments = requested_future_segments;
                    if (extra_future_segments < 50)
                        extra_future_segments = 50;
                }

                Vector3 future_sun_dir = {0};
                if (future_orbits_enabled && cfg.highlight_sunlit)
                    future_sun_dir = Vector3Normalize(calculate_sun_position(current_epoch));

                /* three passes: dimmed tracks, then important tracks, then satellite icons on top */
                for (int pass = 0; pass < 3; pass++)
                {
                    for (int i = 0; i < sat_count; i++)
                    {
                        if (!satellites[i].is_active)
                            continue;
                        bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                        float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                        if (sat_alpha <= 0.0f)
                            continue;

                        bool is_hl = (active_sat == &satellites[i]);
                        const auto extra_it =
                            std::find(extra_track_sats.begin(), extra_track_sats.end(),
                                      &satellites[i]);
                        const bool extra_is_fav_color =
                            extra_it != extra_track_sats.end() &&
                            extra_track_is_fav[(size_t)(extra_it - extra_track_sats.begin())];
                        const bool draw_future_track =
                            future_orbits_enabled && satellites[i].mean_motion > 0.0 &&
                            !(is_pov_mode && &satellites[i] == selected_sat) &&
                            (is_hl || extra_it != extra_track_sats.end());
                        const bool track_important = is_hl || extra_is_fav_color;

                        Color sCol = (selected_sat == &satellites[i])
                            ? g_theme.world.sat_selected
                            : (hovered_sat == &satellites[i])
                                ? g_theme.world.sat_hover
                                : g_theme.world.sat;
                        if (draw_future_track)
                        {
                            sCol = is_hl ? g_theme.world.orbit_active
                                         : (extra_is_fav_color ? MultiGroundTrackColor(i)
                                                               : g_theme.world.orbit);
                        }
                        /* orbit color is faint for lines; keep the satellite icon itself visible */
                        if (sCol.a < g_theme.world.sat.a)
                            sCol.a = g_theme.world.sat.a;
                        sCol = ApplyAlpha(sCol, sat_alpha);

                        /* sunlit scope: Sel = active only, Fav = favorites only, All = everyone */
                        bool apply_sunlit = sunlit_scope == LAYERS_ORBITS_SUNLIT_ALL ||
                                            (sunlit_scope == LAYERS_ORBITS_SUNLIT_SELECTED && is_hl) ||
                                            (sunlit_scope == LAYERS_ORBITS_SUNLIT_FAV &&
                                             IsFavorite(satellites[i].norad_id_num));

                        /* pass 0 draws dimmed tracks, pass 1 draws important (active + favourite) tracks on top */
                        const bool track_pass =
                            (pass == 0 && !track_important) || (pass == 1 && track_important);

                        if (draw_future_track && track_pass)
                        {
                            const int segments = is_hl ? requested_future_segments : extra_future_segments;
                            Vector2 track_pts[4001];
                            bool is_sunlit_arr[4001];
                            double track_times[4001];

                            const Color track_color = ApplyAlpha(
                                is_hl ? g_theme.world.orbit_active
                                      : (extra_is_fav_color ? MultiGroundTrackColor(i)
                                                            : g_theme.world.orbit),
                                sat_alpha);

                            double period_days = (2.0 * PI / satellites[i].mean_motion) / 86400.0;
                            if (!(period_days > 0.0))
                                period_days = 1.0 / 1440.0;

                            const bool want_sunlit = cfg.highlight_sunlit && apply_sunlit;
                            const FutureTrackCacheEntry *ftc = GetFutureTrackSamples(
                                &satellites[i], segments, future_orbit_span, current_epoch,
                                cfg.earth_rotation_offset, map_w, map_h, want_sunlit, future_sun_dir);

                            for (int j = 0; j <= segments; j++)
                            {
                                track_pts[j] = ftc->pts[j];
                                track_times[j] = ftc->times[j];
                                if (want_sunlit)
                                    is_sunlit_arr[j] = ftc->sunlit[j];
                            }

                            /* per-sample thingy, pain */
                            bool in_transit[4001] = {false};
                            if (num_passes > 0)
                            {
                                for (int j = 0; j <= segments; j++)
                                {
                                    const double te = track_times[j];
                                    for (int pi = 0; pi < num_passes; pi++)
                                    {
                                        const SatPass &sp = passes[pi];
                                        if (sp.sat != &satellites[i])
                                            continue;
                                        if (sp.los_epoch > sp.aos_epoch && te >= sp.aos_epoch && te <= sp.los_epoch)
                                        {
                                            in_transit[j] = true;
                                            break;
                                        }
                                    }
                                }
                            }

                            for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
                            {
                                float x_off = offset_i * map_w;
                                for (int j = 1; j <= segments; j++)
                                {
                                    if (fabs(track_pts[j].x - track_pts[j - 1].x) < map_w * 0.6f)
                                    {
                                        Color drawCol = track_color;
                                        if (cfg.highlight_sunlit && apply_sunlit && is_sunlit_arr[j])
                                            drawCol = ApplyAlpha(g_theme.world.sat_hover, sat_alpha);
                                        float track_w = is_hl ? 2.0f : (extra_is_fav_color ? 1.5f : 1.0f);
                                        /* direction trail is a selected-orbit indicator: dimmed tracks stay flat */
                                        if (is_hl || extra_is_fav_color)
                                        {
                                            const float dir_floor = is_hl ? ORBIT_DIR_MIN_SELECTED : ORBIT_DIR_MIN;
                                            // 2D tracks only draw the future; anchor the trail at the sat and clamp so it never wraps
                                            const double dir_phase = (track_times[j] - current_epoch) / period_days;
                                            const double trail_phase = fmax(-dir_phase, -(double)ORBIT_DIR_TRAIL_LEN);
                                            drawCol = ApplyOrbitDirection(drawCol, trail_phase, dir_floor);
                                            /* keep the 2D trail thin: damp the width ramp the 3D-style width uses */
                                            track_w *= 1.0f + (OrbitDirectionWidthScale(trail_phase, dir_floor) - 1.0f) * 0.2f;
                                        }
                                        /* transit accent wins over all other segment coloring */
                                        if (in_transit[j - 1] || in_transit[j])
                                            drawCol = g_theme.ui.accent;
                                        track_w /= Camera2DParams.zoom;
                                        DrawLineEx((Vector2){track_pts[j - 1].x + x_off, track_pts[j - 1].y}, (Vector2){track_pts[j].x + x_off, track_pts[j].y}, track_w, drawCol);
                                    }
                                }

                                if (cfg.show_apsides && is_hl)
                                {
                                    Vector2 peri2d, apo2d;
                                    get_apsis_2d(&satellites[i], current_epoch, false, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &peri2d);
                                    get_apsis_2d(&satellites[i], current_epoch, true, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &apo2d);

                                    DrawTexturePro(
                                        periMark, (Rectangle){0, 0, periMark.width, periMark.height}, (Rectangle){peri2d.x + x_off, peri2d.y, mark_size_2d, mark_size_2d},
                                        (Vector2){mark_size_2d / 2.f, mark_size_2d / 2.f}, 0.0f, ApplyAlpha(g_theme.world.periapsis, sat_alpha)
                                    );
                                    DrawTexturePro(
                                        apoMark, (Rectangle){0, 0, apoMark.width, apoMark.height}, (Rectangle){apo2d.x + x_off, apo2d.y, mark_size_2d, mark_size_2d},
                                        (Vector2){mark_size_2d / 2.f, mark_size_2d / 2.f}, 0.0f, ApplyAlpha(g_theme.world.apoapsis, sat_alpha)
                                    );

                                }
                            }
                        }

                        /* pass 2: satellite icons stay on top of every track */
                        if (pass == 2)
                        {
                            float sat_mx, sat_my;
                            get_map_coordinates(satellites[i].current_pos, gmst_deg, cfg.earth_rotation_offset, map_w, map_h, &sat_mx, &sat_my);
                            if (!(is_pov_mode && &satellites[i] == selected_sat))
                            {
                                for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
                                {
                                    DrawTexturePro(
                                        satIcon, (Rectangle){0, 0, satIcon.width, satIcon.height}, (Rectangle){sat_mx + (offset_i * map_w), sat_my, m_size_2d, m_size_2d},
                                        (Vector2){m_size_2d / 2.f, m_size_2d / 2.f}, 0.0f, sCol
                                    );
                                }
                            }
                        }
                    }
                }

                /* ground station markers (home location) */
                Location *home = GetHomeLocation();
                float hx = home ? (home->lon / 360.0f) * map_w : 0.0f;
                float hy = home ? -(home->lat / 180.0f) * map_h : 0.0f;
                if (home && cfg.show_markers)
                {
                    for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
                    {
                        float x_off = offset_i * map_w;
                        DrawTexturePro(
                            markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){hx + x_off, hy, m_size_2d, m_size_2d}, (Vector2){m_size_2d / 2.f, m_size_2d / 2.f}, 0.0f, WHITE
                        );
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

                    for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
                    {
                        float x_off = offset_i * map_w;
                        Vector2 p1 = {hx + x_off, hy};
                        Vector2 p2 = {sx + x_off, sy};
                        DrawLineEx(p1, p2, 2.0f / Camera2DParams.zoom, ApplyAlpha(g_theme.ui.accent, 0.8f));
                    }
                }

                if (cfg.show_markers)
                {
                    for (int i = 0; i < location_count; i++)
                    {
                        if (locations[i].is_home)
                            continue; /* home is drawn separately above */
                        float mx = (locations[i].lon / 360.0f) * map_w;
                        float my = -(locations[i].lat / 180.0f) * map_h;
                        for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
                        {
                            float x_off = offset_i * map_w;
                            DrawTexturePro(
                                markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){mx + x_off, my, m_size_2d, m_size_2d}, (Vector2){m_size_2d / 2.f, m_size_2d / 2.f},
                                0.0f, WHITE
                            );
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
                    for (int offset_i = first_copy; offset_i <= last_copy; offset_i++)
                    {
                        float x_off = offset_i * map_w;
                        DrawTexturePro(
                            markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){mx + x_off, my, m_size_2d, m_size_2d}, (Vector2){m_size_2d / 2.f, m_size_2d / 2.f}, 0.0f,
                            (Color){0, 255, 255, 255}
                        );
                    }
                }
            }

            /* tool scene hooks (2D overlays) */
            DrawSceneHooks(&sctx, &cfg);

            EndMode2D();
        }
        else
        {
            /* 3d globe rendering */
        Viewport3DApplyGL();
        BeginMode3D(Camera3DParams);
        Viewport3DFixProjection(Camera3DParams);
        
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

        double continuous_cloud_angle = fmod(gmst_deg + cfg.earth_rotation_offset + (current_epoch * 360.0 * 0.04), 360.0);
        float cloud_rot_rad = (float)(continuous_cloud_angle * DEG2RAD);

        if (cfg.show_night_lights)
        {
            earthModel.materials[0].shader = shader3D;
            SetShaderValue(shader3D, sunDirLoc3D, &sunEcef, SHADER_UNIFORM_VEC3);
            SetShaderValue(shader3D, moonPosLoc3D, &moonEcef, SHADER_UNIFORM_VEC3);
            
            int doAdvScat = cfg.show_scattering ? 1 : 0;
            SetShaderValue(shader3D, advScatLoc3D, &doAdvScat, SHADER_UNIFORM_INT);
            SetShaderValue(shader3D, viewPosLoc3D, &viewEcef, SHADER_UNIFORM_VEC3);
            
            float uv_offset = (earth_rot_rad - cloud_rot_rad) / (2.0f * PI);
            SetShaderValue(shader3D, cloudUVOffsetLoc3D, &uv_offset, SHADER_UNIFORM_FLOAT);

            int doShowClouds = cfg.show_clouds ? 1 : 0;
            SetShaderValue(shader3D, showCloudsLoc3D, &doShowClouds, SHADER_UNIFORM_INT);
        }
        else
        {
            earthModel.materials[0].shader = defaultEarthShader;
        }

        if (show_earth)
        {
            /* textured Earth (shader selected above) */
            DrawModel(earthModel, Vector3Zero(), 1.0f, WHITE);
        }
        else
        {
            /* textureless Earth: shaded, theme-derived solid body */
            Color earth_base = ThemeEarthColor();
            Vector4 earth_base_v = {
                earth_base.r / 255.0f,
                earth_base.g / 255.0f,
                earth_base.b / 255.0f,
                earth_base.a / 255.0f
            };
            earthModel.materials[0].shader = earthSolidShader;
            SetShaderValue(earthSolidShader, earthSolidSunLoc, &sunEcef, SHADER_UNIFORM_VEC3);
            SetShaderValue(earthSolidShader, earthSolidBaseLoc, &earth_base_v, SHADER_UNIFORM_VEC4);
            DrawModel(earthModel, Vector3Zero(), 1.0f, WHITE);
            /* restore default shader for later draw paths */
            earthModel.materials[0].shader = defaultEarthShader;
        }

        /* atmosphere/cloud layer */
        if (cfg.show_clouds)
        {
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

            rlDrawRenderBatchActive();
            rlDisableDepthMask();
            DrawModel(cloudModel, Vector3Zero(), 1.0f, WHITE);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
        }

        /* atmospheric layer rendering */
        if (cfg.show_scattering)
        {
            atmosphereModel.transform = MatrixRotateY(earth_rot_rad);
            SetShaderValue(shaderAtmosphere, sunDirLocAtmosphere, &sunEcef, SHADER_UNIFORM_VEC3);
            SetShaderValue(shaderAtmosphere, viewPosLocAtmosphere, &viewEcef, SHADER_UNIFORM_VEC3);
            // like the cloud shell: transparent glow must not occlude the ground coverage
            rlDrawRenderBatchActive();
            rlDisableDepthMask();
            DrawModel(atmosphereModel, Vector3Zero(), 1.0f, WHITE);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
        }

        Vector3 sunDirWorld = Vector3Normalize(calculate_sun_position(current_epoch));
            SetShaderValue(shaderMoon, sunDirLocMoon, &sunDirWorld, SHADER_UNIFORM_VEC3);
            SetShaderValue(shaderMoon, moonPosLocMoon, &draw_moon_pos, SHADER_UNIFORM_VEC3);
            SetShaderValueMatrix(shaderMoon, moonRotLocMoon, moonModel.transform);

            DrawModel(moonModel, draw_moon_pos, 1.0f, WHITE);

            /* Shader-based ground coverage rendering (3D) */
            if (cfg.show_ground_coverage && !(is_pov_mode && selected_sat != NULL))
            {
                SetShaderValue(g_coverage_shaders.shader3D, g_coverage_shaders.cameraPosLoc,
                               &Camera3DParams.position, SHADER_UNIFORM_VEC3);
                
                /* static: keeps this large buffer off the stack */
                static Satellite *visible_sats[MAX_SATELLITES_HARD_CAP];
                int visible_count = 0;
                
                if (gc_mode == LAYERS_GC_MODE_ALL)
                {
                    for (int i = 0; i < sat_count; i++)
                    {
                        if (!satellites[i].is_active)
                            continue;
                        if (IsCoverageVisible(&satellites[i], Camera3DParams))
                        {
                            visible_sats[visible_count++] = &satellites[i];
                        }
                    }
                }
                else if (gc_mode == LAYERS_GC_MODE_FAV)
                {
                    /* static: keeps this large buffer off the stack */
                    static uint32_t fav_ids[MAX_SATELLITES_HARD_CAP];
                    int fav_count = GetFavoriteIds(fav_ids, MAX_SATELLITES_HARD_CAP);
                    for (int i = 0; i < sat_count; i++)
                    {
                        if (!satellites[i].is_active)
                            continue;
                        bool is_fav = false;
                        for (int k = 0; k < fav_count; k++)
                        {
                            if (satellites[i].norad_id_num == fav_ids[k])
                            {
                                is_fav = true;
                                break;
                            }
                        }
                        if (!is_fav && &satellites[i] != selected_sat)
                            continue;
                        if (IsCoverageVisible(&satellites[i], Camera3DParams))
                        {
                            visible_sats[visible_count++] = &satellites[i];
                        }
                    }
                }
                else
                {
                    /* Sel mode: selected sat, plus hovered as extra highlight */
                    if (selected_sat && selected_sat->is_active &&
                        IsCoverageVisible(selected_sat, Camera3DParams))
                    {
                        visible_sats[visible_count++] = selected_sat;
                    }
                    if (hovered_sat && hovered_sat != selected_sat && hovered_sat->is_active &&
                        IsCoverageVisible(hovered_sat, Camera3DParams))
                    {
                        visible_sats[visible_count++] = hovered_sat;
                    }
                }
                
                std::sort(visible_sats, visible_sats + visible_count, [&](Satellite *a, Satellite *b) {
                    auto rank = [&](Satellite *s) -> int {
                        if (s == selected_sat) return 2; /* selected draws last */
                        if (s == hovered_sat)  return 1; /* hovered just below */
                        return 0;
                    };
                    int rank_a = rank(a);
                    int rank_b = rank(b);
                    if (rank_a != rank_b)
                        return rank_a < rank_b;
                    float dist_a = Vector3Distance(Camera3DParams.position,
                                                   Vector3Scale(a->current_pos, 1.0f/DRAW_SCALE));
                    float dist_b = Vector3Distance(Camera3DParams.position,
                                                   Vector3Scale(b->current_pos, 1.0f/DRAW_SCALE));
                    return dist_a > dist_b;  // Farther first
                });
                
                /* Disable depth writes for proper alpha blending */
                rlDisableDepthMask();
                rlSetBlendMode(BLEND_ALPHA);
                
                for (int i = 0; i < visible_count; i++)
                {
                    Satellite *sat = visible_sats[i];
                    
                    float theta, radius;
                    CalculateCoverageParams(sat, &theta, &radius);
                    if (theta <= 0.0f) continue;
                    
                    CoverageMeshLOD lod = SelectCoverageLOD(sat, Camera3DParams);
                    
                    float altitude_km = Vector3Length(sat->current_pos) - EARTH_RADIUS_KM;
                    Model *coverage_model = GetCachedCoverageMesh(altitude_km, lod, g_coverage_shaders.shader3D);
                    
                    /* mesh is an Earth-centred cap; shader reorients it from this pos */
                    Vector3 sat_pos_draw = Vector3Scale(sat->current_pos, 1.0f / DRAW_SCALE);
                    SetShaderValue(g_coverage_shaders.shader3D, g_coverage_shaders.satPosLoc,
                                   &sat_pos_draw, SHADER_UNIFORM_VEC3);
                    
                    /* selection tint wins over hover when both apply */
                    Color fill_col   = g_theme.world.footprint_fill;
                    Color border_col = g_theme.world.footprint_border;
                    if (sat == selected_sat)
                    {
                        fill_col   = CoverageSelectFill(g_theme.world.footprint_fill);
                        border_col = g_theme.world.sat_selected;
                    }
                    else if (sat == hovered_sat)
                    {
                        fill_col   = CoverageHoverFill(g_theme.world.footprint_fill);
                        border_col = g_theme.world.sat_hover;
                    }

                    Vector4 color = {
                        fill_col.r / 255.0f,
                        fill_col.g / 255.0f,
                        fill_col.b / 255.0f,
                        fill_col.a / 255.0f
                    };
                    SetShaderValue(g_coverage_shaders.shader3D, g_coverage_shaders.colorLoc,
                                   &color, SHADER_UNIFORM_VEC4);

                    Vector4 border = {
                        border_col.r / 255.0f,
                        border_col.g / 255.0f,
                        border_col.b / 255.0f,
                        border_col.a / 255.0f
                    };
                    SetShaderValue(g_coverage_shaders.shader3D, g_coverage_shaders.borderColorLoc,
                                   &border, SHADER_UNIFORM_VEC4);
                    
                    DrawModel(*coverage_model, Vector3Zero(), 1.0f, WHITE);
                }
                
                rlEnableDepthMask();
            }

            /* unified Orbits layer: the master toggle kills ALL orbit drawing */
            const bool orbits_enabled = ToolSettingGetBool(&cfg, LAYERS_KEY_ORBITS, true);
            const bool orbits_dimmed = ToolSettingGetBool(&cfg, LAYERS_KEY_ORBITS_DIMMED, true);
            const bool orbits_fav_colored = ToolSettingGetBool(&cfg, LAYERS_KEY_FAV_ORBITS_3D, false);
            const int sunlit_scope = ToolSettingGetInt(&cfg, LAYERS_KEY_ORBITS_SUNLIT_SCOPE, LAYERS_ORBITS_SUNLIT_SELECTED);

            /* static: keeps this large buffer off the stack */
            static uint32_t fav_ids[MAX_SATELLITES_HARD_CAP];
            int fav_count = orbits_fav_colored ? GetFavoriteIds(fav_ids, MAX_SATELLITES_HARD_CAP) : 0;

            /* one orbit style per satellite: sunlit > highlight > fav-color > dimmed */
            if (orbits_enabled)
            {
                /* important pass draws with depth test off so dimmed orbits and the Earth can't occlude it */
                for (int pass = 0; pass < 2; pass++)
                {
                    if (pass == 1)
                    {
                        rlDrawRenderBatchActive();
                        rlDisableDepthTest();
                        rlDisableDepthMask();
                    }

                    for (int i = 0; i < sat_count; i++)
                    {
                        if (!satellites[i].is_active)
                            continue;
                        if (is_pov_mode && &satellites[i] == selected_sat)
                            continue;
                        bool is_hl = (active_sat == &satellites[i]);
                        bool is_unselected = (selected_sat != NULL && &satellites[i] != selected_sat);
                        float sat_alpha = is_unselected ? unselected_fade : 1.0f;
                        if (sat_alpha <= 0.0f)
                            continue;

                        bool is_fav = false;
                        int fav_index = 0;
                        if (orbits_fav_colored)
                        {
                            for (int k = 0; k < fav_count; k++)
                            {
                                if (satellites[i].norad_id_num == fav_ids[k])
                                {
                                    is_fav = true;
                                    fav_index = k;
                                    break;
                                }
                            }
                        }

                        /* sunlit scope: Sel = active only, Fav = favorites only, All = everyone */
                        bool apply_sunlit = sunlit_scope == LAYERS_ORBITS_SUNLIT_ALL ||
                                            (sunlit_scope == LAYERS_ORBITS_SUNLIT_SELECTED && is_hl) ||
                                            (sunlit_scope == LAYERS_ORBITS_SUNLIT_FAV &&
                                             IsFavorite(satellites[i].norad_id_num));

                        /* priority 2: highlighted (active) orbit outranks fav-color */
                        if (is_hl)
                        {
                            if (pass == 1)
                            {
                                draw_orbit_3d(&satellites[i], current_epoch, true, sat_alpha, global_orbit_step, apply_sunlit, Camera3DParams.position);
                                Vector3 draw_pos = Vector3Scale(satellites[i].current_pos, 1.0f / DRAW_SCALE);
                                float ground_r = draw_earth_radius * 1.001f;
                                Vector3 ground_pt = Vector3Scale(Vector3Normalize(draw_pos), ground_r);
                                if (!IsOrbitSegmentOccluded(Camera3DParams.position, ground_pt, draw_pos, draw_earth_radius * 0.995f))
                                {
                                    rlDrawRenderBatchActive();
                                    DrawLine3D(ground_pt, draw_pos, ApplyAlpha(g_theme.world.orbit_active, sat_alpha));
                                    rlDrawRenderBatchActive();
                                }
                            }
                            continue;
                        }

                        /* priority 3: fav-colored orbit outranks the dimmed unselected track */
                        if (is_fav && orbits_fav_colored)
                        {
                            if (pass == 1)
                            {
                                Color fav_color = MultiGroundTrackColor(fav_index);
                                draw_orbit_3d_colored(&satellites[i], current_epoch, fav_color, 0.9f, global_orbit_step, apply_sunlit, Camera3DParams.position);
                            }
                            continue;
                        }

                        /* dimmed/unselected orbit (first pass only) */
                        if (pass == 0)
                        {
                            if (&satellites[i] != selected_sat && !orbits_dimmed)
                                continue;
                            draw_orbit_3d(&satellites[i], current_epoch, false, sat_alpha, global_orbit_step, apply_sunlit, Camera3DParams.position);
                        }
                    }

                    if (pass == 1)
                    {
                        rlDrawRenderBatchActive();
                        rlEnableDepthTest();
                        rlEnableDepthMask();
                    }
                }
            }

            /* slant range overlay 3d line */
            Location *home = GetHomeLocation();
            if (cfg.show_slant_range && home && active_sat && active_sat->is_active)
            {
                float h_lat_rad = home->lat * DEG2RAD;
                float h_lon_rad = (home->lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                Vector3 h_pos3d = {cosf(h_lat_rad) * cosf(h_lon_rad) * draw_earth_radius, sinf(h_lat_rad) * draw_earth_radius, -cosf(h_lat_rad) * sinf(h_lon_rad) * draw_earth_radius};
                Vector3 s_pos3d = Vector3Scale(active_sat->current_pos, 1.0f / DRAW_SCALE);

                // flush the batch, then disable depth test so the cloud/scattering/coverage shells don't occlude the line
                rlDrawRenderBatchActive();
                rlDisableDepthTest();
                rlDisableDepthMask();
                DrawLine3D(h_pos3d, s_pos3d, ApplyAlpha(g_theme.ui.accent, 0.6f));
                rlDrawRenderBatchActive();
                rlEnableDepthTest();
                rlEnableDepthMask();
            }

            /* tool scene hooks (3D overlays) */
            DrawSceneHooks(&sctx, &cfg);

            EndMode3D();


            Viewport3DResetGL();

            /* screen-space icons/text for 3d objects */
            float m_size_3d = 24.0f * cfg.ui_scale;
            float mark_size_3d = 32.0f * cfg.ui_scale;

            Vector3 camForward = Vector3Normalize(Vector3Subtract(Camera3DParams.target, Camera3DParams.position));

            DrawSunOverlay(sunDirWorld, camForward, Camera3DParams, cfg);

            bool hide_apsis = (is_pov_mode && selected_sat != NULL && active_sat == selected_sat);
            if (cfg.show_apsides && active_sat && active_sat->is_active && !hide_apsis)
            {
                bool is_unselected = (selected_sat != NULL && active_sat != selected_sat);
                float sat_alpha = is_unselected ? unselected_fade : 1.0f;

                double t_peri_unix, t_apo_unix;
                get_apsis_times(active_sat, current_epoch, &t_peri_unix, &t_apo_unix);

                Vector3 draw_p = Vector3Scale(calculate_position(active_sat, t_peri_unix), 1.0f / DRAW_SCALE);
                Vector3 draw_a = Vector3Scale(calculate_position(active_sat, t_apo_unix), 1.0f / DRAW_SCALE);

                if (!IsOccludedByEarth(Camera3DParams.position, draw_p, draw_earth_radius))
                {
                    Vector2 sp = WorldToScreenViewport3D(draw_p, Camera3DParams);
                    DrawTexturePro(
                        periMark, (Rectangle){0, 0, periMark.width, periMark.height}, (Rectangle){sp.x, sp.y, mark_size_3d, mark_size_3d}, (Vector2){mark_size_3d / 2.f, mark_size_3d / 2.f}, 0.0f,
                        ApplyAlpha(g_theme.world.periapsis, sat_alpha)
                    );
                }
                if (!IsOccludedByEarth(Camera3DParams.position, draw_a, draw_earth_radius))
                {
                    Vector2 sp = WorldToScreenViewport3D(draw_a, Camera3DParams);
                    DrawTexturePro(
                        apoMark, (Rectangle){0, 0, apoMark.width, apoMark.height}, (Rectangle){sp.x, sp.y, mark_size_3d, mark_size_3d}, (Vector2){mark_size_3d / 2.f, mark_size_3d / 2.f}, 0.0f,
                        ApplyAlpha(g_theme.world.apoapsis, sat_alpha)
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
                        Color sCol = (selected_sat == &satellites[i]) ? g_theme.world.sat_selected : (hovered_sat == &satellites[i]) ? g_theme.world.sat_hover : g_theme.world.sat;
                        if (orbits_enabled)
                        {
                            if (active_sat == &satellites[i])
                            {
                                sCol = g_theme.world.orbit_active;
                            }
                            else
                            {
                                bool matched_favorite = false;
                                if (orbits_fav_colored)
                                {
                                    for (int k = 0; k < fav_count; k++)
                                    {
                                        if (satellites[i].norad_id_num == fav_ids[k])
                                        {
                                            sCol = MultiGroundTrackColor(k);
                                            matched_favorite = true;
                                            break;
                                        }
                                    }
                                }
                                if (!matched_favorite &&
                                    (&satellites[i] == selected_sat || orbits_dimmed))
                                {
                                    sCol = g_theme.world.orbit;
                                }
                            }
                        }
                        /* orbit color is faint for lines; keep the satellite icon itself visible */
                        if (sCol.a < g_theme.world.sat.a)
                            sCol.a = g_theme.world.sat.a;
                        sCol = ApplyAlpha(sCol, sat_alpha);
                        Vector2 sp = WorldToScreenViewport3D(draw_pos, Camera3DParams);
                        // rotate the icon so its bottom-right corner points at Earth (raylib rotation is in degrees)
                        Vector2 earthScreen = WorldToScreenViewport3D(Vector3Zero(), Camera3DParams);
                        float sat_angle = (atan2f(earthScreen.y - sp.y, earthScreen.x - sp.x) * RAD2DEG) - 45.0f;
                        DrawTexturePro(satIcon, (Rectangle){0, 0, satIcon.width, satIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, sat_angle, sCol);
                    }
                }
            }

            float h_lat_rad = home->lat * DEG2RAD, h_lon_rad = (home->lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
            Vector3 h_pos = {cosf(h_lat_rad) * cosf(h_lon_rad) * draw_earth_radius, sinf(h_lat_rad) * draw_earth_radius, -cosf(h_lat_rad) * sinf(h_lon_rad) * draw_earth_radius};
            Vector3 h_normal = Vector3Normalize(h_pos);
            Vector3 h_viewDir = Vector3Normalize(Vector3Subtract(Camera3DParams.position, h_pos));
            Vector3 h_toTarget = Vector3Subtract(h_pos, Camera3DParams.position);

            if (cfg.show_markers &&
                Vector3DotProduct(h_normal, h_viewDir) > 0.0f && Vector3DotProduct(h_toTarget, camForward) > 0.0f)
            {
                Vector2 sp = WorldToScreenViewport3D(h_pos, Camera3DParams);
                DrawTexturePro(
                    markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f, WHITE
                );
            }

            if (cfg.show_markers)
            {
                for (int i = 0; i < location_count; i++)
                {
                    if (locations[i].is_home)
                        continue; /* home is drawn separately above */
                    float lat_rad = locations[i].lat * DEG2RAD, lon_rad = (locations[i].lon + gmst_deg + cfg.earth_rotation_offset) * DEG2RAD;
                    Vector3 m_pos = {cosf(lat_rad) * cosf(lon_rad) * draw_earth_radius, sinf(lat_rad) * draw_earth_radius, -cosf(lat_rad) * sinf(lon_rad) * draw_earth_radius};
                    Vector3 normal = Vector3Normalize(m_pos);
                    Vector3 viewDir = Vector3Normalize(Vector3Subtract(Camera3DParams.position, m_pos));
                    Vector3 toTarget = Vector3Subtract(m_pos, Camera3DParams.position);

                    if (Vector3DotProduct(normal, viewDir) > 0.0f && Vector3DotProduct(toTarget, camForward) > 0.0f)
                    {
                        Vector2 sp = WorldToScreenViewport3D(m_pos, Camera3DParams);
                        DrawTexturePro(
                            markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f, WHITE
                        );
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
                    Vector2 sp = WorldToScreenViewport3D(pos, Camera3DParams);
                    DrawTexturePro(
                        markerIcon, (Rectangle){0, 0, markerIcon.width, markerIcon.height}, (Rectangle){sp.x, sp.y, m_size_3d, m_size_3d}, (Vector2){m_size_3d / 2.f, m_size_3d / 2.f}, 0.0f,
                        (Color){0, 255, 255, 255}
                    );
                }
            }
        }

        /* ui overlay rendering */
        UIContext uiCtx = {
            .current_epoch = &current_epoch,
            .time_multiplier = &time_multiplier,
            .saved_multiplier = &saved_multiplier,
            .is_auto_warping = &is_auto_warping,
            .auto_warp_target = &auto_warp_target,
            .auto_warp_initial_diff = &auto_warp_initial_diff,
            .live_follow = &live_follow,
            .is_2d_view = &is_2d_view,
            .hide_unselected = &hide_unselected,
            .picking_home = &picking_home,
            .exit_app = &exit_app,
            .is_ecliptic_frame = &is_ecliptic_frame,
            .is_pov_mode = &is_pov_mode,
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
        // darken the background behind the settings modal (cheaper than the old blur, no font-atlas bleed)
        if (LayoutSettingsOpen())
        {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                          g_theme.ui.overlay);
        }

        // apply completed async fetch results on the UI thread (safe with the render loop)
        AsyncFetchApplyResults();

        DrawGUI(&uiCtx, &cfg, customFont);

        // statistics overlay: top-left of the central view, below the nav bar
        if (cfg.show_statistics)
        {
            float stat_size = 16.0f * cfg.ui_scale;
            float pad = 8.0f * cfg.ui_scale;

            float nav_h = ImGui::GetFrameHeight();
            float x = vp_x + pad;
            float y = nav_h + pad;

            char fps_str[64];
            TextCopy(fps_str, TextFormat("FPS: %d", GetFPS()));
            DrawUIText(customFont, fps_str, x, y, stat_size, g_theme.ui.text);
            y += stat_size + 4.0f * cfg.ui_scale;

            char frame_str[64];
            TextCopy(frame_str, TextFormat("Frame time: %.2f ms", GetFrameTime() * 1000.0f));
            DrawUIText(customFont, frame_str, x, y, stat_size, g_theme.ui.text_dim);
            y += stat_size + 4.0f * cfg.ui_scale;

            char sat_str[64];
            TextCopy(sat_str, TextFormat("Satellites: %d", sat_count));
            DrawUIText(customFont, sat_str, x, y, stat_size, g_theme.ui.text_dim);
            y += stat_size + 4.0f * cfg.ui_scale;

            char time_str[128];
            TextCopy(time_str, TextFormat("Time: %s", datetime_str));
            DrawUIText(customFont, time_str, x, y, stat_size, g_theme.ui.text_dim);
        }

        // night mode: flush queued geometry, copy the resolved backbuffer into nightTex, blit back through the red shader
        if (cfg.night_mode)
        {
            int nw = GetRenderWidth();
            int nh = GetRenderHeight();
            if (nw > 0 && nh > 0)
            {
                /* (re)allocate the backbuffer copy at device-pixel size */
                if (nightTex.id == 0 || nightTex.width != nw || nightTex.height != nh)
                {
                    if (nightTex.id != 0) UnloadTexture(nightTex);

                    glGenTextures(1, &nightTex.id);
                    rlActiveTextureSlot(0);
                    glBindTexture(GL_TEXTURE_2D, nightTex.id);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                    nightTex.width = nw;
                    nightTex.height = nh;
                    nightTex.mipmaps = 1;
                    nightTex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                }

                // ensure every queued draw has landed before copying
                rlDrawRenderBatchActive();

                /* GPU copy of the backbuffer colour (resolves MSAA) */
                rlActiveTextureSlot(0);
                rlEnableTexture(nightTex.id);
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, nw, nh);

                // fullscreen quad through the red shader; negative source height flips the bottom-up GL copy
                float night_intensity = 1.0f;
                SetShaderValue(shaderNight, nightIntensityLoc, &night_intensity, SHADER_UNIFORM_FLOAT);
                BeginShaderMode(shaderNight);
                DrawTexturePro(nightTex,
                               (Rectangle){ 0.0f, 0.0f, (float)nw, (float)-nh },
                               (Rectangle){ 0.0f, 0.0f, (float)GetScreenWidth(), (float)GetScreenHeight() },
                               (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
                EndShaderMode();
            }
        }

        // Expo Mode overlay: after night mode so it stays full-colour
        ExpoModeDrawOverlay(logoTex, customFont, &cfg, (Rectangle){vp_x, vp_y, vp_w, vp_h});

        EndDrawing();
    }

    /* cleanup and save */
    UnloadTexture(logoTex);
    UnloadTexture(satIcon);
    UnloadTexture(markerIcon);
    UnloadTexture(periMark);
    UnloadTexture(apoMark);
    UnloadTexture(earthTexture);
    UnloadTexture(earthNightTexture);
    UnloadTexture(skyboxTexture);
    if (g_sunGlowTex.id != 0)
        UnloadTexture(g_sunGlowTex);
    UnloadModel(skyboxModel);
    UnloadModel(earthModel);
    UnloadShader(shader3D);
    UnloadShader(shader2D);
    UnloadShader(earthSolidShader);
    UnloadShader(shaderNight);
    if (nightTex.id != 0)
        UnloadTexture(nightTex);
    UnloadShader(shaderCloud);
    UnloadShader(shaderMoon);
    UnloadTexture(cloudTexture);
    UnloadModel(cloudModel);
    UnloadTexture(moonTexture);
    UnloadModel(moonModel);
    UnloadShader(shaderAtmosphere);
    UnloadModel(atmosphereModel);
    /* ground coverage resources */
    ClearCoverageMeshCache();
    UnloadShader(g_coverage_shaders.shader3D);
    UnloadShader(g_coverage_shaders.shader2D);
    UnloadFont(customFont);

    /* persist layout state before shutdown */
    LayoutFillPersist(&cfg.ui_layout);
    RotatorSaveSettings(&cfg); /* copy live rotator settings into cfg */
    SaveSatSelection(&cfg);    /* copy live active-satellite selection into cfg */
    cfg.show_ecliptic_frame = is_ecliptic_frame; /* copy live ecliptic-frame state into cfg */
    SaveAppConfig("settings.json", &cfg);

    SaveDataSelections();
    SaveFavorites("favorites.json"); /* persist favorite satellites */
    SimScanShutdown();
    AsyncFetchShutdown();
    RotatorShutdown();
    LogShutdown();

    CloseWindow();
    return 0;
}
