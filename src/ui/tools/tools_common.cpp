/*
 * tools_common.cpp - Shared helpers and state for tool panels
 *
 * Contains the data-source selection (shopping-cart) persistence and the
 * small UI helpers (InfoRow, RA/Dec formatting, case-insensitive search)
 * that several tool panels rely on.
 */

#include "tools_common.h"
#include "core/theme.h"
#include "util/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <cmath>

#include <raylib.h>

#include "imgui.h"

/* -- Shared state ---------------------------------------------------------- */

bool log_auto_scroll = true;
bool log_show_timestamps = false;  /* timestamps hidden by default (cleaner) */

/* -- Helpers --------------------------------------------------------------- */

ImVec4 ThemeColor(const Color &c)
{
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

/** case-insensitive substring search; returns true if substr is found in str */
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

/* -- Data Source Selection List (shopping-cart model) ---------------------- */

static DataSourceSelection g_data_selections[MAX_DATA_SOURCE_SELECTIONS];
static int g_data_selection_count = 0;

/** helper: check if a selection already exists (type + identifier match) */
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

/** helper: add a selection, returns true if added */
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

/** helper: remove a selection by index */
static void selection_remove(int idx)
{
    if (idx < 0 || idx >= g_data_selection_count) return;
    for (int i = idx; i < g_data_selection_count - 1; i++)
        g_data_selections[i] = g_data_selections[i + 1];
    g_data_selection_count--;
}

/* -- Public accessors (used by the Data Sources tool) ---------------------- */

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

/* -- Data source selection persistence (section 11) ------------------------- */

/** persist the shopping-cart selections to data_selections.json */
void SaveDataSelections(void)
{
    FILE *f = fopen("data_selections.json", "w");
    if (!f)
    {
        LOG_ERROR("Failed to save data selections to data_selections.json");
        return;
    }
    LOG_INFO("Saving %d data source selections", g_data_selection_count);

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"selection_count\": %d,\n", g_data_selection_count);
    fprintf(f, "  \"selections\": [\n");

    for (int i = 0; i < g_data_selection_count; i++)
    {
        DataSourceSelection *s = &g_data_selections[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"type\": %d,\n", (int)s->type);
        fprintf(f, "      \"name\": \"%s\",\n", s->name);
        fprintf(f, "      \"identifier\": \"%s\",\n", s->identifier);
        fprintf(f, "      \"paste_data\": \"%s\",\n", s->paste_data);
        fprintf(f, "      \"format\": %d\n", (int)s->format);
        fprintf(f, "    }%s\n", (i == g_data_selection_count - 1) ? "" : ",");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
}

/** restore the shopping-cart selections from data_selections.json */
void LoadDataSelections(void)
{
    g_data_selection_count = 0;

    if (!FileExists("data_selections.json"))
        return;

    char *text = LoadFileText("data_selections.json");
    if (!text)
        return;

    const char *array_start = strstr(text, "\"selections\"");
    if (!array_start)
    {
        UnloadFileText(text);
        return;
    }

    const char *ptr = strchr(array_start, '[');
    if (!ptr)
    {
        UnloadFileText(text);
        return;
    }

    int brace_depth = 0;
    int obj_start = -1;

    while (*ptr && g_data_selection_count < MAX_DATA_SOURCE_SELECTIONS)
    {
        if (*ptr == '{')
        {
            if (brace_depth == 0) obj_start = (int)(ptr - text);
            brace_depth++;
        }
        else if (*ptr == '}')
        {
            brace_depth--;
            if (brace_depth == 0 && obj_start >= 0)
            {
                int obj_len = (int)(ptr - text) - obj_start + 1;
                char *obj_text = (char*)malloc(obj_len + 1);
                if (obj_text)
                {
                    strncpy(obj_text, text + obj_start, obj_len);
                    obj_text[obj_len] = '\0';

                    DataSourceSelection *s = &g_data_selections[g_data_selection_count];
                    memset(s, 0, sizeof(DataSourceSelection));

                    /* type */
                    const char *v = strstr(obj_text, "\"type\"");
                    if (v) { v = strchr(v, ':'); if (v) s->type = (SourceType)atoi(v + 1); }

                    /* name */
                    v = strstr(obj_text, "\"name\"");
                    if (v) { v = strchr(v, ':'); if (v) { v = strchr(v, '"'); if (v) sscanf(v + 1, "%63[^\"]", s->name); } }

                    /* identifier */
                    v = strstr(obj_text, "\"identifier\"");
                    if (v) { v = strchr(v, ':'); if (v) { v = strchr(v, '"'); if (v) sscanf(v + 1, "%63[^\"]", s->identifier); } }

                    /* paste_data */
                    v = strstr(obj_text, "\"paste_data\"");
                    if (v) { v = strchr(v, ':'); if (v) { v = strchr(v, '"'); if (v) { int i = 0; v++; while (*v && *v != '"' && i < 4095) s->paste_data[i++] = *v++; s->paste_data[i] = '\0'; } } }

                    /* format */
                    v = strstr(obj_text, "\"format\"");
                    if (v) { v = strchr(v, ':'); if (v) s->format = (OrbitalDataFormat)atoi(v + 1); }

                    free(obj_text);
                    g_data_selection_count++;
                }
                obj_start = -1;
            }
        }
        else if (*ptr == ']' && brace_depth == 0)
        {
            break;
        }
        ptr++;
    }

    UnloadFileText(text);
    LOG_INFO("Loaded %d data source selections", g_data_selection_count);
}

/* -- RA/Dec format helpers ------------------------------------------------- */

/** convert degrees to hours:minutes:seconds (RA) */
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

/** convert degrees to degrees:arcminutes:arcseconds (Dec) */
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

/** format RA degrees into a HMS string buffer; returns the buffer */
static const char *format_ra_str(double ra_deg, int format)
{
    static char buf[48];
    if (format == 0)
    {
        /* decimal degrees */
        snprintf(buf, sizeof(buf), "%.4f\xc2\xb0", ra_deg);
    }
    else
    {
        /* hours:minutes:seconds */
        int h, m;
        double s;
        deg_to_hms(ra_deg, &h, &m, &s);
        snprintf(buf, sizeof(buf), "%02dh %02dm %05.2fs", h, m, s);
    }
    return buf;
}

/** format Dec degrees into a DMS string buffer; returns the buffer */
static const char *format_dec_str(double dec_deg, int format)
{
    static char buf[48];
    if (format == 0)
    {
        /* decimal degrees */
        snprintf(buf, sizeof(buf), "%+.4f\xc2\xb0", dec_deg);
    }
    else
    {
        /* degrees:arcminutes:arcseconds */
        int d, m;
        double s;
        deg_to_dms(dec_deg, &d, &m, &s);
        char sign = (d < 0 || dec_deg < 0.0) ? '-' : '+';
        int ad = (d < 0) ? -d : d;
        snprintf(buf, sizeof(buf), "%c%02d\xc2\xb0 %02d' %05.2f\"", sign, ad, m, s);
    }
    return buf;
}

/** draw a clickable RA or Dec value that cycles format on click */
void DrawClickableRADec(const char *label, double deg_value,
                        int *format_var, bool is_ra)
{
    ImGui::Text("%s", label);
    ImGui::SameLine();

    /* build the formatted string */
    const char *val_str = is_ra ? format_ra_str(deg_value, *format_var)
                                : format_dec_str(deg_value, *format_var);

    ImGui::TextUnformatted(val_str);

    if (ImGui::IsItemHovered())
    {
        /* highlight on hover to indicate clickability */
        ImGui::SetTooltip("Click to switch format");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            *format_var = (*format_var + 1) % 2;
    }
}

/** helper: draw a two-column table row (label | value) */
void InfoRow(const char *label, const char *fmt, ...)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s", label);
    ImGui::TableNextColumn();
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}
