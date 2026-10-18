/*
 * tool_simulate_scan.cpp - "Imager Simulation" tab of the Transit Overview modal.
 *
 * Two modes share one equirectangular Earth map:
 *   - Whiskbroom / Pushbroom: the full AOS->LOS cross-track swath
 *   - Single-shot: camera ground footprint + photo-frame overlay at a chosen time
 *
 * All geometry is drawn with ImGui draw-list primitives over the map canvas;
 * the textured background is an ImGui::Image of a lazily loaded Earth texture.
 */

#include "tools.h"
#include "tools_settings.h"
#include "tools_common.h"
#include "ui/ui.h"
#include "ui/imgui_theme.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/propagator.h"
#include "core/theme.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

#include "ui/notifications.h"
#include "util/log.h"

#include "map_detail_data.h"

/* ---- persisted setting keys ---------------------------------------------- */

#define SIMSCAN_KEY_MODE     "simscan.mode"
#define SIMSCAN_KEY_TEXTURED "simscan.textured"
#define SIMSCAN_KEY_SWATH    "simscan.swath_deg"
#define SIMSCAN_KEY_FRAC     "simscan.time_frac"
#define SIMSCAN_KEY_ASPECT   "simscan.aspect"
#define SIMSCAN_KEY_FOCAL    "simscan.focal_mm"
#define SIMSCAN_KEY_POINTING "simscan.pointing"
#define SIMSCAN_KEY_OFFNADIR "simscan.off_nadir_deg"
#define SIMSCAN_KEY_AZIMUTH  "simscan.azimuth_deg"
#define SIMSCAN_KEY_ROLL     "simscan.roll_deg"
#define SIMSCAN_KEY_SKEW     "simscan.skew_deg"
#define SIMSCAN_KEY_VIEW     "simscan.view"
#define SIMSCAN_KEY_MARKERS  "simscan.markers"
#define SIMSCAN_KEY_SCAN_W   "simscan.scan_w" /* legacy read-only fallback */
#define SIMSCAN_KEY_SHOT_RES   "simscan.shot_res"
#define SIMSCAN_KEY_SWATH_KM   "simscan.swath_km"
#define SIMSCAN_KEY_SWATH_PX   "simscan.swath_px"
#define SIMSCAN_KEY_GEOCORRECT "simscan.geocorrect"
#define SIMSCAN_KEY_EXPORT_DIR "simscan.export_dir"

/* full-frame sensor long side, mm */
#define SIMSCAN_SENSOR_LONG_MM 36.0f


#define SIMSCAN_LINE_SWATH_PX 50

/* aspect ratios (width, height) matching the combo order below */
static const float s_aspect_rw[] = { 1.0f, 3.0f, 4.0f, 16.0f, 2.0f, 3.0f, 9.0f };
static const float s_aspect_rh[] = { 1.0f, 2.0f, 3.0f, 9.0f, 3.0f, 4.0f, 16.0f };
static const char *s_aspect_labels[] = { "1:1", "3:2", "4:3", "16:9", "2:3", "3:4", "9:16" };
#define SIMSCAN_ASPECT_COUNT ((int)(sizeof(s_aspect_labels) / sizeof(s_aspect_labels[0])))

/* ---- local math helpers -------------------------------------------------- */

/* ray P + t*d (d unit) against a sphere of radius Re; returns the near hit */
static bool RaySphere(const Vector3 &P, const Vector3 &d, float Re, Vector3 *hit)
{
    float b = Vector3DotProduct(P, d);
    float c = Vector3DotProduct(P, P) - Re * Re;
    float disc = b * b - c;
    if (disc < 0.0f)
        return false;
    float t = -b - sqrtf(disc);
    if (t < 0.0f)
        return false;
    *hit = Vector3Add(P, Vector3Scale(d, t));
    return true;
}

/* Rodrigues rotation of v about unit axis k by ang radians */
static Vector3 RotAxis(const Vector3 &v, const Vector3 &k, float ang)
{
    float c = cosf(ang), s = sinf(ang);
    Vector3 kxv = Vector3CrossProduct(k, v);
    float kdv = Vector3DotProduct(k, v);
    return Vector3Add(Vector3Add(Vector3Scale(v, c), Vector3Scale(kxv, s)),
                      Vector3Scale(k, kdv * (1.0f - c)));
}

/* orthonormal sensor frame around a boresight, rolled about the boresight */
static void CameraFrame(const Vector3 &boresight, float roll_rad, Vector3 *right, Vector3 *up)
{
    Vector3 bs = Vector3Normalize(boresight);
    Vector3 ref = { 0.0f, 1.0f, 0.0f };
    Vector3 r = Vector3CrossProduct(bs, ref);
    if (Vector3Length(r) < 1e-4f)
        r = Vector3CrossProduct(bs, (Vector3){ 1.0f, 0.0f, 0.0f });
    r = Vector3Normalize(r);
    Vector3 u = Vector3Normalize(Vector3CrossProduct(r, bs));
    if (roll_rad != 0.0f)
    {
        *right = RotAxis(r, bs, roll_rad);
        *up = RotAxis(u, bs, roll_rad);
    }
    else
    {
        *right = r;
        *up = u;
    }
}

/* ---- texture / coastline helpers ----------------------------------------- */

static Texture2D s_earth_tex = { 0 };
static bool s_earth_tex_tried = false;

static void EnsureEarthTexture(void)
{
    if (s_earth_tex_tried)
        return;
    s_earth_tex_tried = true;
    s_earth_tex = LoadTexture(ThemeAssetPath(g_theme.textures.earth));
    if (s_earth_tex.id != 0)
    {
        GenTextureMipmaps(&s_earth_tex);
        SetTextureFilter(s_earth_tex, TEXTURE_FILTER_BILINEAR);
    }
}

/* lon/lat (hundredths of a degree) to map-local pixels, centred on the map */
static Vector2 MapDetailToWorld(MapDetailPoint pt, float map_w, float map_h)
{
    return { ((float)pt.lon100 / 100.0f / 360.0f) * map_w,
             -((float)pt.lat100 / 100.0f / 180.0f) * map_h };
}

static void DrawDetailLines(ImDrawList *dl, const ImVec2 &p0, float map_w, float map_h,
                            const MapDetailPoint *pts, const MapDetailLine *lines, int line_count,
                            ImU32 col, float thickness)
{
    for (int i = 0; i < line_count; i++)
    {
        const int end = lines[i].start + lines[i].count;
        for (int p = lines[i].start; p + 1 < end; p++)
        {
            Vector2 a = MapDetailToWorld(pts[p], map_w, map_h);
            Vector2 b = MapDetailToWorld(pts[p + 1], map_w, map_h);
            if (fabsf(a.x - b.x) > map_w * 0.45f)
                continue; /* antimeridian seam */
            dl->AddLine(ImVec2(p0.x + a.x + map_w * 0.5f, p0.y + a.y + map_h * 0.5f),
                        ImVec2(p0.x + b.x + map_w * 0.5f, p0.y + b.y + map_h * 0.5f), col, thickness);
        }
    }
}

static ImU32 ColU32(Color c, float a)
{
    ImVec4 v = ThemeColor(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

/* contiguous polyline runs in normalized image fractions (u,v in [0,1]) */
using LineRuns = std::vector<std::vector<Vector2>>;

/* project each detail line into runs of consecutive visible vertices */
template <typename Proj>
static void BuildLineRuns(const MapDetailPoint *pts, const MapDetailLine *lines, int line_count,
                          Proj project, LineRuns *out)
{
    for (int i = 0; i < line_count; i++)
    {
        const int end = lines[i].start + lines[i].count;
        std::vector<Vector2> run;
        for (int p = lines[i].start; p < end; p++)
        {
            Vector2 uv;
            if (project(pts[p].lat100 / 100.0f, pts[p].lon100 / 100.0f, &uv))
                run.push_back(uv);
            else
            {
                if (run.size() >= 2)
                    out->push_back(run);
                run.clear();
            }
        }
        if (run.size() >= 2)
            out->push_back(run);
    }
}

/* ---- CPU earth imagery (image views) ------------------------------------- */

static Image s_earth_img = { 0 };
static bool s_earth_img_tried = false;
static Image s_night_img = { 0 };
static bool s_night_img_tried = false;
static const int SIMSCAN_CPU_TEX_W = 2048, SIMSCAN_CPU_TEX_H = 1024;

static void EnsureEarthImages(void)
{
    if (!s_earth_img_tried)
    {
        s_earth_img_tried = true;
        s_earth_img = LoadImage(ThemeAssetPath(g_theme.textures.earth));
        if (s_earth_img.data)
        {
            ImageFormat(&s_earth_img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            ImageResize(&s_earth_img, SIMSCAN_CPU_TEX_W, SIMSCAN_CPU_TEX_H);
        }
    }
    if (!s_night_img_tried)
    {
        s_night_img_tried = true;
        s_night_img = LoadImage(ThemeAssetPath(g_theme.textures.earth_night));
        if (s_night_img.data)
        {
            ImageFormat(&s_night_img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            ImageResize(&s_night_img, SIMSCAN_CPU_TEX_W, SIMSCAN_CPU_TEX_H);
        }
    }
}

static Color SampleImage(const Image &im, float u, float v)
{
    if (!im.data)
        return (Color){ 0, 0, 0, 255 };
    int x = (int)(u * (float)im.width);
    int y = (int)(v * (float)im.height);
    if (x < 0) x = 0;
    if (x >= im.width) x = im.width - 1;
    if (y < 0) y = 0;
    if (y >= im.height) y = im.height - 1;
    const unsigned char *p = (const unsigned char *)im.data + ((size_t)y * im.width + x) * 4;
    return (Color){ p[0], p[1], p[2], p[3] };
}

static Texture2D s_out_tex = { 0 };
static int s_out_w = 0, s_out_h = 0;
static int s_out_owner = -1; /* 0 = shot, 1 = strip */

static void UploadOutTexture(int W, int H, const unsigned char *rgba)
{
    Image out = { (void *)rgba, W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (s_out_tex.id == 0 || W != s_out_w || H != s_out_h)
    {
        if (s_out_tex.id)
            UnloadTexture(s_out_tex);
        s_out_tex = LoadTextureFromImage(out);
        SetTextureFilter(s_out_tex, TEXTURE_FILTER_BILINEAR);
        s_out_w = W;
        s_out_h = H;
    }
    else
    {
        UpdateTexture(s_out_tex, rgba);
    }
}

/* ECI ground point -> lat/lon (deg) via the same 360x180 map path as the swath */
static void GroundLL(const Vector3 &hit_km, double gmst_deg, float earth_offset, float *lat, float *lon)
{
    float lon_deg, neg_lat_deg;
    get_map_coordinates(hit_km, gmst_deg, earth_offset, 360.0f, 180.0f, &lon_deg, &neg_lat_deg);
    *lon = lon_deg;
    *lat = -neg_lat_deg;
}

/* day/night shaded RGBA for one earth hit point (images passed in: worker-safe) */
static void ShadeHit(const Vector3 &hit, double gmst, float earth_offset, const Vector3 &sun,
                     const Image *earth, const Image *night, unsigned char *out)
{
    float lat, lon;
    GroundLL(hit, gmst, earth_offset, &lat, &lon);
    float u = 0.5f + lon / 360.0f;
    float v = 0.5f - lat / 180.0f;
    Vector3 N = Vector3Normalize(hit);
    float s = Vector3DotProduct(N, sun);
    float day = (s + 0.10f) / 0.20f;
    if (day < 0.0f) day = 0.0f;
    if (day > 1.0f) day = 1.0f;
    Color dc = earth ? SampleImage(*earth, u, v) : (Color){ 0, 0, 0, 255 };
    Color nc = (night && night->data) ? SampleImage(*night, u, v) : (Color){ 0, 0, 0, 255 };
    float df = 0.08f + day;
    float nf = (1.0f - day) * 0.9f;
    out[0] = (unsigned char)fminf(255.0f, dc.r * df + nc.r * nf);
    out[1] = (unsigned char)fminf(255.0f, dc.g * df + nc.g * nf);
    out[2] = (unsigned char)fminf(255.0f, dc.b * df + nc.b * nf);
    out[3] = 255;
}

/* camera basis + FOV for a single-shot, mirroring ComputeShot exactly */
static void BuildShotCamera(const Satellite *sat, double t, float focal, int aspect,
                            int pointing, float off, float az, float roll,
                            Vector3 *P, Vector3 *boresight, Vector3 *right, Vector3 *up, float *thx, float *thy)
{
    Vector3 V;
    sat_prop_state(sat, get_unix_from_epoch(t), P, &V);
    Vector3 nadir = Vector3Negate(Vector3Normalize(*P));
    Vector3 along = Vector3Normalize(V);
    Vector3 cross = Vector3Normalize(Vector3CrossProduct(along, nadir));

    if (pointing == 1)
    {
        *boresight = Vector3Negate(Vector3Normalize(calculate_sun_position(t)));
    }
    else
    {
        float th = off * DEG2RAD, ph = az * DEG2RAD;
        Vector3 az_dir = Vector3Add(Vector3Scale(cross, cosf(ph)), Vector3Scale(along, sinf(ph)));
        *boresight = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(th)), Vector3Scale(az_dir, sinf(th))));
    }

    CameraFrame(*boresight, roll * DEG2RAD, right, up);

    float rw = s_aspect_rw[aspect], rh = s_aspect_rh[aspect];
    float sw, sh;
    if (rw >= rh)
    {
        sw = SIMSCAN_SENSOR_LONG_MM;
        sh = SIMSCAN_SENSOR_LONG_MM * rh / rw;
    }
    else
    {
        sh = SIMSCAN_SENSOR_LONG_MM;
        sw = SIMSCAN_SENSOR_LONG_MM * rw / rh;
    }
    float hFOV = 2.0f * atanf(sw / (2.0f * focal));
    float vFOV = 2.0f * atanf(sh / (2.0f * focal));
    *thx = tanf(hFOV * 0.5f);
    *thy = tanf(vFOV * 0.5f);
}

/* ---- cached whiskbroom swath --------------------------------------------- */

struct SwathCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0;
    float half_rad = -1.0f;
    float skew_rad = -999.0f;
    float map_w = 0.0f;
    float map_h = 0.0f;
    std::vector<Vector2> left, right;
    std::vector<unsigned char> ok;
};
static SwathCache s_swath;

static void ComputeSwath(SatPass *pass, AppConfig *cfg, float map_w, float map_h, float half_rad,
                         float skew_rad)
{
    s_swath.left.clear();
    s_swath.right.clear();
    s_swath.ok.clear();
    s_swath.valid = true;
    s_swath.norad = pass->sat ? pass->sat->norad_id_num : 0u;
    s_swath.aos = pass->aos_epoch;
    s_swath.half_rad = half_rad;
    s_swath.skew_rad = skew_rad;
    s_swath.map_w = map_w;
    s_swath.map_h = map_h;

    const int N = 200;
    const float half = half_rad;
    const double span = pass->los_epoch - pass->aos_epoch;

    bool have_prev = false;
    float prev_lx = 0.0f;
    for (int i = 0; i < N; i++)
    {
        double t = pass->aos_epoch + span * (double)i / (double)(N - 1);
        Vector3 P, V;
        sat_prop_state(pass->sat, get_unix_from_epoch(t), &P, &V);
        Vector3 nadir = Vector3Negate(Vector3Normalize(P));
        Vector3 cross = Vector3Normalize(Vector3CrossProduct(V, P));
        if (skew_rad != 0.0f)
        {
            Vector3 axis = Vector3Normalize(V);
            nadir = RotAxis(nadir, axis, skew_rad);
            cross = RotAxis(cross, axis, skew_rad);
        }
        double gmst = epoch_to_gmst(t);

        Vector3 dL = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(half)), Vector3Scale(cross, -sinf(half))));
        Vector3 dR = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(half)), Vector3Scale(cross, sinf(half))));
        Vector3 hitL, hitR;
        bool okL = RaySphere(P, dL, EARTH_RADIUS_KM, &hitL);
        bool okR = RaySphere(P, dR, EARTH_RADIUS_KM, &hitR);

        Vector2 pl = { 0.0f, 0.0f }, pr = { 0.0f, 0.0f };
        if (okL)
            get_map_coordinates(hitL, gmst, cfg->earth_rotation_offset, map_w, map_h, &pl.x, &pl.y);
        if (okR)
            get_map_coordinates(hitR, gmst, cfg->earth_rotation_offset, map_w, map_h, &pr.x, &pr.y);

        /* unwrap across the seam so the swath stays contiguous */
        if (okL && have_prev && fabsf(pl.x - prev_lx) > map_w * 0.45f)
            pl.x += (pl.x < prev_lx) ? map_w : -map_w;
        if (okL && okR)
        {
            while (pr.x - pl.x > map_w * 0.5f) pr.x -= map_w;
            while (pl.x - pr.x > map_w * 0.5f) pr.x += map_w;
        }

        s_swath.left.push_back(pl);
        s_swath.right.push_back(pr);
        s_swath.ok.push_back((okL && okR) ? 1u : 0u);

        if (okL)
        {
            prev_lx = pl.x;
            have_prev = true;
        }
        else
        {
            have_prev = false;
        }
    }
}

/* ---- cached single-shot footprint ---------------------------------------- */

struct ShotCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0;
    float frac = -1.0f, focal = -1.0f, off = -1.0f, az = -1.0f, roll = -1.0f;
    int aspect = -1, pointing = -1;
    float map_w = 0.0f, map_h = 0.0f;
    Vector2 corner[4];
    unsigned char corner_ok[4] = { 0, 0, 0, 0 };
    Vector2 subsat;
    unsigned char subsat_ok = 0;
    Vector2 bores;
    unsigned char bores_ok = 0;
};
static ShotCache s_shot;

static void ComputeShot(SatPass *pass, AppConfig *cfg, float map_w, float map_h,
                        float frac, int aspect, float focal, int pointing,
                        float off, float az, float roll)
{
    s_shot.valid = true;
    s_shot.norad = pass->sat ? pass->sat->norad_id_num : 0u;
    s_shot.aos = pass->aos_epoch;
    s_shot.frac = frac;
    s_shot.aspect = aspect;
    s_shot.focal = focal;
    s_shot.pointing = pointing;
    s_shot.off = off;
    s_shot.az = az;
    s_shot.roll = roll;
    s_shot.map_w = map_w;
    s_shot.map_h = map_h;

    double t = pass->aos_epoch + (double)frac * (pass->los_epoch - pass->aos_epoch);
    Vector3 P, V;
    sat_prop_state(pass->sat, get_unix_from_epoch(t), &P, &V);
    Vector3 nadir = Vector3Negate(Vector3Normalize(P));
    Vector3 along = Vector3Normalize(V);
    Vector3 cross = Vector3Normalize(Vector3CrossProduct(along, nadir));

    Vector3 boresight;
    if (pointing == 1)
    {
        boresight = Vector3Negate(Vector3Normalize(calculate_sun_position(t)));
    }
    else
    {
        float th = off * DEG2RAD, ph = az * DEG2RAD;
        Vector3 az_dir = Vector3Add(Vector3Scale(cross, cosf(ph)), Vector3Scale(along, sinf(ph)));
        boresight = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(th)), Vector3Scale(az_dir, sinf(th))));
    }

    Vector3 right, up;
    CameraFrame(boresight, roll * DEG2RAD, &right, &up);

    /* sensor size: fit the long side to the full-frame 36 mm dimension */
    float rw = s_aspect_rw[aspect], rh = s_aspect_rh[aspect];
    float sw, sh;
    if (rw >= rh)
    {
        sw = SIMSCAN_SENSOR_LONG_MM;
        sh = SIMSCAN_SENSOR_LONG_MM * rh / rw;
    }
    else
    {
        sh = SIMSCAN_SENSOR_LONG_MM;
        sw = SIMSCAN_SENSOR_LONG_MM * rw / rh;
    }
    float hFOV = 2.0f * atanf(sw / (2.0f * focal));
    float vFOV = 2.0f * atanf(sh / (2.0f * focal));
    float thx = tanf(hFOV * 0.5f), thy = tanf(vFOV * 0.5f);

    const float su[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
    const float sv[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    double gmst = epoch_to_gmst(t);

    for (int i = 0; i < 4; i++)
    {
        Vector3 d = Vector3Normalize(Vector3Add(boresight,
                    Vector3Add(Vector3Scale(right, su[i] * thx), Vector3Scale(up, sv[i] * thy))));
        Vector3 hit;
        if (RaySphere(P, d, EARTH_RADIUS_KM, &hit))
        {
            get_map_coordinates(hit, gmst, cfg->earth_rotation_offset, map_w, map_h,
                                &s_shot.corner[i].x, &s_shot.corner[i].y);
            s_shot.corner_ok[i] = 1;
        }
        else
        {
            s_shot.corner[i] = { 0.0f, 0.0f };
            s_shot.corner_ok[i] = 0;
        }
    }

    /* keep the quad contiguous around the seam */
    for (int i = 1; i < 4; i++)
    {
        while (s_shot.corner[i].x - s_shot.corner[0].x > map_w * 0.5f) s_shot.corner[i].x -= map_w;
        while (s_shot.corner[0].x - s_shot.corner[i].x > map_w * 0.5f) s_shot.corner[i].x += map_w;
    }

    Vector3 sub = Vector3Scale(Vector3Normalize(P), EARTH_RADIUS_KM);
    get_map_coordinates(sub, gmst, cfg->earth_rotation_offset, map_w, map_h, &s_shot.subsat.x, &s_shot.subsat.y);
    s_shot.subsat_ok = 1;

    Vector3 bhit;
    if (RaySphere(P, boresight, EARTH_RADIUS_KM, &bhit))
    {
        get_map_coordinates(bhit, gmst, cfg->earth_rotation_offset, map_w, map_h, &s_shot.bores.x, &s_shot.bores.y);
        s_shot.bores_ok = 1;
    }
    else
    {
        s_shot.bores_ok = 0;
    }
}

/* ---- time formatting ----------------------------------------------------- */

static void FmtDuration(double secs, char *buf, int n)
{
    int s = (int)(secs + 0.5);
    int h = s / 3600, m = (s % 3600) / 60, ss = s % 60;
    if (h > 0)
        snprintf(buf, n, "%d:%02d:%02d", h, m, ss);
    else
        snprintf(buf, n, "%02d:%02d", m, ss);
}

/* ---- image-view caches --------------------------------------------------- */

struct RowSat
{
    double t, gmst;
    Vector3 P, nadir, cross, along;
};

/* single-shot render buffer dims: sensor resolution capped to ~4 MP */
static const long SIMSCAN_SHOT_MAX_PX = 4000000;
static void ComputeShotDims(int res, int aspect, int *out_W, int *out_H)
{
    float rw = s_aspect_rw[aspect], rh = s_aspect_rh[aspect];
    int W = res;
    if (W < 64) W = 64;
    if (W > 4096) W = 4096;
    int H = (int)roundf((float)W * rh / rw);
    if (H < 32) H = 32;
    if (H > 8192) H = 8192;
    long px = (long)W * H;
    if (px > SIMSCAN_SHOT_MAX_PX)
    {
        float s = sqrtf((float)SIMSCAN_SHOT_MAX_PX / (float)px);
        W = (int)roundf(W * s);
        H = (int)roundf(H * s);
        if (W < 64) W = 64;
        if (H < 32) H = 32;
    }
    *out_W = W;
    *out_H = H;
}

struct ShotImageCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0, los = 0.0;
    float frac = -1.0f, focal = -1.0f, off = -1.0f, az = -1.0f, roll = -1.0f;
    int aspect = -1, pointing = -1;
    int res = 0;
    int W = 0, H = 0;
    bool textured = false;
    char earth_path[128] = { 0 };
    std::vector<unsigned char> px;
    Vector3 P, boresight, right, up;
    float thx = 0.0f, thy = 0.0f;
    double t = 0.0, gmst = 0.0;
};
static ShotImageCache s_shot_img;

struct ShotLineCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0, los = 0.0;
    float frac = -1.0f, focal = -1.0f, off = -1.0f, az = -1.0f, roll = -1.0f;
    int aspect = -1, pointing = -1;
    LineRuns coast, border;
};
static ShotLineCache s_shot_lines;

static bool SimScanCancelled(uint64_t gen);

static bool ComputeShotLines(ShotLineCache *lc, const ShotImageCache *c, float earth_rotation_offset, uint64_t gen)
{
    lc->valid = true;
    lc->norad = c->norad;
    lc->aos = c->aos;
    lc->los = c->los;
    lc->frac = c->frac;
    lc->focal = c->focal;
    lc->off = c->off;
    lc->az = c->az;
    lc->roll = c->roll;
    lc->aspect = c->aspect;
    lc->pointing = c->pointing;
    lc->coast.clear();
    lc->border.clear();

    bool cancelled = false;
    auto project = [&](float lat, float lon, Vector2 *out) -> bool {
        if (SimScanCancelled(gen)) { cancelled = true; return false; }
        float latr = lat * DEG2RAD;
        float lon_eci = (lon + (float)c->gmst + earth_rotation_offset) * DEG2RAD;
        Vector3 M = { EARTH_RADIUS_KM * cosf(latr) * cosf(lon_eci),
                      EARTH_RADIUS_KM * sinf(latr),
                      -EARTH_RADIUS_KM * cosf(latr) * sinf(lon_eci) };
        Vector3 v = Vector3Subtract(M, c->P);
        float xc = Vector3DotProduct(v, c->right);
        float yc = Vector3DotProduct(v, c->up);
        float zc = Vector3DotProduct(v, c->boresight);
        if (zc <= 0.0f)
            return false;
        /* M lies exactly on the sphere: test a lifted copy so float rounding of |M| cannot cull visible vertices */
        if (IsOccludedByEarth(c->P, Vector3Scale(M, 1.00002f), EARTH_RADIUS_KM))
            return false;
        float nx = (xc / zc) / c->thx;
        float ny = (yc / zc) / c->thy;
        if (fabsf(nx) > 1.0f || fabsf(ny) > 1.0f)
            return false;
        out->x = nx * 0.5f + 0.5f;
        out->y = 0.5f - ny * 0.5f;
        return true;
    };

    BuildLineRuns(MAP_COAST_POINTS, MAP_COAST_LINES, MAP_COAST_LINE_COUNT, project, &lc->coast);
    if (cancelled) return false;
    BuildLineRuns(MAP_BORDER_POINTS, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT, project, &lc->border);
    return !cancelled;
}

struct StripImageCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0, los = 0.0;
    float half_rad = -1.0f;
    float skew_rad = -999.0f;
    int W = 0, H = 0;
    bool geocorrect = false;
    std::vector<float> geo_angle;
    bool textured = false;
    char earth_path[128] = { 0 };
    std::vector<unsigned char> px;
    std::vector<RowSat> rows;
};
static StripImageCache s_strip_img;

struct StripLineCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0, los = 0.0;
    float half_rad = -1.0f;
    int W = 0, H = 0;
    bool geocorrect = false;
    LineRuns coast, border;
};
static StripLineCache s_strip_lines;

// ---- async simulate-scan worker ------------------------------------------

enum SimScanState { SIMSCAN_IDLE = 0, SIMSCAN_RUNNING, SIMSCAN_DONE };

// one unit of work: a fully self-contained snapshot (no pointers into globals)
struct SimScanJob
{
    uint64_t gen = 0;                 // generation; bumped on every submit/cancel
    int      kind = 0;                // 0 = strip image, 1 = shot image
    uint32_t norad = 0;
    double   aos = 0.0, los = 0.0;
    Satellite sat_copy;               // private copy: deep-space sgp4 mutates satrec
    float swath_km = 0.0f; int swath_px = 0; bool geocorrect = false; float half_rad = 0.0f;
    float skew_rad = 0.0f;            // whiskbroom scan-plane tilt about the along-track axis
    int   W = 0, H = 0;
    float frac = 0.0f; int aspect = 0; float focal = 0.0f; int pointing = 0;
    float off = 0.0f, az = 0.0f, roll = 0.0f; int shot_res = 0;
    bool  textured = false;
    float earth_rotation_offset = 0.0f;
    Color bg = { 0, 0, 0, 255 };
    const Image *earth = nullptr;     // read-only, owned by the UI thread
    const Image *night = nullptr;
    char  earth_path[128] = { 0 };    // theme texture path (cache identity)
    int marker_count = 0;
    struct { float lat, lon; int kind; } markers[MAX_LOCATIONS + 1];
};

// completed work handed back to the UI thread
struct SimScanResult
{
    uint64_t gen = 0;
    int      kind = 0;
    StripImageCache strip_img;
    StripLineCache  strip_lines;
    ShotImageCache  shot_img;
    ShotLineCache   shot_lines;
    std::vector<ImVec2> marker_pts;   // normalized [0,1] screen positions
    std::vector<int>    marker_kinds;
};

// cached marker overlays (populated on commit, drawn each frame)
static std::vector<ImVec2> s_shot_markers;
static std::vector<int>    s_shot_marker_kinds;
static std::vector<ImVec2> s_strip_markers;
static std::vector<int>    s_strip_marker_kinds;

// cached strip dimensions (removes 200 SGP4 calls/frame)
struct StripDimsCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0, los = 0.0;
    float swath_km = -1.0f;
    int swath_px = 0;
    int W = 0, H = 0;
};
static StripDimsCache s_strip_dims;

// cached full (horizon-to-horizon) pass window for the selected pass
struct FullPassCache
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0.0, los = 0.0;      // trimmed window (cache key)
    double full_aos = 0.0, full_los = 0.0;
};
static FullPassCache s_full_pass;

static std::thread             s_sim_thread;
static std::mutex              s_sim_mtx;
static std::condition_variable s_sim_cv;
static std::atomic<bool>       s_sim_shutdown{false};
static std::atomic<bool>       s_sim_running{false};
static std::atomic<uint64_t>   s_sim_gen{0};
static std::atomic<float>      s_sim_progress{0.0f};
static std::atomic<int>        s_sim_state{SIMSCAN_IDLE};
static SimScanJob              s_sim_pending;
static bool                    s_sim_has_pending = false;
static SimScanResult           s_sim_done;
static bool                    s_sim_has_done = false;
static uint64_t                s_sim_req_key = 0;   // dedup: last dispatched request

static bool SimScanCancelled(uint64_t gen)
{
    return s_sim_shutdown.load() || gen != s_sim_gen.load();
}

static bool ComputeStripJob(const SimScanJob *job, SimScanResult *res);
static bool ComputeShotJob(const SimScanJob *job, SimScanResult *res);

// FNV-1a over raw bytes
static uint64_t SimScanHashBytes(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++)
    {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t SimScanLocHash(uint64_t h)
{
    h = SimScanHashBytes(h, &location_count, sizeof(location_count));
    for (int i = 0; i < location_count && i < MAX_LOCATIONS; i++)
    {
        h = SimScanHashBytes(h, &locations[i].lat, sizeof(float));
        h = SimScanHashBytes(h, &locations[i].lon, sizeof(float));
        h = SimScanHashBytes(h, &locations[i].is_home, sizeof(bool));
    }
    return h;
}

// this is just magic actually
static int64_t SimScanQuantEpoch(double e)
{
    return (int64_t)llround(e / 1e-3);
}

static uint64_t SimScanStripKey(uint32_t norad, double aos, double los, float half_rad,
                                float skew_rad, bool geocorrect, int W, int H, bool textured,
                                const char *earth_path)
{
    uint64_t h = 1469598103934665603ULL;
    h = SimScanHashBytes(h, &norad, sizeof(norad));
    int64_t aos_q = SimScanQuantEpoch(aos);
    int64_t los_q = SimScanQuantEpoch(los);
    h = SimScanHashBytes(h, &aos_q, sizeof(aos_q));
    h = SimScanHashBytes(h, &los_q, sizeof(los_q));
    h = SimScanHashBytes(h, &half_rad, sizeof(half_rad));
    h = SimScanHashBytes(h, &skew_rad, sizeof(skew_rad));
    h = SimScanHashBytes(h, &geocorrect, sizeof(geocorrect));
    h = SimScanHashBytes(h, &W, sizeof(W));
    h = SimScanHashBytes(h, &H, sizeof(H));
    h = SimScanHashBytes(h, &textured, sizeof(textured));
    if (earth_path) h = SimScanHashBytes(h, earth_path, strlen(earth_path));
    return SimScanLocHash(h);
}

static uint64_t SimScanShotKey(uint32_t norad, double aos, double los, float frac, int aspect,
                               float focal, int pointing, float off, float az, float roll,
                               int res, int W, int H, bool textured, const char *earth_path)
{
    uint64_t h = 1469598103934665603ULL;
    h = SimScanHashBytes(h, &norad, sizeof(norad));
    int64_t aos_q = SimScanQuantEpoch(aos);
    int64_t los_q = SimScanQuantEpoch(los);
    h = SimScanHashBytes(h, &aos_q, sizeof(aos_q));
    h = SimScanHashBytes(h, &los_q, sizeof(los_q));
    h = SimScanHashBytes(h, &frac, sizeof(frac));
    h = SimScanHashBytes(h, &aspect, sizeof(aspect));
    h = SimScanHashBytes(h, &focal, sizeof(focal));
    h = SimScanHashBytes(h, &pointing, sizeof(pointing));
    h = SimScanHashBytes(h, &off, sizeof(off));
    h = SimScanHashBytes(h, &az, sizeof(az));
    h = SimScanHashBytes(h, &roll, sizeof(roll));
    h = SimScanHashBytes(h, &res, sizeof(res));
    h = SimScanHashBytes(h, &W, sizeof(W));
    h = SimScanHashBytes(h, &H, sizeof(H));
    h = SimScanHashBytes(h, &textured, sizeof(textured));
    if (earth_path) h = SimScanHashBytes(h, earth_path, strlen(earth_path));
    return SimScanLocHash(h);
}

// worker loop: single latest-wins job slot, single completion slot
static void simscan_worker_main(void)
{
    while (true)
    {
        SimScanJob job;
        {
            std::unique_lock<std::mutex> lk(s_sim_mtx);
            s_sim_cv.wait(lk, [] { return s_sim_shutdown.load() || s_sim_has_pending; });
            if (s_sim_shutdown.load() && !s_sim_has_pending)
                break;
            job = s_sim_pending;
            s_sim_has_pending = false;
        }
        if (job.gen != s_sim_gen.load())
            continue; // superseded before start

        SimScanResult res;
        res.gen = job.gen;
        res.kind = job.kind;
        bool ok = (job.kind == 0) ? ComputeStripJob(&job, &res) : ComputeShotJob(&job, &res);
        if (!ok)
            continue; // cancelled mid-compute

        {
            std::lock_guard<std::mutex> lk(s_sim_mtx);
            if (job.gen != s_sim_gen.load())
                continue; // superseded during compute
            s_sim_done = std::move(res);
            s_sim_has_done = true;
            s_sim_state.store(SIMSCAN_DONE);
        }
    }
}

// ---- public API ----------------------------------------------------------

void SimScanInit(void)
{
    if (s_sim_running.load())
        return;
    s_sim_shutdown = false;
    s_sim_running = true;
    s_sim_thread = std::thread(simscan_worker_main);
}

void SimScanShutdown(void)
{
    if (!s_sim_running.load())
        return;
    {
        std::lock_guard<std::mutex> lk(s_sim_mtx);
        s_sim_shutdown = true;
    }
    s_sim_cv.notify_all();
    if (s_sim_thread.joinable())
        s_sim_thread.join();
    s_sim_running = false;
}

void SimScanSubmit(const SimScanJob &job)
{
    if (!s_sim_running.load())
        SimScanInit();
    {
        std::lock_guard<std::mutex> lk(s_sim_mtx);
        uint64_t g = s_sim_gen.fetch_add(1) + 1;
        s_sim_pending = job;
        s_sim_pending.gen = g;
        s_sim_has_pending = true;
        s_sim_has_done = false;
        s_sim_state.store(SIMSCAN_RUNNING);
        s_sim_progress.store(0.0f);
    }
    s_sim_cv.notify_one();
}

void SimScanCancel(void)
{
    std::lock_guard<std::mutex> lk(s_sim_mtx);
    s_sim_gen.fetch_add(1);
    s_sim_has_pending = false;
    s_sim_has_done = false;
    s_sim_state.store(SIMSCAN_IDLE);
    s_sim_progress.store(0.0f);
    s_sim_req_key = 0;
}

bool SimScanPoll(SimScanResult *out)
{
    if (!out)
        return false;
    std::lock_guard<std::mutex> lk(s_sim_mtx);
    if (!s_sim_has_done)
        return false;
    *out = std::move(s_sim_done);
    s_sim_has_done = false;
    s_sim_state.store(SIMSCAN_IDLE);
    return true;
}

bool SimScanIsRunning(void)
{
    return s_sim_state.load() == SIMSCAN_RUNNING;
}

float SimScanProgress(void)
{
    return s_sim_progress.load();
}

// ---- pure, cancellable compute functions ---------------------------------

// project the location + sub-satellite markers into normalized shot coords
static bool ProjectShotMarkers(const SimScanJob *job, const ShotImageCache *c,
                               std::vector<ImVec2> *pts, std::vector<int> *kinds)
{
    pts->clear();
    kinds->clear();
    const float ero = job->earth_rotation_offset;

    struct M { float lat, lon; int kind; };
    std::vector<M> mk;
    mk.reserve((size_t)job->marker_count + 1);
    for (int i = 0; i < job->marker_count; i++)
        mk.push_back({ job->markers[i].lat, job->markers[i].lon, job->markers[i].kind });

    Vector3 Psub, Vsub;
    sat_prop_state(&job->sat_copy, get_unix_from_epoch(c->t), &Psub, &Vsub);
    Vector3 sub = Vector3Scale(Vector3Normalize(Psub), EARTH_RADIUS_KM);
    float sub_lat, sub_lon;
    GroundLL(sub, epoch_to_gmst(c->t), ero, &sub_lat, &sub_lon);
    mk.push_back({ sub_lat, sub_lon, 2 });

    for (const M &m : mk)
    {
        if (SimScanCancelled(job->gen))
            return false;
        float latr = m.lat * DEG2RAD;
        float lon_eci = (m.lon + (float)c->gmst + ero) * DEG2RAD;
        Vector3 Mv = { EARTH_RADIUS_KM * cosf(latr) * cosf(lon_eci),
                       EARTH_RADIUS_KM * sinf(latr),
                       -EARTH_RADIUS_KM * cosf(latr) * sinf(lon_eci) };
        Vector3 v = Vector3Subtract(Mv, c->P);
        float xc = Vector3DotProduct(v, c->right);
        float yc = Vector3DotProduct(v, c->up);
        float zc = Vector3DotProduct(v, c->boresight);
        if (zc <= 0.0f)
            continue;
        if (IsOccludedByEarth(c->P, Mv, EARTH_RADIUS_KM))
            continue;
        float nx = (xc / zc) / c->thx;
        float ny = (yc / zc) / c->thy;
        if (fabsf(nx) > 1.0f || fabsf(ny) > 1.0f)
            continue;
        pts->push_back(ImVec2(nx * 0.5f + 0.5f, 0.5f - ny * 0.5f));
        kinds->push_back(m.kind);
    }
    return true;
}

// port of RenderShotImage: pure CPU, cancellable, progress-reporting
static bool ComputeShotJob(const SimScanJob *job, SimScanResult *res)
{
    ShotImageCache &c = res->shot_img;
    c.norad = job->norad;
    c.aos = job->aos;
    c.los = job->los;
    c.frac = job->frac;
    c.aspect = job->aspect;
    c.focal = job->focal;
    c.pointing = job->pointing;
    c.off = job->off;
    c.az = job->az;
    c.roll = job->roll;
    c.res = job->shot_res;
    c.W = job->W;
    c.H = job->H;
    c.textured = job->textured;
    snprintf(c.earth_path, sizeof(c.earth_path), "%s", job->earth_path);

    double t = job->aos + (double)c.frac * (job->los - job->aos);
    Vector3 P, boresight, right, up;
    float thx, thy;
    BuildShotCamera(&job->sat_copy, t, c.focal, c.aspect, c.pointing, c.off, c.az, c.roll,
                    &P, &boresight, &right, &up, &thx, &thy);
    c.P = P;
    c.boresight = boresight;
    c.right = right;
    c.up = up;
    c.thx = thx;
    c.thy = thy;
    c.t = t;
    c.gmst = epoch_to_gmst(t);

    if (c.textured)
    {
        Vector3 sun = calculate_sun_position(t);
        int W = c.W, H = c.H;
        c.px.assign((size_t)W * H * 4, 0);
        Color bg = job->bg;
        for (int row = 0; row < H; row++)
        {
            if ((row & 15) == 0 && (job->gen != s_sim_gen.load() || s_sim_shutdown.load()))
                return false;
            float sy = 1.0f - 2.0f * ((row + 0.5f) / (float)H);
            for (int col = 0; col < W; col++)
            {
                float sx = 2.0f * ((col + 0.5f) / (float)W) - 1.0f;
                Vector3 d = Vector3Normalize(Vector3Add(boresight,
                            Vector3Add(Vector3Scale(right, sx * thx), Vector3Scale(up, sy * thy))));
                unsigned char *o = &c.px[((size_t)row * W + col) * 4];
                Vector3 hit;
                if (RaySphere(P, d, EARTH_RADIUS_KM, &hit))
                    ShadeHit(hit, c.gmst, job->earth_rotation_offset, sun, job->earth, job->night, o);
                else
                {
                    o[0] = bg.r; o[1] = bg.g; o[2] = bg.b; o[3] = 255;
                }
            }
            s_sim_progress.store(0.9f * (float)(row + 1) / (float)H);
        }
    }
    if (!ComputeShotLines(&res->shot_lines, &c, job->earth_rotation_offset, job->gen))
        return false;
    c.valid = true;
    if (!ProjectShotMarkers(job, &c, &res->marker_pts, &res->marker_kinds))
        return false;
    s_sim_progress.store(1.0f);
    return true;
}

/* derived cross-track FOV for a requested ground swath width (mid-pass basis) */
struct SwathFov
{
    bool valid = false;
    uint32_t norad = 0;
    double aos = 0, los = 0;
    float km = -1;
    float half_rad = 0, visible_km = 0;
    bool capped = false;
};
static SwathFov s_fov;

static float GroundSwathKm(const Vector3 &P, const Vector3 &nadir, const Vector3 &cross, float half)
{
    Vector3 dL = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(half)), Vector3Scale(cross, -sinf(half))));
    Vector3 dR = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(half)), Vector3Scale(cross, sinf(half))));
    Vector3 hL, hR;
    if (!RaySphere(P, dL, EARTH_RADIUS_KM, &hL) || !RaySphere(P, dR, EARTH_RADIUS_KM, &hR))
        return 0.0f;
    return Vector3Distance(hL, hR);
}

static void ComputeSwathFov(const SatPass *pass, float swath_km, SwathFov *out)
{
    out->norad = pass->sat ? pass->sat->norad_id_num : 0u;
    out->aos = pass->aos_epoch;
    out->los = pass->los_epoch;
    out->km = swath_km;
    double t_mid = pass->aos_epoch + 0.5 * (pass->los_epoch - pass->aos_epoch);
    Vector3 P, V;
    sat_prop_state(pass->sat, get_unix_from_epoch(t_mid), &P, &V);
    Vector3 nadir = Vector3Negate(Vector3Normalize(P));
    Vector3 cross = Vector3Normalize(Vector3CrossProduct(V, P));
    float half_max = asinf(EARTH_RADIUS_KM / Vector3Length(P));
    half_max *= 0.9995f;
    out->visible_km = GroundSwathKm(P, nadir, cross, half_max);
    if (out->visible_km <= swath_km)
    {
        out->half_rad = half_max;
        out->capped = true;
    }
    else
    {
        float lo = 0, hi = half_max;
        for (int i = 0; i < 60; i++)
        {
            float mid = 0.5f * (lo + hi);
            if (GroundSwathKm(P, nadir, cross, mid) < swath_km)
                lo = mid;
            else
                hi = mid;
        }
        out->half_rad = 0.5f * (lo + hi);
        out->capped = false;
    }
    out->valid = true;
}

/* strip dims from explicit swath width (km) + cross-track pixel count */
static void ComputeStripDims(const Satellite *sat, double aos, double los,
                             float swath_km, int swath_px, int *out_W, int *out_H)
{
    int W = swath_px;
    if (W < 64) W = 64;
    if (W > 4096) W = 4096;
    float GSD_km = swath_km / (float)swath_px;
    if (GSD_km < 1e-6f)
        GSD_km = 1e-6f;

    /* along-track ground-track length */
    const int N = 200;
    float L_km = 0.0f;
    Vector3 prev_sub = { 0.0f, 0.0f, 0.0f };
    bool have_prev = false;
    for (int i = 0; i < N; i++)
    {
        double t = aos + (los - aos) * (double)i / (double)(N - 1);
        Vector3 Pi, Vi;
        sat_prop_state(sat, get_unix_from_epoch(t), &Pi, &Vi);
        Vector3 sub = Vector3Scale(Vector3Normalize(Pi), EARTH_RADIUS_KM);
        if (have_prev)
            L_km += Vector3Distance(sub, prev_sub);
        prev_sub = sub;
        have_prev = true;
    }

    int H = (int)roundf(L_km / GSD_km);
    if (H < 32) H = 32;
    if (H > 8192) H = 8192;

    /* cap total pixels ~4 MP, scaling both dims proportionally to keep the aspect */
    long px = (long)W * H;
    if (px > 4000000)
    {
        float s = sqrtf(4000000.0f / (float)px);
        W = (int)roundf((float)W * s);
        H = (int)roundf((float)H * s);
        if (W < 64) W = 64;
        if (H < 32) H = 32;
    }

    *out_W = W;
    *out_H = H;
}

// cached wrapper: avoids 200 SGP4 calls/frame when nothing changed
static void ComputeStripDimsCached(const Satellite *sat, uint32_t norad, double aos, double los,
                                   float swath_km, int swath_px, int *out_W, int *out_H)
{
    if (!s_strip_dims.valid || s_strip_dims.norad != norad ||
        fabs(s_strip_dims.aos - aos) > 1e-6 || fabs(s_strip_dims.los - los) > 1e-6 ||
        s_strip_dims.swath_km != swath_km || s_strip_dims.swath_px != swath_px)
    {
        ComputeStripDims(sat, aos, los, swath_km, swath_px, &s_strip_dims.W, &s_strip_dims.H);
        s_strip_dims.valid = true;
        s_strip_dims.norad = norad;
        s_strip_dims.aos = aos;
        s_strip_dims.los = los;
        s_strip_dims.swath_km = swath_km;
        s_strip_dims.swath_px = swath_px;
    }
    *out_W = s_strip_dims.W;
    *out_H = s_strip_dims.H;
}

/* col -> cross-track angle table, spacing columns by equal ground distance */
static bool BuildGeoAngles(const Satellite *sat, double aos, double los, float half, int W,
                           std::vector<float> *out, bool *out_capped)
{
    const int M = 512;
    double t_mid = aos + 0.5 * (los - aos);
    Vector3 P, V;
    sat_prop_state(sat, get_unix_from_epoch(t_mid), &P, &V);
    Vector3 nadir = Vector3Negate(Vector3Normalize(P));
    Vector3 cross = Vector3Normalize(Vector3CrossProduct(V, P));
    std::vector<float> sang(M), sdist(M);
    Vector3 prev;
    bool have = false, any_miss = false;
    for (int i = 0; i < M; i++)
    {
        float a = -half + (float)i / (float)(M - 1) * 2.0f * half;
        Vector3 d = Vector3Normalize(Vector3Add(Vector3Scale(nadir, cosf(a)), Vector3Scale(cross, sinf(a))));
        Vector3 hit;
        sang[i] = a;
        if (RaySphere(P, d, EARTH_RADIUS_KM, &hit))
        {
            sdist[i] = have ? sdist[i - 1] + Vector3Distance(hit, prev) : 0.0f;
            prev = hit;
            have = true;
        }
        else
        {
            sdist[i] = have ? sdist[i - 1] : 0.0f;
            any_miss = true;
        }
    }
    float total = sdist[M - 1];
    out->resize(W);
    if (total < 1e-3f)
    {
        for (int i = 0; i < W; i++)
            (*out)[i] = -half + (float)i / (float)(W - 1) * 2.0f * half;
        *out_capped = true;
        return false;
    }
    int j = 0;
    for (int i = 0; i < W; i++)
    {
        float target = (float)i / (float)(W - 1) * total;
        while (j + 1 < M - 1 && sdist[j + 1] < target)
            j++;
        float den = sdist[j + 1] - sdist[j];
        float f = (den > 1e-9f) ? (target - sdist[j]) / den : 0.0f;
        (*out)[i] = sang[j] + f * (sang[j + 1] - sang[j]);
    }
    *out_capped = any_miss;
    return !any_miss;
}

/* fractional table index for a cross-track angle (binary search) */
static float GeoTableIndex(const std::vector<float> &ang, float a)
{
    int n = (int)ang.size();
    if (n < 2)
        return 0.0f;
    if (a <= ang[0])
        return 0.0f;
    if (a >= ang[n - 1])
        return (float)(n - 1);
    int lo = 0, hi = n - 1;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        if (ang[mid] <= a)
            lo = mid;
        else
            hi = mid;
    }
    float den = ang[hi] - ang[lo];
    float f = (den > 1e-9f) ? (a - ang[lo]) / den : 0.0f;
    return (float)lo + f;
}

static bool ComputeStripLines(StripLineCache *lc, const StripImageCache *c, float earth_rotation_offset, uint64_t gen)
{
    lc->valid = true;
    lc->norad = c->norad;
    lc->aos = c->aos;
    lc->los = c->los;
    lc->half_rad = c->half_rad;
    lc->W = c->W;
    lc->H = c->H;
    lc->geocorrect = c->geocorrect;
    lc->coast.clear();
    lc->border.clear();

    const int Hr = (int)c->rows.size();
    if (Hr < 2)
        return true;
    const float half = c->half_rad;
    const bool geo = c->geocorrect && c->geo_angle.size() >= 2;
    const std::vector<RowSat> &rows = c->rows;

    bool cancelled = false;
    auto project = [&](float lat, float lon, Vector2 *out) -> bool {
        float latr = lat * DEG2RAD;
        float clat = cosf(latr), slat = sinf(latr);
        bool have_prev = false;
        float prev_b = 0.0f, prev_a = 0.0f, prev_dn = 0.0f;
        for (int r = 0; r < Hr; r++)
        {
            if ((r & 15) == 0 && SimScanCancelled(gen))
            {
                cancelled = true;
                return false;
            }
            const RowSat &rs = rows[r];
            float lon_eci = (lon + (float)rs.gmst + earth_rotation_offset) * DEG2RAD;
            Vector3 M = { EARTH_RADIUS_KM * clat * cosf(lon_eci),
                          EARTH_RADIUS_KM * slat,
                          -EARTH_RADIUS_KM * clat * sinf(lon_eci) };
            Vector3 d = Vector3Normalize(Vector3Subtract(M, rs.P));
            float dn = Vector3DotProduct(d, rs.nadir);
            float b = atan2f(Vector3DotProduct(d, rs.along), dn);
            float a = atan2f(Vector3DotProduct(d, rs.cross), dn);
            if (have_prev)
            {
                /* keep b continuous across the atan2 branch cut */
                while (b - prev_b > PI) b -= 2.0f * PI;
                while (b - prev_b < -PI) b += 2.0f * PI;
                if ((prev_b <= 0.0f && b >= 0.0f) || (prev_b >= 0.0f && b <= 0.0f))
                {
                    float denom = b - prev_b;
                    float f = (denom != 0.0f) ? (-prev_b / denom) : 0.0f;
                    float a_star = prev_a + f * (a - prev_a);
                    float dn_star = prev_dn + f * (dn - prev_dn);
                    float r_star = (float)(r - 1) + f;
                    if (dn_star > 0.0f && fabsf(a_star) <= half)
                    {
                        int ri = (int)(r_star + 0.5f);
                        if (ri < 0) ri = 0;
                        if (ri >= Hr) ri = Hr - 1;
                        const RowSat &rs2 = rows[ri];
                        float lon_eci2 = (lon + (float)rs2.gmst + earth_rotation_offset) * DEG2RAD;
                        Vector3 M2 = { EARTH_RADIUS_KM * clat * cosf(lon_eci2),
                                       EARTH_RADIUS_KM * slat,
                                       -EARTH_RADIUS_KM * clat * sinf(lon_eci2) };
                        if (!IsOccludedByEarth(rs2.P, Vector3Scale(M2, 1.00002f), EARTH_RADIUS_KM))
                        {
                            float u = geo ? GeoTableIndex(c->geo_angle, a_star) /
                                                (float)(c->geo_angle.size() - 1)
                                          : (a_star + half) / (2.0f * half);
                            out->x = u;
                            out->y = r_star / (float)(Hr - 1);
                            return true;
                        }
                    }
                    return false;
                }
            }
            prev_b = b;
            prev_a = a;
            prev_dn = dn;
            have_prev = true;
        }
        return false;
    };

    BuildLineRuns(MAP_COAST_POINTS, MAP_COAST_LINES, MAP_COAST_LINE_COUNT, project, &lc->coast);
    if (cancelled) return false;
    BuildLineRuns(MAP_BORDER_POINTS, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT, project, &lc->border);
    return !cancelled;
}

// project the location + sub-satellite markers into normalized strip coords
static bool ProjectStripMarkers(const SimScanJob *job, const StripImageCache *c,
                                std::vector<ImVec2> *pts, std::vector<int> *kinds)
{
    pts->clear();
    kinds->clear();
    if (c->rows.empty())
        return true;
    const std::vector<RowSat> &rows = c->rows;
    const int Hr = (int)rows.size();
    const float half = c->half_rad;
    const float ero = job->earth_rotation_offset;
    const bool geo = c->geocorrect && c->geo_angle.size() >= 2;

    struct M { float lat, lon; int kind; };
    std::vector<M> mk;
    mk.reserve((size_t)job->marker_count + 1);
    for (int i = 0; i < job->marker_count; i++)
        mk.push_back({ job->markers[i].lat, job->markers[i].lon, job->markers[i].kind });

    double t_sub = rows[Hr / 2].t;
    Vector3 Psub, Vsub;
    sat_prop_state(&job->sat_copy, get_unix_from_epoch(t_sub), &Psub, &Vsub);
    Vector3 sub = Vector3Scale(Vector3Normalize(Psub), EARTH_RADIUS_KM);
    float sub_lat, sub_lon;
    GroundLL(sub, epoch_to_gmst(t_sub), ero, &sub_lat, &sub_lon);
    mk.push_back({ sub_lat, sub_lon, 2 });

    for (const M &m : mk)
    {
        if (SimScanCancelled(job->gen))
            return false;
        float latr = m.lat * DEG2RAD;
        float clat = cosf(latr), slat = sinf(latr);
        bool have_prev = false;
        float prev_b = 0.0f, prev_a = 0.0f;
        for (int r = 0; r < Hr; r++)
        {
            const RowSat &rs = rows[r];
            float lon_eci = (m.lon + (float)rs.gmst + ero) * DEG2RAD;
            Vector3 Mv = { EARTH_RADIUS_KM * clat * cosf(lon_eci),
                           EARTH_RADIUS_KM * slat,
                           -EARTH_RADIUS_KM * clat * sinf(lon_eci) };
            Vector3 d = Vector3Normalize(Vector3Subtract(Mv, rs.P));
            float dn = Vector3DotProduct(d, rs.nadir);
            float b = atan2f(Vector3DotProduct(d, rs.along), dn);
            float a = atan2f(Vector3DotProduct(d, rs.cross), dn);
            if (have_prev && ((prev_b <= 0.0f && b >= 0.0f) || (prev_b >= 0.0f && b <= 0.0f)))
            {
                float denom = b - prev_b;
                float f = (denom != 0.0f) ? (-prev_b / denom) : 0.0f;
                float a_star = prev_a + f * (a - prev_a);
                float r_star = (float)(r - 1) + f;
                if (fabsf(a_star) <= half)
                {
                    int ri = (int)(r_star + 0.5f);
                    if (ri < 0) ri = 0;
                    if (ri >= Hr) ri = Hr - 1;
                    const RowSat &rs2 = rows[ri];
                    float lon_eci2 = (m.lon + (float)rs2.gmst + ero) * DEG2RAD;
                    Vector3 M2 = { EARTH_RADIUS_KM * clat * cosf(lon_eci2),
                                   EARTH_RADIUS_KM * slat,
                                   -EARTH_RADIUS_KM * clat * sinf(lon_eci2) };
                    if (!IsOccludedByEarth(rs2.P, M2, EARTH_RADIUS_KM))
                    {
                        float u = geo ? GeoTableIndex(c->geo_angle, a_star) /
                                            (float)(c->geo_angle.size() - 1)
                                      : (a_star + half) / (2.0f * half);
                        float v = r_star / (float)(Hr - 1);
                        pts->push_back(ImVec2(u, v));
                        kinds->push_back(m.kind);
                    }
                }
                break;
            }
            prev_b = b;
            prev_a = a;
            have_prev = true;
        }
    }
    return true;
}

// port of RenderStripImage: pure CPU, cancellable, progress-reporting
static bool ComputeStripJob(const SimScanJob *job, SimScanResult *res)
{
    StripImageCache &c = res->strip_img;
    c.norad = job->norad;
    c.aos = job->aos;
    c.los = job->los;
    c.half_rad = job->half_rad;
    c.skew_rad = job->skew_rad;
    c.W = job->W;
    c.H = job->H;
    c.geocorrect = job->geocorrect;
    c.textured = job->textured;
    snprintf(c.earth_path, sizeof(c.earth_path), "%s", job->earth_path);

    int W = c.W, H = c.H;
    float half = c.half_rad;
    c.rows.resize(H);
    for (int r = 0; r < H; r++)
    {
        if ((r & 15) == 0 && (job->gen != s_sim_gen.load() || s_sim_shutdown.load()))
            return false;
        double t = job->aos + (double)r / (double)(H - 1) * (job->los - job->aos);
        Vector3 P, V;
        sat_prop_state(&job->sat_copy, get_unix_from_epoch(t), &P, &V);
        RowSat &rs = c.rows[r];
        rs.t = t;
        rs.P = P;
        rs.nadir = Vector3Negate(Vector3Normalize(P));
        rs.cross = Vector3Normalize(Vector3CrossProduct(P, V));
        rs.along = Vector3Normalize(Vector3CrossProduct(P, rs.cross));
        if (job->skew_rad != 0.0f)
        {
            /* tilt the scan plane about the along-track (velocity) axis: the plane
               stays perpendicular to `along`, so only the column angle changes */
            Vector3 axis = Vector3Normalize(V);
            rs.nadir = RotAxis(rs.nadir, axis, job->skew_rad);
            rs.cross = RotAxis(rs.cross, axis, job->skew_rad);
        }
        rs.gmst = epoch_to_gmst(t);
        s_sim_progress.store(0.2f * (float)(r + 1) / (float)H);
    }

    /* the geo table is built in the untilted scan plane and is only valid there */
    if (c.geocorrect && W >= 2 && job->skew_rad == 0.0f)
    {
        bool capped = false;
        BuildGeoAngles(&job->sat_copy, job->aos, job->los, half, W, &c.geo_angle, &capped);
    }
    else
    {
        c.geo_angle.clear();
    }

    if (c.textured)
    {
        c.px.assign((size_t)W * H * 4, 0);
        Color bg = job->bg;
        for (int r = 0; r < H; r++)
        {
            if ((r & 15) == 0 && (job->gen != s_sim_gen.load() || s_sim_shutdown.load()))
                return false;
            const RowSat &rs = c.rows[r];
            Vector3 sun = calculate_sun_position(rs.t);
            for (int col = 0; col < W; col++)
            {
                float a = (c.geocorrect && c.geo_angle.size() == (size_t)W)
                              ? c.geo_angle[col]
                              : -half + ((float)col / (float)(W - 1)) * 2.0f * half;
                Vector3 d = Vector3Normalize(Vector3Add(Vector3Scale(rs.nadir, cosf(a)),
                                                        Vector3Scale(rs.cross, sinf(a))));
                unsigned char *o = &c.px[((size_t)r * W + col) * 4];
                Vector3 hit;
                if (RaySphere(rs.P, d, EARTH_RADIUS_KM, &hit))
                    ShadeHit(hit, rs.gmst, job->earth_rotation_offset, sun, job->earth, job->night, o);
                else
                {
                    o[0] = bg.r; o[1] = bg.g; o[2] = bg.b; o[3] = 255;
                }
            }
            s_sim_progress.store(0.2f + 0.7f * (float)(r + 1) / (float)H);
        }
    }
    if (!ComputeStripLines(&res->strip_lines, &c, job->earth_rotation_offset, job->gen))
        return false;
    c.valid = true;
    if (!ProjectStripMarkers(job, &c, &res->marker_pts, &res->marker_kinds))
        return false;
    s_sim_progress.store(1.0f);
    return true;
}

/* ---- line-only image rendering ------------------------------------------- */

static void StrokeRuns(ImDrawList *dl, const ImVec2 &p0, float W, float H,
                       const LineRuns &runs, ImU32 col, float thick)
{
    std::vector<ImVec2> buf;
    for (const std::vector<Vector2> &run : runs)
    {
        if (run.size() < 2)
            continue;
        buf.resize(run.size());
        for (size_t i = 0; i < run.size(); i++)
            buf[i] = ImVec2(p0.x + run[i].x * W, p0.y + run[i].y * H);
        dl->AddPolyline(buf.data(), (int)buf.size(), col, ImDrawFlags_None, thick);
    }
}

static void DrawShotLines(ImDrawList *dl, const ImVec2 &p0, float W, float H, const ShotLineCache *lc)
{
    const float thick = UIPx(1.0f);
    StrokeRuns(dl, p0, W, H, lc->coast, ColU32(g_theme.ui.text, 0.55f), thick);
    StrokeRuns(dl, p0, W, H, lc->border, ColU32(g_theme.ui.text, 0.35f), thick);
}

static void DrawStripLines(ImDrawList *dl, const ImVec2 &p0, float W, float H, const StripLineCache *lc)
{
    const float thick = UIPx(1.0f);
    StrokeRuns(dl, p0, W, H, lc->coast, ColU32(g_theme.ui.text, 0.55f), thick);
    StrokeRuns(dl, p0, W, H, lc->border, ColU32(g_theme.ui.text, 0.35f), thick);
}

/* ---- marker overlay ------------------------------------------------------ */

static void DrawScanMarkersImg(ImDrawList *dl, const ImVec2 &p0, float W, float H,
                               const SatPass *pass, AppConfig *cfg, int view)
{
    auto glyph = [&](ImVec2 p, int kind, const char *name) {
        if (kind == 2)
        {
            dl->AddCircleFilled(p, UIPx(2.5f), ColU32(g_theme.world.sat, 0.95f));
        }
        else if (kind == 1)
        {
            float s = UIPx(3.5f);
            dl->AddQuadFilled(ImVec2(p.x, p.y - s), ImVec2(p.x + s, p.y),
                              ImVec2(p.x, p.y + s), ImVec2(p.x - s, p.y),
                              ColU32(g_theme.world.sat_selected, 1.0f));
            dl->AddCircle(p, UIPx(5.0f), ColU32(g_theme.world.sat_selected, 0.8f), 0, UIPx(1.0f));
        }
        else
        {
            float s = UIPx(3.5f);
            dl->AddQuadFilled(ImVec2(p.x, p.y - s), ImVec2(p.x + s, p.y),
                              ImVec2(p.x, p.y + s), ImVec2(p.x - s, p.y),
                              ColU32(g_theme.ui.accent, 0.9f));
            dl->AddQuad(ImVec2(p.x, p.y - s), ImVec2(p.x + s, p.y),
                        ImVec2(p.x, p.y + s), ImVec2(p.x - s, p.y),
                        ColU32(g_theme.ui.text, 0.9f), UIPx(1.0f));
        }
        if (view == 0 && name && name[0])
        {
            ImVec2 tp(p.x + UIPx(5.0f), p.y - UIPx(6.0f));
            dl->AddText(ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 180), name);
            dl->AddText(tp, ColU32(g_theme.ui.text, 0.9f), name);
        }
    };

    if (view == 0)
    {
        // map view: cheap, computed synchronously each frame
        struct Marker { float lat, lon; const char *name; int kind; };
        std::vector<Marker> markers;
        for (int i = 0; i < location_count; i++)
            markers.push_back({ locations[i].lat, locations[i].lon, locations[i].name,
                                locations[i].is_home ? 1 : 0 });

        double t_sub = pass->aos_epoch + 0.5 * (pass->los_epoch - pass->aos_epoch);
        Vector3 Psub, Vsub;
        sat_prop_state(pass->sat, get_unix_from_epoch(t_sub), &Psub, &Vsub);
        Vector3 sub = Vector3Scale(Vector3Normalize(Psub), EARTH_RADIUS_KM);
        float sub_lat, sub_lon;
        GroundLL(sub, epoch_to_gmst(t_sub), cfg->earth_rotation_offset, &sub_lat, &sub_lon);
        markers.push_back({ sub_lat, sub_lon, "SAT", 2 });

        double gmst = epoch_to_gmst(t_sub);
        const float offs[3] = { -W, 0.0f, W };
        for (const Marker &m : markers)
        {
            float latr = m.lat * DEG2RAD;
            float lon_eci = (m.lon + (float)gmst + cfg->earth_rotation_offset) * DEG2RAD;
            Vector3 M = { EARTH_RADIUS_KM * cosf(latr) * cosf(lon_eci),
                          EARTH_RADIUS_KM * sinf(latr),
                          -EARTH_RADIUS_KM * cosf(latr) * sinf(lon_eci) };
            float x, y;
            get_map_coordinates(M, gmst, cfg->earth_rotation_offset, W, H, &x, &y);
            for (int k = 0; k < 3; k++)
                glyph(ImVec2(p0.x + x + W * 0.5f + offs[k], p0.y + y + H * 0.5f), m.kind, m.name);
        }
        return;
    }

    if (view == 1)
    {
        // shot view: positions precomputed in the async job (normalized [0,1])
        for (size_t i = 0; i < s_shot_markers.size(); i++)
        {
            const ImVec2 &uv = s_shot_markers[i];
            glyph(ImVec2(p0.x + uv.x * W, p0.y + uv.y * H), s_shot_marker_kinds[i], NULL);
        }
        return;
    }

    // strip view: positions precomputed in the async job (normalized [0,1])
    for (size_t i = 0; i < s_strip_markers.size(); i++)
    {
        const ImVec2 &uv = s_strip_markers[i];
        glyph(ImVec2(p0.x + uv.x * (W - 1.0f), p0.y + uv.y * H), s_strip_marker_kinds[i], NULL);
    }
}

/* ---- image export -------------------------------------------------------- */

/* wrap an RGBA buffer as an Image and write it to disk (no ownership transfer) */
static bool ExportOutImage(const char *path, const std::vector<unsigned char> &px, int W, int H)
{
    if (px.empty() || (size_t)W * H * 4 != px.size())
        return false;
    Image img = { (void *)px.data(), W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    return ExportImage(img, path);
}

/* epoch days -> compact UTC stamp (yyyymmddThhmmssZ) */
static void StampUTC(double epoch, char *out, int n)
{
    time_t t = (time_t)get_unix_from_epoch(epoch);
    struct tm *g = gmtime(&t);
    if (g)
        strftime(out, n, "%Y%m%dT%H%M%SZ", g);
    else
        snprintf(out, n, "00000000T000000Z");
}

static void BuildExportPath(char *out, int n, const char *dir, const char *prefix, uint32_t norad, double epoch)
{
    char stamp[32];
    StampUTC(epoch, stamp, sizeof(stamp));
    if (dir && dir[0])
        snprintf(out, n, "%s/%s_%u_%s.png", dir, prefix, norad, stamp);
    else
        snprintf(out, n, "%s_%u_%s.png", prefix, norad, stamp);
}

/* ---- interactive image-view helpers -------------------------------------- */

/* clamp a zoom factor to the supported range */
static float ZoomClamp(float z)
{
    if (z < 0.05f)
        z = 0.05f;
    if (z > 16.0f)
        z = 16.0f;
    return z;
}

/* result of the compact zoom overlay */
struct ZoomOverlayResult
{
    bool fit = false;
    bool hovered = false;
    bool export_clicked = false;
};

/* compact zoom control overlay pinned to the top-right of a rect */
static ZoomOverlayResult ZoomOverlay(const char *id, float *zoom, const ImVec2 &rect_min,
                                     const ImVec2 &rect_max, bool show_export, bool export_enabled)
{
    ZoomOverlayResult res;
    ImGuiStyle &st = ImGui::GetStyle();
    const float pad = UIPx(4.0f);
    const float fh = ImGui::GetFrameHeight();
    const float b_minus = ImGui::CalcTextSize("-").x + st.FramePadding.x * 2.0f;
    const float b_plus = ImGui::CalcTextSize("+").x + st.FramePadding.x * 2.0f;
    const float b_fit = ImGui::CalcTextSize("Fit").x + st.FramePadding.x * 2.0f;
    const float b_exp = ImGui::CalcTextSize("Export PNG").x + st.FramePadding.x * 2.0f;
    const float b_pct = ImGui::CalcTextSize("1600%").x;
    float row_w = b_minus + b_plus + b_fit + b_pct + st.ItemSpacing.x * 3.0f;
    if (show_export)
        row_w += b_exp + st.ItemSpacing.x;
    const ImVec2 bg_max(rect_max.x - pad, rect_min.y + pad + fh + pad * 2.0f);
    const ImVec2 bg_min(bg_max.x - row_w - pad * 2.0f, rect_min.y + pad);

    ImGui::GetWindowDrawList()->AddRectFilled(bg_min, bg_max, IM_COL32(0, 0, 0, 120), UIPx(4.0f));

    ImGui::SetCursorScreenPos(ImVec2(bg_min.x + pad, bg_min.y + pad));
    ImGui::PushID(id);
    if (ImGui::SmallButton("-"))
        *zoom = ZoomClamp(*zoom / 1.25f);
    ImGui::SameLine();
    if (ImGui::SmallButton("+"))
        *zoom = ZoomClamp(*zoom * 1.25f);
    ImGui::SameLine();
    ImGui::Text("%.0f%%", *zoom * 100.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit"))
    {
        *zoom = 1.0f;
        res.fit = true;
    }
    if (show_export)
    {
        ImGui::SameLine();
        if (!export_enabled)
            ImGui::BeginDisabled();
        if (ImGui::SmallButton("Export PNG"))
            res.export_clicked = true;
        if (!export_enabled)
            ImGui::EndDisabled();
        if (!export_enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Enable Textured to export");
    }
    ImGui::PopID();

    res.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(bg_min, bg_max);
    return res;
}

/* ---- tab entry point ----------------------------------------------------- */

void DrawTransitSimulateScanTab(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;

    SatPass *pass = (g_ui.selected_pass_idx >= 0 && g_ui.selected_pass_idx < num_passes)
                        ? &passes[g_ui.selected_pass_idx]
                        : NULL;
    if (!pass || !pass->sat)
    {
        ImGui::TextDisabled("Select a transit to simulate a scan.");
        return;
    }

    // always shows the full horizon-to-horizon pass
    SatPass full_pass;
    {
        uint32_t norad = pass->sat->norad_id_num;
        if (!s_full_pass.valid || s_full_pass.norad != norad ||
            fabs(s_full_pass.aos - pass->aos_epoch) > 1e-6 ||
            fabs(s_full_pass.los - pass->los_epoch) > 1e-6)
        {
            GetFullPassWindow(pass->sat, pass->aos_epoch, pass->los_epoch,
                              &s_full_pass.full_aos, &s_full_pass.full_los);
            s_full_pass.valid = true;
            s_full_pass.norad = norad;
            s_full_pass.aos = pass->aos_epoch;
            s_full_pass.los = pass->los_epoch;
        }
        full_pass = *pass;
        full_pass.aos_epoch = s_full_pass.full_aos;
        full_pass.los_epoch = s_full_pass.full_los;
        pass = &full_pass;
    }

    // drain a completed async job and commit it on the UI thread
    SimScanResult done;
    if (SimScanPoll(&done))
    {
        if (done.gen == s_sim_gen.load())
        {
            if (done.kind == 0)
            {
                s_strip_img = std::move(done.strip_img);
                s_strip_lines = std::move(done.strip_lines);
                s_strip_markers = std::move(done.marker_pts);
                s_strip_marker_kinds = std::move(done.marker_kinds);
                if (s_strip_img.textured && !s_strip_img.px.empty())
                    UploadOutTexture(s_strip_img.W, s_strip_img.H, s_strip_img.px.data());
                s_out_owner = 1;
            }
            else
            {
                s_shot_img = std::move(done.shot_img);
                s_shot_lines = std::move(done.shot_lines);
                s_shot_markers = std::move(done.marker_pts);
                s_shot_marker_kinds = std::move(done.marker_kinds);
                if (s_shot_img.textured && !s_shot_img.px.empty())
                    UploadOutTexture(s_shot_img.W, s_shot_img.H, s_shot_img.px.data());
                s_out_owner = 0;
            }
        }
    }

    static bool s_loaded = false;
    static int s_mode = 0;           /* 0 = whiskbroom, 1 = single-shot */
    static bool s_textured = true;
    static float s_swath_deg = 60.0f;
    static float s_time_frac = 0.5f;
    static int s_aspect = 1;
    static float s_focal_mm = 50.0f;
    static int s_pointing = 0;       /* 0 = manual, 1 = anti-sun */
    static float s_off_nadir = 15.0f;
    static float s_azimuth = 0.0f;
    static float s_roll_deg = 0.0f;
    static float s_skew_deg = 0.0f;  /* whiskbroom scan-plane tilt about the along-track axis */
    static int s_view = 0;           /* 0 = map, 1 = image */
    static bool s_markers = true;
    static int s_shot_res = 1024;
    static float s_swath_km = 300.0f;
    static int s_swath_px = 512;
    static bool s_geocorrect = false;
    static char s_export_dir[256] = { 0 };
    static char s_export_status[256] = { 0 };
    static float s_zoom_persp = 1.0f;
    static float s_zoom_strip = 1.0f;

    if (!s_loaded)
    {
        s_mode = ToolSettingGetInt(cfg, SIMSCAN_KEY_MODE, 0);
        s_textured = ToolSettingGetBool(cfg, SIMSCAN_KEY_TEXTURED, false);
        s_swath_deg = ToolSettingGetFloat(cfg, SIMSCAN_KEY_SWATH, 60.0f);
        s_time_frac = ToolSettingGetFloat(cfg, SIMSCAN_KEY_FRAC, 0.5f);
        s_aspect = ToolSettingGetInt(cfg, SIMSCAN_KEY_ASPECT, 1);
        s_focal_mm = ToolSettingGetFloat(cfg, SIMSCAN_KEY_FOCAL, 50.0f);
        s_pointing = ToolSettingGetInt(cfg, SIMSCAN_KEY_POINTING, 0);
        s_off_nadir = ToolSettingGetFloat(cfg, SIMSCAN_KEY_OFFNADIR, 15.0f);
        s_azimuth = ToolSettingGetFloat(cfg, SIMSCAN_KEY_AZIMUTH, 0.0f);
        s_roll_deg = ToolSettingGetFloat(cfg, SIMSCAN_KEY_ROLL, 0.0f);
        s_skew_deg = ToolSettingGetFloat(cfg, SIMSCAN_KEY_SKEW, 0.0f);
        if (s_skew_deg < -120.0f) s_skew_deg = -120.0f;
        if (s_skew_deg >  120.0f) s_skew_deg =  120.0f;
        s_view = ToolSettingGetInt(cfg, SIMSCAN_KEY_VIEW, 0);
        s_markers = ToolSettingGetBool(cfg, SIMSCAN_KEY_MARKERS, true);
        s_shot_res = ToolSettingGetInt(cfg, SIMSCAN_KEY_SHOT_RES, 1024);
        if (s_shot_res < 256) s_shot_res = 256;
        if (s_shot_res > 4096) s_shot_res = 4096;
        s_swath_px = ToolSettingGetInt(cfg, SIMSCAN_KEY_SWATH_PX,
                                       ToolSettingGetInt(cfg, SIMSCAN_KEY_SCAN_W, 512));
        if (s_swath_px < 64) s_swath_px = 64;
        if (s_swath_px > 4096) s_swath_px = 4096;
        float km = ToolSettingGetFloat(cfg, SIMSCAN_KEY_SWATH_KM, -1.0f);
        if (km < 0.0f)
        {
            km = 300.0f; /* estimate from the legacy swath-angle key */
            if (s_swath_deg > 0.0f)
            {
                double t_mid = pass->aos_epoch + 0.5 * (pass->los_epoch - pass->aos_epoch);
                Vector3 P, V;
                sat_prop_state(pass->sat, get_unix_from_epoch(t_mid), &P, &V);
                Vector3 nadir = Vector3Negate(Vector3Normalize(P));
                Vector3 cross = Vector3Normalize(Vector3CrossProduct(V, P));
                float est = GroundSwathKm(P, nadir, cross, s_swath_deg * 0.5f * DEG2RAD);
                if (est > 1e-3f)
                    km = est;
            }
        }
        s_swath_km = km;
        s_geocorrect = ToolSettingGetBool(cfg, SIMSCAN_KEY_GEOCORRECT, false);
        const char *dir = ToolSettingGetString(cfg, SIMSCAN_KEY_EXPORT_DIR, "");
        snprintf(s_export_dir, sizeof(s_export_dir), "%s", dir ? dir : "");
        if (s_aspect < 0 || s_aspect >= SIMSCAN_ASPECT_COUNT)
            s_aspect = 1;
        s_loaded = true;
    }

    /* derived FOV for the requested swath width (invalidated when geometry changes) */
    bool need_fov = !s_fov.valid || s_fov.norad != (pass->sat ? pass->sat->norad_id_num : 0u) ||
                    fabs(s_fov.aos - pass->aos_epoch) > 1e-6 ||
                    fabs(s_fov.los - pass->los_epoch) > 1e-6 || s_fov.km != s_swath_km;
    if (need_fov)
        ComputeSwathFov(pass, s_swath_km, &s_fov);

    /* ---- two-pane layout: controls left, preview right ---- */
    ImVec2 full = ImGui::GetContentRegionAvail();
    float ctrl_w = full.x * 0.32f;
    ctrl_w = ctrl_w < 220.0f ? 220.0f : (ctrl_w > 340.0f ? 340.0f : ctrl_w);

    ImGui::BeginChild("##simscan_controls", ImVec2(ctrl_w, 0.0f), false);
    ImGui::PushTextWrapPos(0.0f);

    ImGui::SeparatorText("Mode");
    if (ImGui::RadioButton("Whiskbroom / Pushbroom", s_mode == 0))
    {
        s_mode = 0;
        ToolSettingSetInt(cfg, SIMSCAN_KEY_MODE, s_mode);
    }
    if (ImGui::RadioButton("Single-shot", s_mode == 1))
    {
        s_mode = 1;
        ToolSettingSetInt(cfg, SIMSCAN_KEY_MODE, s_mode);
    }

    ImGui::SeparatorText("Display");
    if (ImGui::RadioButton("Textured", s_textured))
    {
        s_textured = true;
        ToolSettingSetBool(cfg, SIMSCAN_KEY_TEXTURED, true);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Line only", !s_textured))
    {
        s_textured = false;
        ToolSettingSetBool(cfg, SIMSCAN_KEY_TEXTURED, false);
    }
    if (ImGui::RadioButton("Map", s_view == 0))
    {
        s_view = 0;
        ToolSettingSetInt(cfg, SIMSCAN_KEY_VIEW, 0);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Image", s_view == 1))
    {
        s_view = 1;
        ToolSettingSetInt(cfg, SIMSCAN_KEY_VIEW, 1);
    }
    if (ImGui::Checkbox("Markers", &s_markers))
        ToolSettingSetBool(cfg, SIMSCAN_KEY_MARKERS, s_markers);

    const int eff_swath_px = s_textured ? s_swath_px : SIMSCAN_LINE_SWATH_PX;

    const float cw = ImGui::GetContentRegionAvail().x;

    if (s_mode == 0)
    {
        ImGui::SeparatorText("Whiskbroom");
        ImGui::TextDisabled("Swath width (km)");
        ImGui::SetNextItemWidth(cw);
        if (ImGui::SliderFloat("##swath_km", &s_swath_km, 1.0f, 5000.0f, "%.0f km"))
            ToolSettingSetFloat(cfg, SIMSCAN_KEY_SWATH_KM, s_swath_km);
        if (s_textured)
        {
            ImGui::TextDisabled("Swath width (px)");
            ImGui::SetNextItemWidth(cw);
            if (ImGui::SliderInt("##swath_px", &s_swath_px, 64, 4096))
                ToolSettingSetInt(cfg, SIMSCAN_KEY_SWATH_PX, s_swath_px);
        }
        else
        {
            ImGui::TextDisabled("Swath width (px): fixed at %d in line mode", SIMSCAN_LINE_SWATH_PX);
        }
        ImGui::TextDisabled("FOV ~ %.1f deg   (GSD %.0f m/px)", s_fov.half_rad * 2.0f * RAD2DEG,
                            1000.0f * s_swath_km / (float)eff_swath_px);
        if (s_fov.capped)
            ImGui::TextDisabled("capped at limb (visible ~ %.0f km)", s_fov.visible_km);

        ImGui::TextDisabled("Skew / Roll (deg)");
        ImGui::SetNextItemWidth(cw);
        if (ImGui::SliderFloat("##skew", &s_skew_deg, -120.0f, 120.0f, "%.1f°"))
        {
            ToolSettingSetFloat(cfg, SIMSCAN_KEY_SKEW, s_skew_deg);
            if (s_skew_deg != 0.0f && s_geocorrect)
            {
                s_geocorrect = false;
                ToolSettingSetBool(cfg, SIMSCAN_KEY_GEOCORRECT, s_geocorrect);
            }
        }

        if (s_view == 1)
        {
            if (ImGui::Checkbox("Geo correct", &s_geocorrect))
            {
                ToolSettingSetBool(cfg, SIMSCAN_KEY_GEOCORRECT, s_geocorrect);
                if (s_geocorrect && s_skew_deg != 0.0f)
                {
                    s_skew_deg = 0.0f;
                    ToolSettingSetFloat(cfg, SIMSCAN_KEY_SKEW, s_skew_deg);
                }
            }
            ImGui::TextDisabled("Export directory");
            ImGui::SetNextItemWidth(cw);
            if (ImGui::InputText("##export_dir", s_export_dir, sizeof(s_export_dir)))
                ToolSettingSetString(cfg, SIMSCAN_KEY_EXPORT_DIR, s_export_dir);
        }
    }
    else
    {
        ImGui::SeparatorText("Single-shot");
        ImGui::TextDisabled("Capture time (AOS -> LOS)");
        ImGui::SetNextItemWidth(cw);
        if (ImGui::SliderFloat("##frac", &s_time_frac, 0.0f, 1.0f, ""))
            ToolSettingSetFloat(cfg, SIMSCAN_KEY_FRAC, s_time_frac);

        double span = pass->los_epoch - pass->aos_epoch;
        double cap_epoch = pass->aos_epoch + (double)s_time_frac * span;
        char t_str[32], el_str[32], tot_str[32];
        epoch_to_time_str(cap_epoch, t_str);
        FmtDuration((double)s_time_frac * span, el_str, sizeof(el_str));
        FmtDuration(span, tot_str, sizeof(tot_str));
        ImGui::TextDisabled("%s    T+ %s / %s", t_str, el_str, tot_str);

        ImGui::TextDisabled("Aspect ratio");
        ImGui::SetNextItemWidth(cw);
        if (ImGui::Combo("##aspect", &s_aspect, s_aspect_labels, SIMSCAN_ASPECT_COUNT))
            ToolSettingSetInt(cfg, SIMSCAN_KEY_ASPECT, s_aspect);

        if (s_view == 1)
        {
            ImGui::TextDisabled("Render resolution (px)");
            ImGui::SetNextItemWidth(cw);
            if (ImGui::SliderInt("##shot_res", &s_shot_res, 256, 4096))
                ToolSettingSetInt(cfg, SIMSCAN_KEY_SHOT_RES, s_shot_res);
        }

        ImGui::TextDisabled("Focal length (full-frame equivalent)");
        ImGui::SetNextItemWidth(cw);
        if (ImGui::SliderFloat("##f", &s_focal_mm, 8.0f, 400.0f, "%.1f mm"))
            ToolSettingSetFloat(cfg, SIMSCAN_KEY_FOCAL, s_focal_mm);

        ImGui::TextDisabled("Pointing");
        if (ImGui::RadioButton("Manual", s_pointing == 0))
        {
            s_pointing = 0;
            ToolSettingSetInt(cfg, SIMSCAN_KEY_POINTING, s_pointing);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Anti-sun", s_pointing == 1))
        {
            s_pointing = 1;
            ToolSettingSetInt(cfg, SIMSCAN_KEY_POINTING, s_pointing);
        }

        if (s_pointing == 0)
        {
            ImGui::TextDisabled("Off-nadir");
            ImGui::SetNextItemWidth(cw);
            if (ImGui::SliderFloat("##offnadir", &s_off_nadir, 0.0f, 90.0f, "%.1f°"))
                ToolSettingSetFloat(cfg, SIMSCAN_KEY_OFFNADIR, s_off_nadir);

            ImGui::TextDisabled("Azimuth");
            ImGui::SetNextItemWidth(cw);
            if (ImGui::SliderFloat("##azimuth", &s_azimuth, 0.0f, 360.0f, "%.1f°"))
                ToolSettingSetFloat(cfg, SIMSCAN_KEY_AZIMUTH, s_azimuth);
        }

        ImGui::TextDisabled("Roll");
        ImGui::SetNextItemWidth(cw);
        if (ImGui::SliderFloat("##roll", &s_roll_deg, -180.0f, 180.0f, "%.1f°"))
            ToolSettingSetFloat(cfg, SIMSCAN_KEY_ROLL, s_roll_deg);
    }

    if (s_export_status[0])
    {
        ImGui::Separator();
        ImGui::TextDisabled("%s", s_export_status);
    }

    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::SameLine();

    /* ---- preview pane ---- */
    ImGui::BeginChild("##simscan_preview", ImVec2(0.0f, 0.0f), false);
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (s_view == 0)
    {
        /* ---- map view ---- */
        float map_w = avail.x;
        float map_h = map_w * 0.5f;
        if (map_h > avail.y)
        {
            map_h = avail.y;
            map_w = map_h * 2.0f;
        }
        ImGui::SetCursorPos(ImVec2((avail.x - map_w) * 0.5f, (avail.y - map_h) * 0.5f));
        const ImVec2 p0 = ImGui::GetCursorScreenPos();

        bool use_tex = s_textured;
        if (use_tex)
        {
            EnsureEarthTexture();
            if (s_earth_tex.id == 0)
                use_tex = false; /* texture missing: fall back to line-only */
        }

        if (use_tex)
            ImGui::Image((ImTextureID)(intptr_t)s_earth_tex.id, ImVec2(map_w, map_h));
        else
            ImGui::Dummy(ImVec2(map_w, map_h));

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p1 = ImVec2(p0.x + map_w, p0.y + map_h);
        dl->PushClipRect(p0, p1, true);

        const float thick = UIPx(1.0f);
        const float offs[3] = { -map_w, 0.0f, map_w };

        if (!use_tex)
        {
            dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));
            Color coast = g_theme.ui.text;
            DrawDetailLines(dl, p0, map_w, map_h, MAP_COAST_POINTS, MAP_COAST_LINES, MAP_COAST_LINE_COUNT,
                            ColU32(coast, 0.55f), thick);
            DrawDetailLines(dl, p0, map_w, map_h, MAP_BORDER_POINTS, MAP_BORDER_LINES, MAP_BORDER_LINE_COUNT,
                            ColU32(coast, 0.35f), thick);
        }

        auto to_screen = [&](const Vector2 &p, float off) -> ImVec2 {
            return ImVec2(p0.x + p.x + map_w * 0.5f + off, p0.y + p.y + map_h * 0.5f);
        };

        if (s_mode == 0)
        {
            /* ---- whiskbroom swath ---- */
            bool need = !s_swath.valid || s_swath.norad != (pass->sat ? pass->sat->norad_id_num : 0u) ||
                        fabs(s_swath.aos - pass->aos_epoch) > 1e-6 || s_swath.half_rad != s_fov.half_rad ||
                        s_swath.skew_rad != s_skew_deg * DEG2RAD ||
                        s_swath.map_w != map_w || s_swath.map_h != map_h;
            if (need)
                ComputeSwath(pass, cfg, map_w, map_h, s_fov.half_rad, s_skew_deg * DEG2RAD);

            const ImU32 fill = ColU32(g_theme.world.footprint_fill, 0.35f);
            const ImU32 border = ColU32(g_theme.world.footprint_border, 0.95f);
            const int n = (int)s_swath.left.size();

            for (int k = 0; k < 3; k++)
            {
                float off = offs[k];
                for (int i = 0; i + 1 < n; i++)
                {
                    if (!s_swath.ok[i] || !s_swath.ok[i + 1])
                        continue;
                    ImVec2 L0 = to_screen(s_swath.left[i], off);
                    ImVec2 R0 = to_screen(s_swath.right[i], off);
                    ImVec2 L1 = to_screen(s_swath.left[i + 1], off);
                    ImVec2 R1 = to_screen(s_swath.right[i + 1], off);
                    dl->AddTriangleFilled(L0, R0, R1, fill);
                    dl->AddTriangleFilled(L0, R1, L1, fill);
                    dl->AddLine(L0, L1, border, thick);
                    dl->AddLine(R0, R1, border, thick);
                }
            }

            char info[96];
            snprintf(info, sizeof(info), "Swath %.0f km  FOV %.1f deg  %d edges",
                     s_swath_km, s_fov.half_rad * 2.0f * RAD2DEG, n);
            dl->AddText(ImVec2(p0.x + UIPx(6.0f), p0.y + UIPx(6.0f)), ColU32(g_theme.ui.text, 0.9f), info);
        }
        else
        {
            /* ---- single-shot footprint ---- */
            bool need = !s_shot.valid || s_shot.norad != (pass->sat ? pass->sat->norad_id_num : 0u) ||
                        fabs(s_shot.aos - pass->aos_epoch) > 1e-6 || s_shot.frac != s_time_frac ||
                        s_shot.aspect != s_aspect || s_shot.focal != s_focal_mm ||
                        s_shot.pointing != s_pointing || s_shot.off != s_off_nadir ||
                        s_shot.az != s_azimuth || s_shot.roll != s_roll_deg ||
                        s_shot.map_w != map_w || s_shot.map_h != map_h;
            if (need)
                ComputeShot(pass, cfg, map_w, map_h, s_time_frac, s_aspect, s_focal_mm,
                            s_pointing, s_off_nadir, s_azimuth, s_roll_deg);

            const ImU32 fill = ColU32(g_theme.ui.accent, 0.30f);
            const ImU32 edge = ColU32(g_theme.ui.accent, 1.0f);
            bool quad_ok = s_shot.corner_ok[0] && s_shot.corner_ok[1] &&
                           s_shot.corner_ok[2] && s_shot.corner_ok[3];

            for (int k = 0; k < 3; k++)
            {
                float off = offs[k];
                if (quad_ok)
                {
                    ImVec2 c0 = to_screen(s_shot.corner[0], off);
                    ImVec2 c1 = to_screen(s_shot.corner[1], off);
                    ImVec2 c2 = to_screen(s_shot.corner[2], off);
                    ImVec2 c3 = to_screen(s_shot.corner[3], off);
                    dl->AddQuadFilled(c0, c1, c2, c3, fill);
                    dl->AddQuad(c0, c1, c2, c3, edge, thick);
                }
                if (s_shot.subsat_ok)
                {
                    ImVec2 s = to_screen(s_shot.subsat, off);
                    dl->AddCircleFilled(s, UIPx(2.5f), ColU32(g_theme.ui.text, 0.9f));
                }
                if (s_shot.bores_ok)
                {
                    ImVec2 b = to_screen(s_shot.bores, off);
                    dl->AddCircle(b, UIPx(4.0f), edge, 0, thick);
                    dl->AddLine(ImVec2(b.x - UIPx(6.0f), b.y), ImVec2(b.x + UIPx(6.0f), b.y), edge, thick);
                    dl->AddLine(ImVec2(b.x, b.y - UIPx(6.0f)), ImVec2(b.x, b.y + UIPx(6.0f)), edge, thick);
                }
            }

            /* rotated photo-frame overlay centred on the canvas */
            float rw = s_aspect_rw[s_aspect], rh = s_aspect_rh[s_aspect];
            float fh = map_h * 0.35f;
            float fw = fh * (rw / rh);
            ImVec2 cc = ImVec2(p0.x + map_w * 0.5f, p0.y + map_h * 0.5f);
            float ang = s_roll_deg * DEG2RAD;
            float ca = cosf(ang), sa = sinf(ang);
            const float fx[4] = { -0.5f, 0.5f, 0.5f, -0.5f };
            const float fy[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
            ImVec2 fr[4];
            for (int i = 0; i < 4; i++)
            {
                float lx = fx[i] * fw, ly = fy[i] * fh;
                fr[i] = ImVec2(cc.x + lx * ca - ly * sa, cc.y + lx * sa + ly * ca);
            }
            const ImU32 frame_col = ColU32(g_theme.ui.text, 0.85f);
            for (int i = 0; i < 4; i++)
                dl->AddLine(fr[i], fr[(i + 1) % 4], frame_col, UIPx(1.5f));
            dl->AddLine(ImVec2(cc.x - UIPx(6.0f), cc.y), ImVec2(cc.x + UIPx(6.0f), cc.y), frame_col, thick);
            dl->AddLine(ImVec2(cc.x, cc.y - UIPx(6.0f)), ImVec2(cc.x, cc.y + UIPx(6.0f)), frame_col, thick);

            char info[96];
            snprintf(info, sizeof(info), "%s   f=%.0fmm", s_aspect_labels[s_aspect], s_focal_mm);
            dl->AddText(ImVec2(p0.x + UIPx(6.0f), p0.y + UIPx(6.0f)), ColU32(g_theme.ui.text, 0.9f), info);
        }

        if (s_markers)
            DrawScanMarkersImg(dl, p0, map_w, map_h, pass, cfg, 0);

        dl->PopClipRect();
    }
    else if (s_mode == 0)
    {
        /* ---- strip image ---- */
        int W = eff_swath_px;
        int H = 0;
        ComputeStripDimsCached(pass->sat, pass->sat ? pass->sat->norad_id_num : 0u,
                               pass->aos_epoch, pass->los_epoch, s_swath_km, eff_swath_px, &W, &H);

        float disp_w = avail.x;
        float disp_h = avail.x * ((float)H / (float)W);

        bool use_tex = s_textured;
        if (use_tex)
        {
            EnsureEarthImages();
            if (!s_earth_img.data)
                use_tex = false;
        }
        const char *earth_path = ThemeAssetPath(g_theme.textures.earth);

        bool need = !s_strip_img.valid ||
                    s_strip_img.norad != (pass->sat ? pass->sat->norad_id_num : 0u) ||
                    fabs(s_strip_img.aos - pass->aos_epoch) > 1e-6 ||
                    fabs(s_strip_img.los - pass->los_epoch) > 1e-6 ||
                    s_strip_img.half_rad != s_fov.half_rad ||
                    s_strip_img.skew_rad != s_skew_deg * DEG2RAD ||
                    s_strip_img.geocorrect != s_geocorrect ||
                    s_strip_img.W != W || s_strip_img.H != H ||
                    s_strip_img.textured != use_tex ||
                    strcmp(s_strip_img.earth_path, earth_path) != 0 ||
                    s_out_owner != 1;
        uint64_t key = SimScanStripKey(pass->sat ? pass->sat->norad_id_num : 0u,
                                       pass->aos_epoch, pass->los_epoch, s_fov.half_rad,
                                       s_skew_deg * DEG2RAD, s_geocorrect, W, H, use_tex, earth_path);
        if (need && !ImGui::IsAnyItemActive() && key != s_sim_req_key)
        {
            EnsureEarthImages(); // raylib image load stays on the UI thread
            SimScanJob job;
            job.kind = 0;
            job.norad = pass->sat ? pass->sat->norad_id_num : 0u;
            job.aos = pass->aos_epoch;
            job.los = pass->los_epoch;
            job.sat_copy = *pass->sat; // private copy: deep-space sgp4 mutates satrec
            job.swath_km = s_swath_km;
            job.swath_px = eff_swath_px;
            job.geocorrect = s_geocorrect;
            job.half_rad = s_fov.half_rad;
            job.skew_rad = s_skew_deg * DEG2RAD;
            job.W = W;
            job.H = H;
            job.textured = use_tex;
            job.earth_rotation_offset = cfg->earth_rotation_offset;
            job.bg = g_theme.world.bg;
            job.earth = &s_earth_img;
            job.night = &s_night_img;
            snprintf(job.earth_path, sizeof(job.earth_path), "%s", earth_path);
            job.marker_count = 0;
            for (int i = 0; i < location_count && i < MAX_LOCATIONS; i++)
            {
                job.markers[job.marker_count].lat = locations[i].lat;
                job.markers[job.marker_count].lon = locations[i].lon;
                job.markers[job.marker_count].kind = locations[i].is_home ? 1 : 0;
                job.marker_count++;
            }
            if (SimScanIsRunning())
                LOG_INFO("SimScan resubmit while a job is in flight (kind=0 key %llu->%llu) - prior job cancelled",
                         (unsigned long long)s_sim_req_key, (unsigned long long)key);
            SimScanSubmit(job);
            s_sim_req_key = key;
        }

        const ImVec2 content = ImVec2(disp_w * s_zoom_strip, disp_h * s_zoom_strip);
        ImGui::BeginChild("##scan_preview_strip", ImVec2(avail.x, avail.y), false,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (SimScanIsRunning())
        {
            ImGui::TextUnformatted("Loading simulation...");
            ImGui::ProgressBar(SimScanProgress(), ImVec2(-FLT_MIN, 0.0f));
        }
        else
        {
        ImVec2 p0c = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##canvas", content);
        bool canvas_hovered = ImGui::IsItemHovered();
        bool canvas_active = ImGui::IsItemActive();
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p1 = ImVec2(p0c.x + content.x, p0c.y + content.y);
        dl->PushClipRect(p0c, p1, true);
        if (use_tex && s_out_tex.id != 0)
            dl->AddImage((ImTextureID)(intptr_t)s_out_tex.id, p0c, p1);
        else
        {
            dl->AddRectFilled(p0c, p1, IM_COL32(0, 0, 0, 255));
            if (s_strip_lines.valid)
                DrawStripLines(dl, p0c, content.x, content.y, &s_strip_lines);
        }
        if (s_markers && s_strip_img.valid)
            DrawScanMarkersImg(dl, p0c, content.x, content.y, pass, cfg, 2);
        dl->PopClipRect();

        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        const float sb = (ImGui::GetScrollMaxY() > 0.0f) ? ImGui::GetStyle().ScrollbarSize : 0.0f;
        ZoomOverlayResult zr = ZoomOverlay("strip", &s_zoom_strip, ImVec2(wp.x, wp.y),
                                           ImVec2(wp.x + ws.x - sb, wp.y + ws.y), true, use_tex);
        if (zr.fit)
        {
            ImGui::SetScrollX(0.0f);
            ImGui::SetScrollY(0.0f);
        }
        if (zr.export_clicked)
        {
            char path[512];
            double ep = s_strip_img.rows.empty()
                            ? pass->aos_epoch
                            : s_strip_img.rows[s_strip_img.rows.size() / 2].t;
            BuildExportPath(path, sizeof(path), s_export_dir, "simscan_swath",
                            s_strip_img.norad, ep);
            if (ExportOutImage(path, s_strip_img.px, s_strip_img.W, s_strip_img.H))
            {
                snprintf(s_export_status, sizeof(s_export_status), "Saved %s", path);
                NotifyPush(NOTIFY_SUCCESS, ICON_FA_FILE_IMAGE, "Saved %s", path);
            }
            else
            {
                snprintf(s_export_status, sizeof(s_export_status), "Export failed: %s", path);
                NotifyPush(NOTIFY_ERROR, ICON_FA_TRIANGLE_EXCLAMATION, "Export failed: %s", path);
            }
        }

        ImGuiIO &io = ImGui::GetIO();
        if (canvas_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        }
        if (canvas_hovered && !zr.hovered && io.MouseWheel != 0.0f)
        {
            const ImVec2 child_pos = ImGui::GetWindowPos();
            const ImVec2 scroll = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            const ImVec2 rel = ImVec2(io.MousePos.x - child_pos.x + scroll.x,
                                      io.MousePos.y - child_pos.y + scroll.y);
            const float old_zoom = s_zoom_strip;
            const ImVec2 img_pt = ImVec2(rel.x / old_zoom, rel.y / old_zoom);
            s_zoom_strip = ZoomClamp(old_zoom * powf(1.10f, io.MouseWheel));
            ImGui::SetScrollX(img_pt.x * s_zoom_strip - (io.MousePos.x - child_pos.x));
            ImGui::SetScrollY(img_pt.y * s_zoom_strip - (io.MousePos.y - child_pos.y));
        }
        }
        ImGui::EndChild();
    }
    else
    {
        /* ---- perspective photo ---- */
        float rw = s_aspect_rw[s_aspect], rh = s_aspect_rh[s_aspect];

        float dw, dh;
        if (avail.x / rw <= avail.y / rh)
        {
            dw = avail.x;
            dh = dw * rh / rw;
        }
        else
        {
            dh = avail.y;
            dw = dh * rw / rh;
        }
        float W_log = roundf(dw);
        float H_log = W_log * rh / rw;
        int W, H;
        ComputeShotDims(s_shot_res, s_aspect, &W, &H);

        bool use_tex = s_textured;
        if (use_tex)
        {
            EnsureEarthImages();
            if (!s_earth_img.data)
                use_tex = false;
        }
        const char *earth_path = ThemeAssetPath(g_theme.textures.earth);

        bool need = !s_shot_img.valid ||
                    s_shot_img.norad != (pass->sat ? pass->sat->norad_id_num : 0u) ||
                    fabs(s_shot_img.aos - pass->aos_epoch) > 1e-6 ||
                    fabs(s_shot_img.los - pass->los_epoch) > 1e-6 ||
                    s_shot_img.frac != s_time_frac || s_shot_img.aspect != s_aspect ||
                    s_shot_img.focal != s_focal_mm || s_shot_img.pointing != s_pointing ||
                    s_shot_img.off != s_off_nadir || s_shot_img.az != s_azimuth ||
                    s_shot_img.roll != s_roll_deg ||
                    s_shot_img.res != s_shot_res ||
                    s_shot_img.W != W || s_shot_img.H != H ||
                    s_shot_img.textured != use_tex ||
                    strcmp(s_shot_img.earth_path, earth_path) != 0 ||
                    s_out_owner != 0;
        uint64_t key = SimScanShotKey(pass->sat ? pass->sat->norad_id_num : 0u,
                                      pass->aos_epoch, pass->los_epoch, s_time_frac, s_aspect,
                                      s_focal_mm, s_pointing, s_off_nadir, s_azimuth, s_roll_deg,
                                      s_shot_res, W, H, use_tex, earth_path);
        if (need && !ImGui::IsAnyItemActive() && key != s_sim_req_key)
        {
            EnsureEarthImages(); // raylib image load stays on the UI thread
            SimScanJob job;
            job.kind = 1;
            job.norad = pass->sat ? pass->sat->norad_id_num : 0u;
            job.aos = pass->aos_epoch;
            job.los = pass->los_epoch;
            job.sat_copy = *pass->sat; // private copy: deep-space sgp4 mutates satrec
            job.frac = s_time_frac;
            job.aspect = s_aspect;
            job.focal = s_focal_mm;
            job.pointing = s_pointing;
            job.off = s_off_nadir;
            job.az = s_azimuth;
            job.roll = s_roll_deg;
            job.shot_res = s_shot_res;
            job.W = W;
            job.H = H;
            job.textured = use_tex;
            job.earth_rotation_offset = cfg->earth_rotation_offset;
            job.bg = g_theme.world.bg;
            job.earth = &s_earth_img;
            job.night = &s_night_img;
            snprintf(job.earth_path, sizeof(job.earth_path), "%s", earth_path);
            job.marker_count = 0;
            for (int i = 0; i < location_count && i < MAX_LOCATIONS; i++)
            {
                job.markers[job.marker_count].lat = locations[i].lat;
                job.markers[job.marker_count].lon = locations[i].lon;
                job.markers[job.marker_count].kind = locations[i].is_home ? 1 : 0;
                job.marker_count++;
            }
            if (SimScanIsRunning())
                LOG_INFO("SimScan resubmit while a job is in flight (kind=1 key %llu->%llu) - prior job cancelled",
                         (unsigned long long)s_sim_req_key, (unsigned long long)key);
            SimScanSubmit(job);
            s_sim_req_key = key;
        }

        const ImVec2 content = ImVec2(W_log * s_zoom_persp, H_log * s_zoom_persp);
        ImGui::BeginChild("##scan_preview_persp", ImVec2(avail.x, avail.y), false,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (SimScanIsRunning())
        {
            ImGui::TextUnformatted("Loading simulation...");
            ImGui::ProgressBar(SimScanProgress(), ImVec2(-FLT_MIN, 0.0f));
        }
        else
        {
        ImVec2 p0c = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##canvas", content);
        bool canvas_hovered = ImGui::IsItemHovered();
        bool canvas_active = ImGui::IsItemActive();
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);

        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 p1 = ImVec2(p0c.x + content.x, p0c.y + content.y);
        dl->PushClipRect(p0c, p1, true);
        if (use_tex && s_out_tex.id != 0)
            dl->AddImage((ImTextureID)(intptr_t)s_out_tex.id, p0c, p1);
        else
        {
            dl->AddRectFilled(p0c, p1, IM_COL32(0, 0, 0, 255));
            if (s_shot_lines.valid)
                DrawShotLines(dl, p0c, content.x, content.y, &s_shot_lines);
        }
        if (s_markers && s_shot_img.valid)
            DrawScanMarkersImg(dl, p0c, content.x, content.y, pass, cfg, 1);
        dl->PopClipRect();

        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        const float sb = (ImGui::GetScrollMaxY() > 0.0f) ? ImGui::GetStyle().ScrollbarSize : 0.0f;
        ZoomOverlayResult zr = ZoomOverlay("persp", &s_zoom_persp, ImVec2(wp.x, wp.y),
                                           ImVec2(wp.x + ws.x - sb, wp.y + ws.y), true, use_tex);
        if (zr.fit)
        {
            ImGui::SetScrollX(0.0f);
            ImGui::SetScrollY(0.0f);
        }
        if (zr.export_clicked)
        {
            char path[512];
            BuildExportPath(path, sizeof(path), s_export_dir, "simscan_shot",
                            s_shot_img.norad, s_shot_img.t);
            if (ExportOutImage(path, s_shot_img.px, s_shot_img.W, s_shot_img.H))
            {
                snprintf(s_export_status, sizeof(s_export_status), "Saved %s", path);
                NotifyPush(NOTIFY_SUCCESS, ICON_FA_FILE_IMAGE, "Saved %s", path);
            }
            else
            {
                snprintf(s_export_status, sizeof(s_export_status), "Export failed: %s", path);
                NotifyPush(NOTIFY_ERROR, ICON_FA_TRIANGLE_EXCLAMATION, "Export failed: %s", path);
            }
        }

        ImGuiIO &io = ImGui::GetIO();
        if (canvas_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
        }
        if (canvas_hovered && !zr.hovered && io.MouseWheel != 0.0f)
        {
            const ImVec2 child_pos = ImGui::GetWindowPos();
            const ImVec2 scroll = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            const ImVec2 rel = ImVec2(io.MousePos.x - child_pos.x + scroll.x,
                                      io.MousePos.y - child_pos.y + scroll.y);
            const float old_zoom = s_zoom_persp;
            const ImVec2 img_pt = ImVec2(rel.x / old_zoom, rel.y / old_zoom);
            s_zoom_persp = ZoomClamp(old_zoom * powf(1.10f, io.MouseWheel));
            ImGui::SetScrollX(img_pt.x * s_zoom_persp - (io.MousePos.x - child_pos.x));
            ImGui::SetScrollY(img_pt.y * s_zoom_persp - (io.MousePos.y - child_pos.y));
        }
        }
        ImGui::EndChild();
    }

    ImGui::EndChild();
}
