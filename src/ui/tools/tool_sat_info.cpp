/*
 * tool_sat_info.cpp - Satellite Info (inspector) panel
 */

#include "tools.h"
#include "tools_common.h"
#include "tools_registry.h"
#include "core/astro.h"
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
    Vector3 p0 = calculate_position(sat, current_unix);
    Vector3 p1 = calculate_position(sat, current_unix + 1.0);

    double x0 = p0.x - obs_eci.x, y0 = p0.y - obs_eci.y, z0 = p0.z - obs_eci.z;
    double x1 = p1.x - obs_eci.x, y1 = p1.y - obs_eci.y, z1 = p1.z - obs_eci.z;

    double r0 = sqrt(x0 * x0 + y0 * y0 + z0 * z0);
    double r1 = sqrt(x1 * x1 + y1 * y1 + z1 * z1);
    if (r0 < 0.001 || r1 < 0.001) return 0.0;

    double cos_a = (x0 * x1 + y0 * y1 + z0 * z1) / (r0 * r1);
    if (cos_a > 1.0) cos_a = 1.0;
    if (cos_a < -1.0) cos_a = -1.0;

    return acos(cos_a) * RAD2DEG;   /* degrees per second */
}

void DrawPanelSatInfo(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (!*ctx->selected_sat)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary),
                           "No satellite selected.\nClick a satellite in the 3D view or in the Satellite Manager.");
        ImGui::PopTextWrapPos();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);

    Satellite *sat = *ctx->selected_sat;

    /* -- Observer geometry (home location) --------------------------------- */
    Vector3 obs_eci = calc_observer_eci(GetHomeLocation(), ctx->gmst_deg);
    double current_unix = get_unix_from_epoch(*ctx->current_epoch);

    double topo_dec = 0.0, topo_ra = 0.0;
    calc_topocentric_radec(sat->current_pos, obs_eci, &topo_dec, &topo_ra);
    double geo_dec = 0.0, geo_ra = 0.0;
    calc_geocentric_radec(sat->current_pos, &geo_dec, &geo_ra);
    double ang_speed = calc_topocentric_ang_speed(sat, current_unix, obs_eci);

    /* -- Header info (NORAD, name, active) --------------------------------- */
    if (ImGui::BeginTable("##sat_header", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        InfoRow("Name:", "%s", sat->name);
        InfoRow("NORAD:", "%s", sat->norad_id);
        InfoRow("Active:", "%s", sat->is_active ? "Yes" : "No");
        ImGui::EndTable();
    }

    /* -- Orbital Elements -------------------------------------------------- */
    ImGui::Separator();
    ImGui::Text("%s Orbital Elements", ICON_FA_SATELLITE);
    if (ImGui::BeginTable("##orbital_elements", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        InfoRow("Inclination", "%.4f\xc2\xb0", sat->inclination * RAD2DEG);
        InfoRow("Eccentricity", "%.6f", sat->eccentricity);
        InfoRow("Apogee", "%.1f km", calc_apogee_km(sat));
        InfoRow("Perigee", "%.1f km", calc_perigee_km(sat));
        ImGui::EndTable();
    }

    /* -- Sky Position ------------------------------------------------------ */
    ImGui::Separator();
    ImGui::Text("%s Sky Position", ICON_FA_BINOCULARS);
    if (ImGui::BeginTable("##sky_position", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        /* Geocentric (J2000) row */
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "Geocentric:");
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
        ImGui::TextColored(ThemeColor(g_theme.ui.text_secondary), "Topocentric:");
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

    /* -- From Home Location ------------------------------------------------ */
    ImGui::Separator();
    ImGui::Text("%s From Home Location", ICON_FA_HOUSE);
    if (ImGui::BeginTable("##home_location", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        double az = 0.0, el = 0.0;
        get_az_el(sat->current_pos, ctx->gmst_deg,
                  GetHomeLocation()->lat, GetHomeLocation()->lon, GetHomeLocation()->alt,
                  &az, &el);
        InfoRow("Azimuth", "%.2f\xc2\xb0", az);
        InfoRow("Elevation", "%.2f\xc2\xb0", el);

        double range = get_sat_range(sat, *ctx->current_epoch, *GetHomeLocation());
        InfoRow("Range", "%.1f km", range);

        ImGui::EndTable();
    }

    /* -- Advanced Orbital Data -------------------------------------------- */
    ImGui::Separator();
    ImGui::PushID("sat_adv");
    if (ImGui::CollapsingHeader(ICON_FA_GEAR " Advanced Orbital Data"))
    {
        if (ImGui::BeginTable("##advanced", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

            InfoRow("RAAN", "%.4f\xc2\xb0", sat->raan * RAD2DEG);
            InfoRow("Arg of Perigee", "%.4f\xc2\xb0", sat->arg_perigee * RAD2DEG);
            InfoRow("Mean Anomaly", "%.4f\xc2\xb0", sat->mean_anomaly * RAD2DEG);
            InfoRow("Mean Motion", "%.6f rev/day", sat->mean_motion * 86400.0 / (2.0 * PI));
            InfoRow("Semi-major Axis", "%.3f km", sat->semi_major_axis);
            InfoRow("B* Drag", "%.4e", sat->bstar);
            InfoRow("Period", "%.2f min", (2.0 * PI / sat->mean_motion) / 60.0);

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

    /* -- Activate / Deactivate -------------------------------------------- */
    ImGui::Separator();
    if (sat->is_active && ImGui::Button("Deactivate", ImVec2(avail_w, 0)))
    {
        sat->is_active = false;
        SaveSatSelection(cfg);
        SaveAppConfig("settings.json", cfg);
    }
    else if (!sat->is_active && ImGui::Button("Activate", ImVec2(avail_w, 0)))
    {
        sat->is_active = true;
        SaveSatSelection(cfg);
        SaveAppConfig("settings.json", cfg);
    }

    ImGui::PopTextWrapPos();
}
