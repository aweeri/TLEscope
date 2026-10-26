// tools_common.cpp - Shared helpers and state for tool panels
//
// Contains the data-source selection (shopping-cart) persistence and the
// small UI helpers (InfoRow, RA/Dec formatting, case-insensitive search)
// that several tool panels rely on.

#include "tools_common.h"
#include "core/theme.h"
#include "data/async_fetch.h"
#include "data/storage.h"
#include "ui/notifications.h"
#include "util/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <cmath>

#include <raylib.h>
#include <nlohmann/json.hpp>

#include "imgui.h"
#include "IconsFontAwesome6.h"

// -- Shared state -----------------------------------------------------------

bool log_auto_scroll = true;
bool log_show_timestamps = false;  // timestamps hidden by default (cleaner)

// -- Helpers ----------------------------------------------------------------

ImVec4 ThemeColor(const Color &c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

// case-insensitive substring search; returns true if substr is found in str
bool str_contains_ic(const char *str, const char *substr)
{
    if (!str || !substr) return false;
    if (*substr == '\0') return true;
    while (*str)
    {
        const char *a = str;
        const char *b = substr;
        while (*a && *b && (tolower((unsigned char)*a) == tolower((unsigned char)*b)))
        { a++; b++; }
        if (*b == '\0') return true;
        str++;
    }
    return false;
}

// -- Orbit direction-of-motion styling --------------------------------------

// wrap a phase into [-0.5, 0.5] (shortest signed distance around the loop)
static double orbit_dir_wrap(double p)
{
    return p - floor(p + 0.5);
}

float OrbitDirectionIntensity(double phase, bool animated, double anim_time, float floor)
{
    const double s = orbit_dir_wrap(phase);
    double g; // 1 at the satellite, decaying with distance along the orbit

    if (animated)
    {
        // a bright comet sweeps the orbit: sharp head at the pulse, tail trailing behind
        const double pulse = anim_time * ORBIT_DIR_ANIM_SPEED;
        const double d = orbit_dir_wrap(s - pulse); // signed distance from the pulse
        g = (d > 0.0) ? 0.0 : exp(d / ORBIT_DIR_ANIM_TAU);
    }
    else
    {
        // static ramp: future (s>0) stays at the base level; the trail (s<0) fades over ORBIT_DIR_TRAIL_LEN with a C1-continuous smoothstep
        if (s >= 0.0)
        {
            g = 0.0;
        }
        else
        {
            const double x = -s / ORBIT_DIR_TRAIL_LEN; // 0 at satellite, 1 at trail end
            g = (x >= 1.0) ? 0.0 : 1.0 - x * x * (3.0 - 2.0 * x);
        }
    }

    return floor + (1.0f - floor) * (float)g;
}

float OrbitDirectionWidth(double phase, bool animated, double anim_time, float floor)
{
    const float t = (OrbitDirectionIntensity(phase, animated, anim_time, floor) - floor) /
                    (1.0f - floor);
    return 1.0f + t * (ORBIT_DIR_WIDTH_MAX - 1.0f);
}

// -- Data Source Selection List (shopping-cart model) -----------------------

static DataSourceSelection g_data_selections[MAX_DATA_SOURCE_SELECTIONS];
static int g_data_selection_count = 0;

// helper: check if a selection already exists (type + identifier match)
static bool selection_exists(SourceType type, const char *identifier)
{
    for (int i = 0; i < g_data_selection_count; i++)
    {
        if (g_data_selections[i].type == type &&
            strcmp(g_data_selections[i].identifier, identifier) == 0)
            return true;
    }
    return false;
}

// helper: add a selection, returns true if added
static bool selection_add(SourceType type, const char *name, const char *identifier,
                          const char *paste_data, OrbitalDataFormat format)
{
    if (g_data_selection_count >= MAX_DATA_SOURCE_SELECTIONS) return false;
    if (selection_exists(type, identifier)) return false;

    DataSourceSelection *s = &g_data_selections[g_data_selection_count++];
    s->type = type;
    strncpy(s->name, name, sizeof(s->name) - 1);
    strncpy(s->identifier, identifier, sizeof(s->identifier) - 1);
    if (paste_data)
        strncpy(s->paste_data, paste_data, sizeof(s->paste_data) - 1);
    else
        s->paste_data[0] = '\0';
    s->format = format;
    return true;
}

// helper: remove a selection by index
static void selection_remove(int idx)
{
    if (idx < 0 || idx >= g_data_selection_count) return;
    for (int i = idx; i < g_data_selection_count - 1; i++)
        g_data_selections[i] = g_data_selections[i + 1];
    g_data_selection_count--;
}

// -- Public accessors (used by the Data Sources tool) -----------------------

int DataSelectionCount(void) { return g_data_selection_count; }
DataSourceSelection *DataSelectionAt(int idx)
{
    return (idx >= 0 && idx < g_data_selection_count) ? &g_data_selections[idx] : NULL;
}
bool DataSelectionAdd(SourceType type, const char *name, const char *identifier,
                      const char *paste_data, OrbitalDataFormat format)
{
    return selection_add(type, name, identifier, paste_data, format);
}
void DataSelectionRemove(int idx) { selection_remove(idx); }

// -- Data source pull orchestration -----------------------------------------
// job-build/submit is centralized here so the panel button and the staleness auto-pull/prompt share one path

static bool g_pull_running = false;  // a pull started via DataSourcesStartPull()
static int g_pull_total = 0;         // number of sources in that pull
static bool g_pull_auto = false;     // the in-flight pull was auto-triggered

bool DataSourcesPullInProgress(void) { return g_pull_running; }
int DataSourcesPullTotal(void) { return g_pull_total; }
void DataSourcesSetAutoPull(bool is_auto) { g_pull_auto = is_auto; }

bool DataSourcesStartPull(AppConfig *cfg, UIContext *ctx)
{
    int count = DataSelectionCount();
    if (count == 0)
    {
        LOG_INFO("No sources selected to pull");
        return false;
    }

    /* never start a second pull while one is already in flight */
    if (AsyncFetchBusy())
        return false;

    g_pull_total = count;
    g_pull_auto = false;

    /* Snapshot the active NORAD ids before replacing the catalogue. */
    SaveSatSelection(cfg);
    if (ctx && ctx->selected_sat)
        *ctx->selected_sat = NULL;

    sat_count = 0;
    g_pull_running = true;

    for (int i = 0; i < g_pull_total; i++)
    {
        DataSourceSelection *s = DataSelectionAt(i);
        if (!s) break;

        AsyncFetchJob job;
        memset(&job, 0, sizeof(job));
        job.type = (AsyncJobType)s->type;
        strncpy(job.name, s->name, sizeof(job.name) - 1);
        strncpy(job.identifier, s->identifier, sizeof(job.identifier) - 1);
        strncpy(job.paste_data, s->paste_data, sizeof(job.paste_data) - 1);
        job.format = s->format;

        AsyncFetchSubmit(&job);
    }

    return true;
}

void DataSourcesPollPullCompletion(AppConfig *cfg)
{
    if (!g_pull_running)
        return;

    /* Wait until both worker jobs and queued results are drained. */
    if (AsyncFetchBusy() || AsyncFetchHasResults())
        return;

    // fetched records start inactive; restore only NORAD ids that were active before this refresh
    LoadSatSelection(cfg);
    SaveOrbitalData("data.json", satellites, sat_count);

    /* clearing the flag first guarantees the toast fires exactly once */
    g_pull_running = false;
    g_pull_total = 0;

    // distinguish success/partial/total failure so a 24/7 operator notices silent refresh failures
    int failed = AsyncFetchTakeFailureCount();
    if (sat_count <= 0)
    {
        /* nothing arrived at all: offline or every selected source failed */
        NotifyPush(NOTIFY_ERROR, ICON_FA_TRIANGLE_EXCLAMATION,
                   "Data refresh failed - no satellites received");
    }
    else if (failed > 0)
    {
        /* partial success: some sources failed but data did arrive */
        NotifyPush(NOTIFY_WARNING, ICON_FA_TRIANGLE_EXCLAMATION,
                   "Data refreshed with %d failed source(s) (%d satellites)",
                   failed, sat_count);
    }
    else if (g_pull_auto)
    {
        NotifyPush(NOTIFY_SUCCESS, ICON_FA_DOWNLOAD,
                   "Stale orbital data auto-pulled (%d satellites)", sat_count);
    }
    else
    {
        NotifyPush(NOTIFY_SUCCESS, ICON_FA_DOWNLOAD,
                   "Data sources refreshed (%d satellites)", sat_count);
    }
    g_pull_auto = false;
}

// -- Data source selection persistence (section 11) --------------------------

// helper: copy a string value from a JSON node into a fixed-size char buffer
static void copy_str(char *dst, size_t dst_size, const std::string &src)
{
    if (!dst || dst_size == 0) return;
    size_t n = src.size();
    if (n > dst_size - 1) n = dst_size - 1;
    memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// persist the shopping-cart selections to data_selections.json
void SaveDataSelections(void)
{
    LOG_INFO("Saving %d data source selections", g_data_selection_count);

    nlohmann::json root;
    root["version"] = 1;
    root["selection_count"] = g_data_selection_count;
    nlohmann::json selections = nlohmann::json::array();
    for (int i = 0; i < g_data_selection_count; i++)
    {
        DataSourceSelection *s = &g_data_selections[i];
        nlohmann::json e;
        e["type"] = (int)s->type;
        e["name"] = s->name;
        e["identifier"] = s->identifier;
        e["paste_data"] = s->paste_data;
        e["format"] = (int)s->format;
        selections.push_back(e);
    }
    root["selections"] = selections;

    std::string text = root.dump(2);
    FILE *f = fopen("data_selections.json", "w");
    if (!f)
    {
        LOG_ERROR("Failed to save data selections to data_selections.json");
        return;
    }
    fwrite(text.data(), 1, text.size(), f);
    fwrite("\n", 1, 1, f);
    fclose(f);
}

// restore the shopping-cart selections from data_selections.json
void LoadDataSelections(void)
{
    g_data_selection_count = 0;

    if (!FileExists("data_selections.json"))
        return;

    char *text = LoadFileText("data_selections.json");
    if (!text)
        return;

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(text);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to parse data_selections.json: %s", e.what());
        UnloadFileText(text);
        return;
    }

    UnloadFileText(text);

    auto selections_it = root.find("selections");
    if (selections_it == root.end() || !selections_it->is_array())
        return;

    for (const auto &e : *selections_it)
    {
        if (g_data_selection_count >= MAX_DATA_SOURCE_SELECTIONS) break;
        if (!e.is_object()) continue;

        DataSourceSelection *s = &g_data_selections[g_data_selection_count];
        memset(s, 0, sizeof(DataSourceSelection));

        // type
        if (e.contains("type") && e["type"].is_number())
            s->type = (SourceType)e["type"].get<int>();

        // name / identifier / paste_data
        copy_str(s->name, sizeof(s->name), e.value("name", std::string()));
        copy_str(s->identifier, sizeof(s->identifier), e.value("identifier", std::string()));
        copy_str(s->paste_data, sizeof(s->paste_data), e.value("paste_data", std::string()));

        // format
        if (e.contains("format") && e["format"].is_number())
            s->format = (OrbitalDataFormat)e["format"].get<int>();

        g_data_selection_count++;
    }

    LOG_INFO("Loaded %d data source selections", g_data_selection_count);
}

// -- RA/Dec format helpers --------------------------------------------------

// convert degrees to hours:minutes:seconds (RA)
static void deg_to_hms(double deg, int *h, int *m, double *s)
{
    double hours = deg / 15.0;
    while (hours < 0.0)  hours += 24.0;
    while (hours >= 24.0) hours -= 24.0;
    *h = (int)hours;
    double rem = (hours - *h) * 60.0;
    *m = (int)rem;
    *s = (rem - *m) * 60.0;
}

// convert degrees to degrees:arcminutes:arcseconds (Dec)
static void deg_to_dms(double deg, int *d, int *m, double *s)
{
    int sign = (deg < 0.0) ? -1 : 1;
    double adeg = fabs(deg);
    *d = (int)adeg;
    double rem = (adeg - *d) * 60.0;
    *m = (int)rem;
    *s = (rem - *m) * 60.0;
    *d *= sign;
}

// format RA degrees into a HMS string buffer; returns the buffer
static const char *format_ra_str(double ra_deg, int format)
{
    static char buf[48];
    if (format == 0)
    {
        // decimal degrees
        snprintf(buf, sizeof(buf), "%.4f\xc2\xb0", ra_deg);
    }
    else
    {
        // hours:minutes:seconds
        int h, m;
        double s;
        deg_to_hms(ra_deg, &h, &m, &s);
        snprintf(buf, sizeof(buf), "%02dh %02dm %05.2fs", h, m, s);
    }
    return buf;
}

// format Dec degrees into a DMS string buffer; returns the buffer
static const char *format_dec_str(double dec_deg, int format)
{
    static char buf[48];
    if (format == 0)
    {
        // decimal degrees
        snprintf(buf, sizeof(buf), "%+.4f\xc2\xb0", dec_deg);
    }
    else
    {
        // degrees:arcminutes:arcseconds
        int d, m;
        double s;
        deg_to_dms(dec_deg, &d, &m, &s);
        char sign = (d < 0 || dec_deg < 0.0) ? '-' : '+';
        int ad = (d < 0) ? -d : d;
        snprintf(buf, sizeof(buf), "%c%02d\xc2\xb0 %02d' %05.2f\"", sign, ad, m, s);
    }
    return buf;
}

// draw a clickable RA or Dec value that cycles format on click
void DrawClickableRADec(const char *label, double deg_value,
                        int *format_var, bool is_ra)
{
    ImGui::Text("%s", label);
    ImGui::SameLine();

    // build the formatted string
    const char *val_str = is_ra ? format_ra_str(deg_value, *format_var)
                                : format_dec_str(deg_value, *format_var);

    ImGui::TextUnformatted(val_str);

    if (ImGui::IsItemHovered())
    {
        // highlight on hover to indicate clickability
        ImGui::SetTooltip("Click to switch format");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            *format_var = (*format_var + 1) % 2;
    }
}

// helper: draw a two-column table row (label | value)
void InfoRow(const char *label, const char *fmt, ...)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // wrap the label so its real rendered height reflects however many lines it needs
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped("%s", label);
    ImGui::PopTextWrapPos();
    float label_h = ImGui::GetItemRectSize().y;

    ImGui::TableNextColumn();
    // nudge the value down so it sits centered against a multiline label
    float value_h = ImGui::GetTextLineHeight();
    if (label_h > value_h)
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (label_h - value_h) * 0.5f);

    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}
