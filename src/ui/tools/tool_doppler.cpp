/*
 * tool_doppler.cpp - Doppler Analysis panel

#include "tools.h"
#include "tools_common.h"
#include "core/astro.h"
#include "core/config.h"
#include "core/theme.h"
#include "core/location.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include "imgui.h"
#include "IconsFontAwesome6.h"

#include "ui/notifications.h"

/* ---- analysis window sampling ------------------------------------------- */

#define DOPPLER_SAMPLES 128   /* samples across the AOS..LOS window */

typedef struct {
    bool            valid;      /* computed for the current target */
    Satellite      *sat;        /* satellite the cache belongs to     */
    double          base_freq;  /* Hz the cache was computed at       */
    double          aos_epoch;  /* window start (epoch days)          */
    double          los_epoch;  /* window end   (epoch days)          */
    double          freq[DOPPLER_SAMPLES]; /* absolute freq (Hz)      */
    double          off[DOPPLER_SAMPLES];  /* shift  freq (Hz)        */
    float           plot[DOPPLER_SAMPLES]; /* float copy for graph    */
} DopplerCache;

static DopplerCache g_cache;

/* ---- frequency presets ---------------------------------------------------- */

typedef struct { const char *label; double freq_hz; } BandPreset;

static const BandPreset s_bands[] = {
    { "2m",        145800000.0 },
    { "70cm",      435000000.0 },
    { "23cm",      1268000000.0 },
    { "13cm",      2400000000.0 },
    { "5cm",       5650000000.0 },
};

static void RebuildCache(Satellite *sat, double aos_epoch, double los_epoch,
                         double base_freq, const Location &obs)
{
    g_cache.valid     = true;
    g_cache.sat       = sat;
    g_cache.base_freq = base_freq;
    g_cache.aos_epoch = aos_epoch;
    g_cache.los_epoch = los_epoch;

    double span = (los_epoch > aos_epoch) ? (los_epoch - aos_epoch) : 0.0;
    for (int i = 0; i < DOPPLER_SAMPLES; i++)
    {
        double t = aos_epoch + span * (span > 0.0 ? (double)i / (double)(DOPPLER_SAMPLES - 1) : 0.0);
        double f = (span > 0.0) ? calculate_doppler_freq(sat, t, obs, base_freq) : base_freq;
        g_cache.freq[i] = f;
        g_cache.off[i]  = f - base_freq;
        g_cache.plot[i] = (float)(f - base_freq); /* offset in Hz, S-curve view */
    }
}

void DrawPanelDoppler(UIContext *ctx, AppConfig *cfg)
{
    (void)cfg;
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushTextWrapPos(0.0f);

    /* ---- target: pass handed off from the Passes panel ------------------- */
    Satellite *target = g_ui.locked_pass_sat;
    double win_aos = g_ui.locked_pass_aos;
    double win_los = g_ui.locked_pass_los;

    if (target)
    {
        char aos_str[64], los_str[64];
        epoch_to_datetime_str(win_aos, aos_str);
        epoch_to_datetime_str(win_los, los_str);
        ImGui::TextColored(ThemeColor(g_theme.ui.accent), "%s %s", ICON_FA_TOWER_BROADCAST, target->name);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "AOS: %s", aos_str);
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim), "LOS: %s", los_str);

        if (ImGui::Button(ICON_FA_XMARK " Release Lock", ImVec2(avail_w, 0)))
        {
            g_ui.locked_pass_sat = NULL;
            g_ui.selected_pass_idx = -1;
        }
        ImGui::Separator();
    }
    else
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.text_dim),
                           "%s No pass selected.", ICON_FA_CIRCLE_QUESTION);
        ImGui::TextDisabled("Right-click a pass row and pick \"Analyze in Doppler\"");
        ImGui::Separator();
    }

    /* ---- frequency input + presets --------------------------------------- */
    static double freq = 145800000.0;       /* base frequency (Hz) */
    ImGui::SetNextItemWidth(avail_w);
    if (ImGui::InputDouble("Frequency (Hz)", &freq, 1000.0, 1000000.0, "%.0f"))
    {
        if (freq < 1.0) freq = 1.0;
    }

    /* preset band buttons wrap to fit the sidebar width */
    float btn_w = (avail_w - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    for (int i = 0; i < 6; i++)
    {
        if (i == 3) { ImGui::NewLine(); }           /* wrap after the 3rd button */
        if (i < 5)
        {
            const BandPreset &b = s_bands[i];
            if (ImGui::Button(b.label, ImVec2(btn_w, 0)))
                freq = b.freq_hz;
            if (i % 3 != 2 && i < 4) ImGui::SameLine();
        }
        else
        {
        }
    }
    ImGui::NewLine();

    if (!target)
    {
        ImGui::PopTextWrapPos();
        return;
    }

    /* ---- observer location ------------------------------------------------ */
    Location *home = GetHomeLocation();
    if (!home)
    {
        ImGui::TextColored(ThemeColor(g_theme.ui.warning),
                           "No home location configured. Set one in Settings.");
        ImGui::PopTextWrapPos();
        return;
    }

    /* ---- rebuild the frequency profile only when inputs change ------------ */
    if (!g_cache.valid || g_cache.sat != target ||
        g_cache.base_freq != freq ||
        g_cache.aos_epoch != win_aos || g_cache.los_epoch != win_los)
    {
        RebuildCache(target, win_aos, win_los, freq, *home);
    }

    /* ---- summary & graph (offset in Hz, or kHz/MHz for readability) -------- */
    double max_off = 0.0;
    for (int i = 0; i < DOPPLER_SAMPLES; i++)
        if (fabs(g_cache.off[i]) > max_off) max_off = fabs(g_cache.off[i]);

    /* pick display units for the offset magnitude */
    double off_scale = (max_off >= 1e6) ? 1e6 : (max_off >= 1e3 ? 1e3 : 1.0);
    const char *off_unit = (max_off >= 1e6) ? "MHz" : (max_off >= 1e3 ? "kHz" : "Hz");

    ImGui::Separator();
    ImGui::TextDisabled("Pass Doppler profile  (base %.0f Hz)", freq);
    ImGui::Text("Max offset: %+.2f %s", max_off / off_scale, off_unit);

    /* ---- graph -------------------------------------------------------------
     * Auto-scaled offset (S-curve) plot. The samples are static (cached), so
     * this is cheap each frame. */
    float plot_h = 110.0f;
    float graph_scale = (max_off > 0.0) ? (float)max_off : 1.0f;
    ImGui::PlotLines("##doppler_offset", g_cache.plot, DOPPLER_SAMPLES, 0,
                     "Offset (Hz)", -graph_scale, graph_scale,
                     ImVec2(avail_w, plot_h));

    /* ---- live readout at current simulation time -------------------------- */
    double now = *ctx->current_epoch;
    ImGui::TextDisabled("Current simulation time");
    if (now >= win_aos && now <= win_los)
    {
        double f = calculate_doppler_freq(target, now, *home, freq);
        double off = f - freq;
        char tm[64];
        epoch_to_time_str(now, tm);
        ImGui::Text("t=%s  %+.1f Hz", tm, off);
    }
    else
    {
        char tm[64];
        epoch_to_time_str(now, tm);
        ImGui::TextDisabled("t=%s (outside AOS..LOS window)", tm);
    }

    /* ---- CSV export -------------------------------------------------------- */
    ImGui::Separator();
    if (ImGui::Button(ICON_FA_FILE_CSV " Export CSV", ImVec2(avail_w, 0)))
    {
        const char *path = "doppler_export.csv";
        FILE *f = fopen(path, "w");
        if (f)
        {
            fprintf(f, "elapsed_s,epoch_days,freq_hz,offset_hz\n");
            for (int i = 0; i < DOPPLER_SAMPLES; i++)
            {
                double t = g_cache.aos_epoch + (g_cache.los_epoch - g_cache.aos_epoch) *
                             (double)i / (double)(DOPPLER_SAMPLES - 1);
                fprintf(f, "%.3f,%.9f,%.3f,%+.3f\n",
                        (t - g_cache.aos_epoch) * 86400.0,
                        t, g_cache.freq[i], g_cache.off[i]);
            }
            fclose(f);
            NotifyPush(NOTIFY_SUCCESS, ICON_FA_FILE_CSV,
                       "Doppler data saved to %s", path);
        }
        else
        {
            NotifyPush(NOTIFY_ERROR, ICON_FA_TRIANGLE_EXCLAMATION,
                       "Could not write %s", path);
        }
    }

    ImGui::PopTextWrapPos();
}