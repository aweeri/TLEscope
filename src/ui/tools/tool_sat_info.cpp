/*
 * tool_sat_info.cpp - Satellite Info (inspector) panel
 */

#include "tools.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "tools_settings.h"
#include "core/astro.h"
#include "core/propagator.h"
#include "core/theme.h"
#include "core/location.h"
#include "core/config.h"

#include <cstdio>
#include <cmath>

#include <raylib.h>
#include <raymath.h>

#include "imgui.h"
#include "IconsFontAwesome6.h"

/** observer position in ECI, same axis convention as calculate_position() */
static Vector3 calc_observer_eci(const Location *obs, double gmst_deg)
{
    double ox, oy, oz;
    geodetic_to_ecef(obs->lat, obs->lon + gmst_deg, obs->alt, &ox, &oy, &oz);
    Vector3 o = { (float)ox, (float)oz, (float)-oy };
    return o;
}

/** topocentric declination / right ascension of the satellite as seen from the observer */
static void calc_topocentric_radec(Vector3 eci_pos, Vector3 obs_eci,
                                   double *out_dec_deg, double *out_ra_deg)
{
    double rx = eci_pos.x - obs_eci.x;
    double ry = eci_pos.y - obs_eci.y;
    double rz = eci_pos.z - obs_eci.z;
    double r = sqrt(rx * rx + ry * ry + rz * rz);
    if (r < 0.001) { *out_dec_deg = 0; *out_ra_deg = 0; return; }

    *out_dec_deg = asin(ry / r) * RAD2DEG;

    double ra = atan2(-rz, rx) * RAD2DEG;
    while (ra < 0.0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
    *out_ra_deg = ra;
}

/** geocentric (Earth-centered) declination / right ascension in the ECI frame (J2000-like) */
static void calc_geocentric_radec(Vector3 eci_pos,
                                  double *out_dec_deg, double *out_ra_deg)
{
    double r = sqrt(eci_pos.x * eci_pos.x + eci_pos.y * eci_pos.y + eci_pos.z * eci_pos.z);
    if (r < 0.001) { *out_dec_deg = 0; *out_ra_deg = 0; return; }

    *out_dec_deg = asin(eci_pos.y / r) * RAD2DEG;

    double ra = atan2(-eci_pos.z, eci_pos.x) * RAD2DEG;
    while (ra < 0.0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;
    *out_ra_deg = ra;
}

/** apparent angular speed (deg/s) of the satellite across the sky from the observer */
static double calc_topocentric_ang_speed(Satellite *sat, double current_unix, Vector3 obs_eci)
{
    Vector3 p = calculate_position(sat, current_unix);
    Vector3 v = calculate_velocity(sat, current_unix);

    double rx = p.x - obs_eci.x, ry = p.y - obs_eci.y, rz = p.z - obs_eci.z;
    double r = sqrt(rx * rx + ry * ry + rz * rz);
    if (r < 0.001) return 0.0;

    /* transverse speed / range = angular rate (rad/s) */
    double vr = (rx * v.x + ry * v.y + rz * v.z) / r;
    double vt2 = (v.x * v.x + v.y * v.y + v.z * v.z) - vr * vr;
    if (vt2 < 0.0) vt2 = 0.0;

    return (sqrt(vt2) / r) * RAD2DEG;
}

/* -- GEO analysis constants ------------------------------------------------ */
#define GEO_RADIUS_KM        42164.17          /* circular GEO radius (drift in km/day) */
#define SIDEREAL_DEG_PER_DAY 360.98564736629   /* Earth rotation, matches epoch_to_gmst */
#define GEO_PERIOD_MIN       1436.07           /* sidereal day in minutes */
#define GEO_PERIOD_TOL_MIN   60.0              /* +/- tolerance for the GEO regime band */
#define GEO_INCL_TOL_DEG     15.0              /* max inclination for "GEO" */
#define GEO_ECC_TOL          0.05              /* max eccentricity for "GEO" */
#define GEO_DRIFT_EPS        1e-6              /* "on station" threshold, deg/day */
#define GEO_KEY_TARGET_LON   "geo.target_lon"  /* persisted target longitude (deg) */

/* -- GEO classification ---------------------------------------------------- */
typedef enum
{
    GEO_YES = 0,
    GEO_NOT_ECCENTRIC,  
    GEO_NOT_INCLINED,
    GEO_NOT
} GeoClass;

static GeoClass classify_geo(double period_min, double incl_deg, double ecc)
{
    bool in_geo_band = fabs(period_min - GEO_PERIOD_MIN) <= GEO_PERIOD_TOL_MIN;
    bool low_incl    = incl_deg <= GEO_INCL_TOL_DEG;
    bool low_ecc     = ecc <= GEO_ECC_TOL;

    if (!in_geo_band)  return GEO_NOT;
    if (!low_ecc)      return GEO_NOT_ECCENTRIC;
    if (!low_incl)     return GEO_NOT_INCLINED;
    return GEO_YES;
}

/* -- Sub-satellite point --------------------------------------------------- */
/* Returns false if the SGP4 position is invalid (NaN / decayed). */
static bool sub_satellite_point(Satellite *sat, double unix, double gmst_deg,
                                double *out_lat, double *out_lon, double *out_r)
{
    Vector3 p = calculate_position(sat, unix);
    if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z))
        return false;

    double r = sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (r < 0.001)
        return false;

    double lat = asin(p.y / r) * RAD2DEG;
    double lon = atan2(-p.z, p.x) * RAD2DEG - gmst_deg;
    while (lon < -180.0) lon += 360.0;
    while (lon >  180.0) lon -= 360.0;

    *out_lat = lat;
    *out_lon = lon;
    *out_r   = r;
    return true;
}

void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg)
{
    if (!*ctx->selected_sat)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                           "No satellite selected.\nClick a satellite in the 3D view or in the Satellite Manager.");
        ImGui::PopTextWrapPos();
        return;
    }

    Satellite *sat = *ctx->selected_sat;
    Location *home = GetHomeLocation();
    if (!home)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ThemeColor(g_theme.ui.warning),
                           "No home location is configured. Add or select a Home location in Settings.");
        ImGui::PopTextWrapPos();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);

    /* -- Observer geometry (home location) --------------------------------- */
    Vector3 obs_eci = calc_observer_eci(home, ctx->gmst_deg);
    double current_unix = get_unix_from_epoch(*ctx->current_epoch);

    double topo_dec = 0.0, topo_ra = 0.0;
    calc_topocentric_radec(sat->current_pos, obs_eci, &topo_dec, &topo_ra);
    double geo_dec = 0.0, geo_ra = 0.0;
    calc_geocentric_radec(sat->current_pos, &geo_dec, &geo_ra);
    double ang_speed = calc_topocentric_ang_speed(sat, current_unix, obs_eci);

    double az = 0.0, el = 0.0;
    get_az_el(sat->current_pos, ctx->gmst_deg,
              home->lat, home->lon, home->alt,
              &az, &el);
    double range = get_sat_range(sat, *ctx->current_epoch, *home);

    double pos_r = Vector3Length(sat->current_pos);
    double sat_lat = 0.0, sat_lon = 0.0;
    if (pos_r > 0.001)
    {
        sat_lat = asin(sat->current_pos.y / pos_r) * RAD2DEG;
        double lon_rad = atan2(-sat->current_pos.z, sat->current_pos.x)
                         - ctx->gmst_deg * DEG2RAD;
        sat_lon = lon_rad * RAD2DEG;
        while (sat_lon < -180.0) sat_lon += 360.0;
        while (sat_lon > 180.0)  sat_lon -= 360.0;
        if (sat_lat > 90.0)  sat_lat -= 180.0;
        if (sat_lat < -90.0) sat_lat += 180.0;
    }

    // derived orbital quantities + GEO classification
    SatPropElements elem;
    sat_prop_elements(sat, current_unix, &elem);
    double n_revday   = elem.mean_motion * 1440.0 / (2.0 * PI);
    double period_min = (n_revday > 0.0) ? (1440.0 / n_revday) : 0.0;
    double a_km       = elem.sma_km;
    double incl_deg   = elem.incl * RAD2DEG;
    double ecc        = elem.ecc;

    // tiered detection: GEO band, then inclination/eccentricity limits
    bool is_geo = (n_revday > 0.0 && a_km > 0.0 &&
                   classify_geo(period_min, incl_deg, ecc) == GEO_YES);

    /* -- Basic info -------------------------------------------------------- */
    if (ImGui::BeginTable("##sat_basic", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        InfoRow("Name", "%s", sat->name);
        InfoRow("NORAD", "%s", sat->norad_id);
        if (!is_geo)
        {
            InfoRow("Latitude", "%.4f\xc2\xb0", sat_lat);
            InfoRow("Longitude", "%.4f\xc2\xb0", sat_lon);
            InfoRow("Period", "%.2f min", period_min);
        }
        InfoRow("Apogee", "%.1f km", calc_apogee_km(sat, current_unix));
        InfoRow("Perigee", "%.1f km", calc_perigee_km(sat, current_unix));
        ImGui::EndTable();
    }

    /* -- GEO Analysis (only for geostationary satellites) ------------------ */
    if (is_geo)
    {
        double now_unix = get_unix_from_epoch(*ctx->current_epoch);
        double sub_lat = 0.0, sub_lon = 0.0, radius_km = 0.0;
        bool have_pos = sub_satellite_point(sat, now_unix, ctx->gmst_deg,
                                            &sub_lat, &sub_lon, &radius_km);

        /* Drift relative to the sidereal rotation rate. */
        double drift_deg_day = n_revday * 360.0 - SIDEREAL_DEG_PER_DAY;
        const char *drift_dir = (drift_deg_day > GEO_DRIFT_EPS) ? "East"
                               : (drift_deg_day < -GEO_DRIFT_EPS) ? "West"
                               : "On station";
        double drift_km_day = drift_deg_day * (PI / 180.0) * GEO_RADIUS_KM;

        /* Smooth, slow accent pulse on the dropdown header. */
        float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * (2.0f * (float)PI / 2.6f));
        ImVec4 normal_col = ThemeColor(g_theme.ui.text);
        ImVec4 accent_col = ThemeColor(g_theme.ui.accent);
        ImVec4 header_col(normal_col.x + (accent_col.x - normal_col.x) * pulse,
                          normal_col.y + (accent_col.y - normal_col.y) * pulse,
                          normal_col.z + (accent_col.z - normal_col.z) * pulse,
                          normal_col.w + (accent_col.w - normal_col.w) * pulse);

        ImGui::Separator();
        ImGui::PushID("geo_analysis");
        ImGui::PushStyleColor(ImGuiCol_Text, header_col);
        bool geo_open = ImGui::CollapsingHeader(ICON_FA_GLOBE " GEO Analysis");
        ImGui::PopStyleColor();
        if (geo_open)
        {
            /* -- Position (sub-satellite point) ---------------------------- */
            ImGui::Text("%s Position", ICON_FA_LOCATION_DOT);
            if (have_pos && ImGui::BeginTable("##geo_pos", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                InfoRow("Lat", "%.4f\xc2\xb0", sub_lat);
                InfoRow("Lon", "%.4f\xc2\xb0", sub_lon);
                InfoRow("Radius", "%.1f km", radius_km);
                InfoRow("Alt", "%.1f km", radius_km - (double)EARTH_RADIUS_KM);
                ImGui::EndTable();
            }
            else if (!have_pos)
            {
                ImGui::TextColored(ThemeColor(g_theme.ui.warning),
                                   "SGP4 failed (decayed or invalid elements).");
            }

            /* -- Drift ---------------------------------------------------- */
            ImGui::Separator();
            ImGui::Text("%s Drift", ICON_FA_ARROWS_LEFT_RIGHT);
            if (ImGui::BeginTable("##geo_drift", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                InfoRow("Drift Dir", "%s", drift_dir);
                InfoRow("Drift \xc2\xb0/d", "%+.6f", drift_deg_day);
                InfoRow("Drift km/d", "%+.1f", drift_km_day);
                InfoRow("Period", "%.2f min", period_min);
                ImGui::EndTable();
            }

            /* -- Target longitude / time to reach ------------------------- */
            ImGui::Separator();
            ImGui::Text("%s Target Longtitude", ICON_FA_CROSSHAIRS);
            float target_lon = ToolSettingGetFloat(cfg, GEO_KEY_TARGET_LON, 0.0f);
            if (ImGui::BeginTable("##geo_target", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("Target Lon");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputFloat("##geo_target_lon", &target_lon, 0.0f, 0.0f, "%.4f\xc2\xb0"))
                    ToolSettingSetFloat(cfg, GEO_KEY_TARGET_LON, target_lon);

                /* Normalize the target longitude to +/-180. */
                double target_norm = fmod((double)target_lon + 180.0, 360.0);
                if (target_norm < 0.0) target_norm += 360.0;
                target_norm -= 180.0;

                if (!have_pos)
                {
                    InfoRow("Days", "n/a");
                }
                else
                {
                    double d = drift_deg_day;
                    double delta;
                    if (fabs(d) < GEO_DRIFT_EPS)
                    {
                        /* On station: direction is undefined, use shortest arc. */
                        delta = fabs(target_norm - sub_lon);
                        if (delta > 180.0) delta = 360.0 - delta;
                    }
                    else if (d > 0.0)
                    {
                        delta = fmod(target_norm - sub_lon + 360.0, 360.0);
                    }
                    else
                    {
                        delta = fmod(sub_lon - target_norm + 360.0, 360.0);
                    }

                    if (delta < 1e-6)
                    {
                        InfoRow("Days", "At target");
                    }
                    else if (fabs(d) < GEO_DRIFT_EPS)
                    {
                        InfoRow("Days", "On station");
                    }
                    else
                    {
                        double days = delta / fabs(d);
                        InfoRow("Days", "%.2f d", days);
                        InfoRow("Delta \xc2\xb0", "%.2f\xc2\xb0", delta);
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }

    /* -- Advanced Orbital Data -------------------------------------------- */
    ImGui::Separator();
    ImGui::PushID("sat_adv");
    if (ImGui::CollapsingHeader(ICON_FA_GEAR " Advanced Orbital Data"))
    {
        ImGui::Text("%s Orbital Elements", ICON_FA_SATELLITE);
        if (ImGui::BeginTable("##orbital_elements", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

            InfoRow("Inclination", "%.4f\xc2\xb0", elem.incl * RAD2DEG);
            InfoRow("Eccentricity", "%.6f", elem.ecc);
            InfoRow("RAAN", "%.4f\xc2\xb0", elem.raan * RAD2DEG);
            InfoRow("Arg of Perigee", "%.4f\xc2\xb0", elem.argp * RAD2DEG);
            InfoRow("Mean Anomaly", "%.4f\xc2\xb0", elem.mean_anom * RAD2DEG);
            InfoRow("Mean Motion", "%.6f rev/day", n_revday);
            InfoRow("Semi-major Axis", "%.3f km", a_km);
            InfoRow("B* Drag", "%.4e", sat->bstar);

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("%s Sky Position", ICON_FA_BINOCULARS);
        if (ImGui::BeginTable("##sky_position", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

            /* Geocentric (J2000) row */
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Geocentric:");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("  RA");
            ImGui::TableNextColumn();
            DrawClickableRADec("", geo_ra, &g_ui.ra_format, true);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("  Dec");
            ImGui::TableNextColumn();
            DrawClickableRADec("", geo_dec, &g_ui.dec_format, false);

            /* Topocentric (from home) row */
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "Topocentric:");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("  RA");
            ImGui::TableNextColumn();
            DrawClickableRADec("", topo_ra, &g_ui.ra_format, true);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("  Dec");
            ImGui::TableNextColumn();
            DrawClickableRADec("", topo_dec, &g_ui.dec_format, false);

            /* Angular speed */
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("Ang. Speed");
            ImGui::TableNextColumn();
            ImGui::Text("%.4f\xc2\xb0/s", ang_speed);

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("%s From Home Location", ICON_FA_HOUSE);
        if (ImGui::BeginTable("##home_location", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

            InfoRow("Azimuth", "%.2f\xc2\xb0", az);
            InfoRow("Elevation", "%.2f\xc2\xb0", el);
            InfoRow("Range", "%.1f km", range);

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("%s ECI Position:", ICON_FA_CUBE);
        if (ImGui::BeginTable("##eci_pos", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

            InfoRow("X", "%.2f km", sat->current_pos.x);
            InfoRow("Y", "%.2f km", sat->current_pos.y);
            InfoRow("Z", "%.2f km", sat->current_pos.z);
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("%s Epoch:", ICON_FA_CALENDAR_DAYS);
        ImGui::SameLine();
        char epoch_str[64];
        epoch_to_datetime_str(sat->epoch_days, epoch_str);
        ImGui::TextWrapped("%s", epoch_str);

        if (sat->data_meta.format != FORMAT_UNKNOWN)
        {
            ImGui::Separator();
            ImGui::Text("%s Data Source", ICON_FA_DATABASE);
            if (ImGui::BeginTable("##data_source", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

                InfoRow("Source", "%s", sat->data_meta.source_name);

                const char *fmt_str = "Unknown";
                switch (sat->data_meta.format)
                {
                    case FORMAT_TLE:      fmt_str = "TLE";       break;
                    case FORMAT_OMM_JSON: fmt_str = "OMM JSON";  break;
                    case FORMAT_OMM_CSV:  fmt_str = "OMM CSV";   break;
                    case FORMAT_OMM_XML:  fmt_str = "OMM XML";   break;
                    case FORMAT_OMM_KVN:  fmt_str = "OMM KVN";   break;
                    default: break;
                }
                InfoRow("Format", "%s", fmt_str);

                ImGui::EndTable();
            }
        }
    }
    ImGui::PopID();

    ImGui::PopTextWrapPos();
}
