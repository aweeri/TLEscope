/* headers and defines */
#define _GNU_SOURCE
#include "ui.h"
#include "astro.h"
#include "rotator.h"
#include <ctype.h>
#include <math.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* prevent Windows API from colliding with raylib's rectangle jesus christ */
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER

/* MinGW headers require LPMSG but ignore NOUSER */
typedef struct tagMSG *LPMSG;
#endif
#include <curl/curl.h>

//TODO: explain better what in gods name happened above

#if defined(_WIN32) || defined(_WIN64)
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#include <dirent.h>
#endif

#define RAYGUI_IMPLEMENTATION
#include "../lib/raygui.h"

typedef struct
{
    const char *id;
    const char *name;
    const char *url;
} TLESource_t;

static TLESource_t SOURCES[] = {
    {"1", "Last 30 Days' Launches", "https://celestrak.org/NORAD/elements/gp.php?GROUP=last-30-days&FORMAT=tle"},
    {"2", "Space Stations", "https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle"},
    {"3", "100 Brightest", "https://celestrak.org/NORAD/elements/gp.php?GROUP=visual&FORMAT=tle"},
    {"4", "Active Satellites", "https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle"},
    {"5", "Analyst Satellites", "https://celestrak.org/NORAD/elements/gp.php?GROUP=analyst&FORMAT=tle"},
    {"6", "Russian ASAT (COSMOS 1408)", "https://celestrak.org/NORAD/elements/gp.php?GROUP=cosmos-1408-debris&FORMAT=tle"},
    {"7", "Chinese ASAT (FENGYUN 1C)", "https://celestrak.org/NORAD/elements/gp.php?GROUP=fengyun-1c-debris&FORMAT=tle"},
    {"8", "IRIDIUM 33 Debris", "https://celestrak.org/NORAD/elements/gp.php?GROUP=iridium-33-debris&FORMAT=tle"},
    {"9", "COSMOS 2251 Debris", "https://celestrak.org/NORAD/elements/gp.php?GROUP=cosmos-2251-debris&FORMAT=tle"},
    {"10", "Weather", "https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle"},
    {"11", "NOAA", "https://celestrak.org/NORAD/elements/gp.php?GROUP=noaa&FORMAT=tle"},
    {"12", "GOES", "https://celestrak.org/NORAD/elements/gp.php?GROUP=goes&FORMAT=tle"},
    {"13", "Earth Resources", "https://celestrak.org/NORAD/elements/gp.php?GROUP=resource&FORMAT=tle"},
    {"14", "SARSAT", "https://celestrak.org/NORAD/elements/gp.php?GROUP=sarsat&FORMAT=tle"},
    {"15", "Disaster Monitoring", "https://celestrak.org/NORAD/elements/gp.php?GROUP=dmc&FORMAT=tle"},
    {"16", "TDRSS", "https://celestrak.org/NORAD/elements/gp.php?GROUP=tdrss&FORMAT=tle"},
    {"17", "ARGOS", "https://celestrak.org/NORAD/elements/gp.php?GROUP=argos&FORMAT=tle"},
    {"18", "Planet", "https://celestrak.org/NORAD/elements/gp.php?GROUP=planet&FORMAT=tle"},
    {"19", "Spire", "https://celestrak.org/NORAD/elements/gp.php?GROUP=spire&FORMAT=tle"},
    {"20", "Starlink", "https://celestrak.org/NORAD/elements/gp.php?GROUP=starlink&FORMAT=tle"},
    {"21", "OneWeb", "https://celestrak.org/NORAD/elements/gp.php?GROUP=oneweb&FORMAT=tle"},
    {"22", "GPS Operational", "https://celestrak.org/NORAD/elements/gp.php?GROUP=gps-ops&FORMAT=tle"},
    {"23", "Galileo", "https://celestrak.org/NORAD/elements/gp.php?GROUP=galileo&FORMAT=tle"},
    {"24", "Amateur Radio", "https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle"},
    {"25", "CubeSats", "https://celestrak.org/NORAD/elements/gp.php?GROUP=cubesat&FORMAT=tle"}
};

static TLESource_t RETLECTOR_SOURCES[] = {
    {"1", "Last 30 Days' Launches", "https://retlector.eu/tle/last-30-days"},
    {"2", "Space Stations", "https://retlector.eu/tle/stations"},
    {"3", "100 Brightest", "https://retlector.eu/tle/visual"},
    {"4", "Active Satellites", "https://retlector.eu/tle/active"},
    {"5", "Analyst Satellites", "https://retlector.eu/tle/analyst"},
    {"6", "Russian ASAT (COSMOS 1408)", "https://retlector.eu/tle/cosmos-1408-debris"},
    {"7", "Chinese ASAT (FENGYUN 1C)", "https://retlector.eu/tle/fengyun-1c-debris"},
    {"8", "IRIDIUM 33 Debris", "https://retlector.eu/tle/iridium-33-debris"},
    {"9", "COSMOS 2251 Debris", "https://retlector.eu/tle/cosmos-2251-debris"},
    {"10", "Weather", "https://retlector.eu/tle/weather"},
    {"11", "NOAA", "https://retlector.eu/tle/noaa"},
    {"12", "GOES", "https://retlector.eu/tle/goes"},
    {"13", "Earth Resources", "https://retlector.eu/tle/resource"},
    {"14", "SARSAT", "https://retlector.eu/tle/sarsat"},
    {"15", "Disaster Monitoring", "https://retlector.eu/tle/dmc"},
    {"16", "TDRSS", "https://retlector.eu/tle/tdrss"},
    {"17", "ARGOS", "https://retlector.eu/tle/argos"},
    {"18", "Planet", "https://retlector.eu/tle/planet"},
    {"19", "Spire", "https://retlector.eu/tle/spire"},
    {"20", "Starlink", "https://retlector.eu/tle/starlink"},
    {"21", "OneWeb", "https://retlector.eu/tle/oneweb"},
    {"22", "GPS Operational", "https://retlector.eu/tle/gps-ops"},
    {"23", "Galileo", "https://retlector.eu/tle/galileo"},
    {"24", "Amateur Radio", "https://retlector.eu/tle/amateur"},
    {"25", "CubeSats", "https://retlector.eu/tle/cubesat"}
};
#define NUM_RETLECTOR_SOURCES 25
#define HELP_WINDOW_W 420.0f
#define HELP_WINDOW_H 500.0f
#define ROT_WINDOW_W 430.0f
#define ROT_WINDOW_H 430.0f

/* window z-ordering management */
typedef enum
{
    WND_HELP,
    WND_SETTINGS,
    WND_TIME,
    WND_PASSES,
    WND_POLAR,
    WND_DOPPLER,
    WND_SAT_MGR,
    WND_TLE_MGR,
    WND_SCOPE,
    WND_SAT_INFO,
    WND_ROTATOR,
    WND_MAX
} WindowID;

static WindowID z_order[WND_MAX] = {WND_HELP, WND_SETTINGS, WND_TIME, WND_PASSES, WND_POLAR, WND_DOPPLER, WND_SAT_MGR, WND_TLE_MGR, WND_SCOPE, WND_SAT_INFO, WND_ROTATOR};

static void BringToFront(WindowID id)
{
    int idx = -1;
    for (int i = 0; i < WND_MAX; i++)
    {
        if (z_order[i] == id)
        {
            idx = i;
            break;
        }
    }
    if (idx == -1 || idx == WND_MAX - 1)
        return;
    for (int i = idx; i < WND_MAX - 1; i++)
        z_order[i] = z_order[i + 1];
    z_order[WND_MAX - 1] = id;
}

/* ui state variables */
static bool show_help = false;
static bool show_settings = false;
static bool show_passes_dialog = false;
static bool show_polar_dialog = false;
static bool show_doppler_dialog = false;
static bool show_tle_warning = false;
static bool show_exit_dialog = false;

static bool show_sat_mgr_dialog = false;
static bool drag_sat_mgr = false;
static Vector2 drag_sat_mgr_off = {0};
static float sm_x = 200.0f, sm_y = 150.0f;
static Vector2 sat_mgr_scroll = {0};
static char sat_search_text[64] = "";
static bool edit_sat_search = false;

static bool show_tle_mgr_dialog = false;
static bool drag_tle_mgr = false;
static Vector2 drag_tle_mgr_off = {0};
static float tm_x = 250.0f, tm_y = 150.0f;
static Vector2 tle_mgr_scroll = {0};
static bool celestrak_expanded = false;
static bool retlector_expanded = false;
static bool retlector_selected[NUM_RETLECTOR_SOURCES] = {false};
static bool other_expanded = false;
static bool manual_expanded = true;
static bool celestrak_selected[25] = {false};
static long data_tle_epoch = -1;

enum { PULL_IDLE = 0, PULL_BUSY, PULL_DONE, PULL_ERROR };
static volatile int pull_state = PULL_IDLE;
static volatile bool pull_partial = false;
static AppConfig *pull_cfg = NULL;
#if defined(_WIN32) || defined(_WIN64)
static HANDLE pull_thread = NULL;
#else
static pthread_t pull_thread;
#endif

static char new_tle_buf[512] = "";
static bool edit_new_tle = false;

static int selected_pass_idx = -1;
static bool multi_pass_mode = true;
static Satellite *locked_pass_sat = NULL;
static double locked_pass_aos = 0.0;
static double locked_pass_los = 0.0;
static char text_min_el[8] = "0";
static bool edit_min_el = false;

static float hw_x = 100.0f, hw_y = 250.0f;
static float sw_x = 100.0f, sw_y = 250.0f;
static float pl_x = 550.0f, pl_y = 150.0f;
static float pd_x = 0.0f, pd_y = 0.0f;

static bool drag_help = false, drag_settings = false, drag_passes = false, drag_polar = false;
static Vector2 drag_help_off = {0}, drag_settings_off = {0}, drag_passes_off = {0}, drag_polar_off = {0};

static bool show_time_dialog = false;
static bool drag_time_dialog = false;
static Vector2 drag_time_off = {0};
static float td_x = 300.0f, td_y = 100.0f;

static Vector2 passes_scroll = {0};
static Vector2 help_scroll = {0};

static char text_doppler_freq[32] = "137625000";
static char text_doppler_res[32] = "1";
static char text_doppler_file[128] = "doppler_export.csv";
static bool edit_doppler_freq = false;
static bool edit_doppler_res = false;
static bool edit_doppler_file = false;
static bool drag_doppler = false;
static Vector2 drag_doppler_off = {0};
static float dop_x = 200.0f, dop_y = 150.0f;

static char text_year[8] = "2026", text_month[4] = "1", text_day[4] = "1";
static char text_hour[4] = "12", text_min[4] = "0", text_sec[4] = "0";
static char text_unix[64] = "0";
static bool edit_year = false, edit_month = false, edit_day = false;
static bool edit_hour = false, edit_min = false, edit_sec = false;
static bool edit_unix = false;

static char text_hl_name[64] = "";
static char text_hl_lat[32] = "";
static char text_hl_lon[32] = "";
static char text_hl_alt[32] = "";
static bool edit_hl_name = false, edit_hl_lat = false, edit_hl_lon = false, edit_hl_alt = false;
static float last_hl_lat = -999, last_hl_lon = -999, last_hl_alt = -999;

static bool polar_lunar_mode = false;
static double lunar_aos = 0.0;
static double lunar_los = 0.0;
static Vector2 lunar_path_pts[100];
static int lunar_num_pts = 0;
static double last_lunar_calc_time = 0.0;

static float tt_hover[19] = {0};
static bool rot_show_window = false;
static bool rot_dragging = false;
static Vector2 rot_drag_off = {0};
static float rot_x = 320.0f, rot_y = 120.0f;
static bool rot_edit_host = false;
static bool rot_edit_port = false;
static bool rot_edit_get_fmt = false;
static bool rot_edit_set_fmt = false;
static bool rot_edit_custom_cmd = false;
static bool rot_edit_park_az = false;
static bool rot_edit_park_el = false;
static bool rot_edit_lead_time = false;
static bool ui_initialized = false;
static char text_fps[8] = "";
static bool edit_fps = false;

#ifndef TLESCOPE_VERSION
#define TLESCOPE_VERSION "vUnknown"
#endif

static char latest_version_str[64] = "";
static bool update_available = false;

static size_t update_header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    size_t numbytes = size * nitems;
    if (strncmp(buffer, "Location: ", 10) == 0 || strncmp(buffer, "location: ", 10) == 0)
    {
        char *tag = strstr(buffer, "/tag/");
        if (tag)
        {
            tag += 5;
            strncpy((char *)userdata, tag, 63);
            for (int i = 0; ((char *)userdata)[i]; i++)
            {
                if (((char *)userdata)[i] == '\r' || ((char *)userdata)[i] == '\n')
                {
                    ((char *)userdata)[i] = '\0';
                    break;
                }
            }
        }
    }
    return numbytes;
}

static void *UpdateCheckThread(void *arg)
{
    (void)arg;
    CURL *curl = curl_easy_init();
    if (curl)
    {
        char user_agent[256];
        snprintf(user_agent, sizeof(user_agent), "Mozilla 5.0 (compatible; TLEscope/%s; +https://github.com/aweeri/TLEscope)", TLESCOPE_VERSION);
        
        curl_easy_setopt(curl, CURLOPT_URL, "https://github.com/aweeri/TLEscope/releases/latest");
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, update_header_callback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, latest_version_str);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
#if defined(_WIN32) || defined(_WIN64)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (strlen(latest_version_str) > 0 && strcmp(latest_version_str, TLESCOPE_VERSION) != 0)
        {
            if (strcmp(TLESCOPE_VERSION, "vUnknown") != 0 && strstr(TLESCOPE_VERSION, "-dirty") == NULL)
                update_available = true;
        }
    }
    return NULL;
}

#if defined(_WIN32) || defined(_WIN64)
static void UpdateCheckThreadWin(void *arg) { UpdateCheckThread(arg); }
#endif

static char theme_names[1024] = "";
static int active_theme_idx = 0;
static bool theme_dropdown_edit = false;

static void LoadThemeList(AppConfig* cfg) {
    theme_names[0] = '\0';
    int count = 0;
    active_theme_idx = 0;
    char temp_names[32][64];

#if defined(_WIN32) || defined(_WIN64)
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA("themes\\*", &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
                    char checkPath[256];
                    snprintf(checkPath, sizeof(checkPath), "themes/%s/theme.json", findData.cFileName);
                    if (FileExists(checkPath) && count < 32) {
                        strncpy(temp_names[count++], findData.cFileName, 63);
                    }
                }
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
#else
    DIR *dir = opendir("themes");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && count < 32) {
            if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
                char checkPath[256];
                snprintf(checkPath, sizeof(checkPath), "themes/%s/theme.json", ent->d_name);
                if (FileExists(checkPath)) {
                    strncpy(temp_names[count++], ent->d_name, 63);
                }
            }
        }
        closedir(dir);
    }
#endif

    for(int i = 0; i < count; i++) {
        strcat(theme_names, temp_names[i]);
        if (i < count - 1) strcat(theme_names, ";");
        if (strcmp(temp_names[i], cfg->theme) == 0) active_theme_idx = i;
    }
}

/* satellite scope variables */
static bool show_scope_dialog = false;
static bool drag_scope = false;
static Vector2 drag_scope_off = {0};
static float sc_x = 400.0f, sc_y = 150.0f;

static float scope_az = 180.0f;
static float scope_el = 45.0f;
static float scope_beam = 30.0f;

static bool scope_drag_active = false;
static Vector2 scope_drag_last = {0};
static bool scope_drag_moved = false;

static char text_scope_az[16] = "180.0";
static char text_scope_el[16] = "45.0";
static char text_scope_beam[16] = "30.0";

static bool edit_scope_az = false;
static bool edit_scope_el = false;
static bool edit_scope_beam = false;

static bool scope_lock = false;
static bool scope_show_leo = true;
static bool scope_show_heo = true;
static bool scope_show_geo = true;
static bool scope_show_trails = true;

static bool show_sat_info_dialog = false;
static bool drag_sat_info = false;
static Vector2 drag_sat_info_off = {0};
static float si_x = 0.0f, si_y = 0.0f;
static bool si_has_been_placed = false;
static bool si_rolled_up = false;
static Satellite *last_selected_sat = NULL;
static Vector2 si_scroll = {0};



static void LoadTLEState(AppConfig *cfg)
{
    if (data_tle_epoch != -1) return;

    char tlefile[] = "data.tle";
    char filename[1024];

    #if defined (BUILD_FOR_DIST)
    //The full path, with / at the end
    char fullPath[1024];
    const char *homeDir = getenv("XDG_CONFIG_HOME");
    if (!homeDir) homeDir = getenv("HOME");
    snprintf(fullPath,sizeof(fullPath),"%s/.config/TLEscope/%s", homeDir,tlefile);
    
    printf("DEBUG: %s\n", fullPath);
    snprintf(filename,sizeof(filename),fullPath);
    #else 
    snprintf(filename, sizeof(filename),tlefile);
    #endif

    FILE *f = fopen(filename, "r");
    if (f)
    {
        char line[256];
        if (fgets(line, sizeof(line), f))
        {
            if (strncmp(line, "# EPOCH:", 8) == 0)
            {
                unsigned int mask = 0, cust_mask = 0, ret_mask = 0;
                if (strstr(line, "RET_MASK:"))
                    sscanf(line, "# EPOCH:%ld MASK:%u CUST_MASK:%u RET_MASK:%u", &data_tle_epoch, &mask, &cust_mask, &ret_mask);
                else if (strstr(line, "CUST_MASK:"))
                    sscanf(line, "# EPOCH:%ld MASK:%u CUST_MASK:%u", &data_tle_epoch, &mask, &cust_mask);
                else
                {
                    int cust_count = 0;
                    sscanf(line, "# EPOCH:%ld MASK:%u CUST:%d", &data_tle_epoch, &mask, &cust_count);
                }

                for (int i = 0; i < 25; i++)
                    celestrak_selected[i] = (mask & (1 << i)) != 0;
                for (int i = 0; i < NUM_RETLECTOR_SOURCES; i++)
                    retlector_selected[i] = (ret_mask & (1 << i)) != 0;
                for (int i = 0; i < cfg->custom_tle_source_count; i++)
                    cfg->custom_tle_sources[i].selected = (cust_mask & (1 << i)) != 0;
            }
        }
        fclose(f);
    }
    if (data_tle_epoch == -1) data_tle_epoch = 0;
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0; // out of memory

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static bool DownloadTLESource(CURL *curl, const char *url, FILE *out)
{
    if (!curl || !url || !out) return false;

    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); /* handle compression */
    
    char user_agent[256];
    snprintf(user_agent, sizeof(user_agent), "Mozilla 5.0 (compatible; TLEscope/%s; +https://github.com/aweeri/TLEscope)", TLESCOPE_VERSION);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);

#if defined(_WIN32) || defined(_WIN64)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    bool ok = (res == CURLE_OK && http_code == 200);
    if (ok)
    {
        fwrite(chunk.memory, 1, chunk.size, out);
        fprintf(out, "\r\n");
    }
    else
    {
        printf("Failed to download %s: %s (HTTP %ld)\n", url, curl_easy_strerror(res), http_code);
    }

    free(chunk.memory);
    return ok;
}

static void ReloadTLEsLocally(UIContext *ctx, AppConfig *cfg)
{
    if (ctx)
    {
        *ctx->selected_sat = NULL;
        ctx->hovered_sat = NULL;
        ctx->active_sat = NULL;
        *ctx->active_lock = LOCK_EARTH;
    }
    locked_pass_sat = NULL;
    num_passes = 0;
    last_pass_calc_sat = NULL;
    sat_count = 0;
    load_tle_data("data.tle");
    load_manual_tles(cfg);
    LoadSatSelection();
}

/* background thread: downloads all selected TLE sources to data.tle */
static void *PullTLEThread(void *arg)
{
    (void)arg;
    AppConfig *cfg = pull_cfg;

    char tlefile[] = "data.tle";
    char filename[1024];

    #if defined (BUILD_FOR_DIST)
    //The full path, with / at the end
    char fullPath[1024];
    const char *homeDir = getenv("XDG_CONFIG_HOME");
    if (!homeDir) homeDir = getenv("HOME");
    snprintf(fullPath,sizeof(fullPath),"%s/.config/TLEscope/%s", homeDir,tlefile);
    
    printf("DEBUG: %s\n", fullPath);
    snprintf(filename,sizeof(filename),fullPath);
    #else 
    snprintf(filename, sizeof(filename),tlefile);
    #endif

    FILE *out = fopen(filename, "wb");
    if (!out)
    {
        pull_state = PULL_ERROR;
        return NULL;
    }

    unsigned int mask = 0, ret_mask = 0, cust_mask = 0;
    for (int i = 0; i < 25; i++)
        if (celestrak_selected[i]) mask |= (1 << i);
    for (int i = 0; i < NUM_RETLECTOR_SOURCES; i++)
        if (retlector_selected[i]) ret_mask |= (1 << i);
    for (int i = 0; i < cfg->custom_tle_source_count; i++)
        if (cfg->custom_tle_sources[i].selected) cust_mask |= (1 << i);

    fprintf(out, "# EPOCH:%ld MASK:%u CUST_MASK:%u RET_MASK:%u\r\n", (long)time(NULL), mask, cust_mask, ret_mask);

    int ok_count = 0, fail_count = 0;
    CURL *curl = curl_easy_init();
    if (curl)
    {
        for (int i = 0; i < NUM_RETLECTOR_SOURCES; i++)
            if (ret_mask & (1 << i))
            { if (DownloadTLESource(curl, RETLECTOR_SOURCES[i].url, out)) ok_count++; else fail_count++; }

        for (int i = 0; i < 25; i++)
            if (mask & (1 << i))
            { if (DownloadTLESource(curl, SOURCES[i].url, out)) ok_count++; else fail_count++; }

        for (int i = 0; i < cfg->custom_tle_source_count; i++)
            if (cust_mask & (1 << i))
            { if (DownloadTLESource(curl, cfg->custom_tle_sources[i].url, out)) ok_count++; else fail_count++; }

        curl_easy_cleanup(curl);
    }
    else
    {
        printf("Failed to initialize libcurl.\n");
    }

    fclose(out);

    pull_partial = (ok_count > 0 && fail_count > 0);
    __sync_synchronize(); /* ensure pull_partial is visible before pull_state on ARM */
    if (fail_count > 0 && ok_count == 0) pull_state = PULL_ERROR;
    else pull_state = PULL_DONE;
    return NULL;
}

#if defined(_WIN32) || defined(_WIN64)
static void PullTLEThreadWin(void *arg) { PullTLEThread(arg); }
#endif

/* called from main thread to kick off async pull */
static void PullTLEData(AppConfig *cfg)
{
    if (pull_state == PULL_BUSY) return;
    pull_state = PULL_BUSY;
    pull_partial = false;
    pull_cfg = cfg;

#if defined(_WIN32) || defined(_WIN64)
    uintptr_t h = _beginthread(PullTLEThreadWin, 0, NULL);
    if (h == (uintptr_t)-1L) { pull_state = PULL_ERROR; return; }
    pull_thread = (HANDLE)h;
#else
    if (pthread_create(&pull_thread, NULL, PullTLEThread, NULL) != 0)
    { pull_state = PULL_ERROR; return; }
    pthread_detach(pull_thread);
#endif
}

/* called each frame from DrawGUI to finish reload on the main thread */
static void FinishPullIfDone(UIContext *ctx, AppConfig *cfg)
{
    if (pull_state == PULL_DONE)
    {
        if (ctx)
        {
            *ctx->selected_sat = NULL;
            ctx->hovered_sat = NULL;
            ctx->active_sat = NULL;
            *ctx->active_lock = LOCK_EARTH;
        }
        locked_pass_sat = NULL;
        num_passes = 0;
        last_pass_calc_sat = NULL;
        sat_count = 0;
        load_tle_data("data.tle");
        load_manual_tles(cfg);
        if (sat_count > 500)
        {
            for (int i = 0; i < sat_count; i++)
                satellites[i].is_active = false;
        }
        LoadSatSelection();
        data_tle_epoch = time(NULL);
        pull_state = PULL_IDLE;
    }
}

static void CalculateLunarPass(double base_epoch, double *aos, double *los, Vector2 *pts, int *num_pts)
{
    double step = 10.0 / 1440.0;
    
    double peak_time = base_epoch;
    double max_el = -90;
    for(double t = base_epoch - 0.5; t <= base_epoch + 0.5; t += step)
    {
        double az, el;
        get_az_el(calculate_moon_position(t), epoch_to_gmst(t), home_location.lat, home_location.lon, home_location.alt, &az, &el);
        if(el > max_el) { max_el = el; peak_time = t; }
    }
    
    double found_aos = peak_time;
    for(double t = peak_time; t >= peak_time - 0.6; t -= step)
    {
        double az, el;
        get_az_el(calculate_moon_position(t), epoch_to_gmst(t), home_location.lat, home_location.lon, home_location.alt, &az, &el);
        if(el < 0) { found_aos = t; break; }
    }
    
    double found_los = peak_time;
    for(double t = peak_time; t <= peak_time + 0.6; t += step)
    {
        double az, el;
        get_az_el(calculate_moon_position(t), epoch_to_gmst(t), home_location.lat, home_location.lon, home_location.alt, &az, &el);
        if(el < 0) { found_los = t; break; }
    }
    
    *aos = found_aos;
    *los = found_los;
    *num_pts = 0;
    double pt_step = (*los - *aos) / 99.0;
    if (pt_step > 0)
    {
        for (int i = 0; i < 100; i++)
        {
            double pt_time = *aos + i * pt_step;
            double az, el;
            get_az_el(calculate_moon_position(pt_time), epoch_to_gmst(pt_time), home_location.lat, home_location.lon, home_location.alt, &az, &el);
            if (el < 0) el = 0; 
            pts[(*num_pts)++] = (Vector2){(float)az, (float)el};
        }
    }
}

/* shared helper functions */
bool IsOccludedByEarth(Vector3 camPos, Vector3 targetPos, float earthRadius)
{
    Vector3 v = Vector3Subtract(targetPos, camPos);
    float a = Vector3LengthSqr(v);
    if (a < 0.000001f) return false;

    float b = 2.0f * Vector3DotProduct(camPos, v);
    float c = Vector3LengthSqr(camPos) - (earthRadius * earthRadius * 0.9801f); // 0.99^2 is 0.9801

    float t = -b / (2.0f * a);

    if (t > 0.0f && t < 1.0f)
    {
        float minDistSqr = c - (b * b) / (4.0f * a);
        if (minDistSqr < 0.0f)
            return true;
    }
    return false;
}

Color ApplyAlpha(Color c, float alpha)
{
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    c.a = (unsigned char)(c.a * alpha);
    return c;
}

void DrawUIText(Font font, const char *text, float x, float y, float size, Color color) { DrawTextEx(font, text, (Vector2){x, y}, size, 1.0f, color); }

double StepTimeMultiplier(double current, bool increase)
{
    double next = current;

    if (increase)
    {
        if (current == 0.0)
            next = 0.25;
        else if (current >= 0.25)
            next = current * 2.0;
        else if (current <= -0.25)
        {
            if (current == -0.25)
                next = 0.0;
            else
                next = current / 2.0;
        }
    }
    else
    {
        if (current == 0.0)
            next = -0.25;
        else if (current <= -0.25)
            next = current * 2.0;
        else if (current >= 0.25)
        {
            if (current == 0.25)
                next = 0.0;
            else
                next = current / 2.0;
        }
    }

    if (next > 1048576.0)
        next = 1048576.0;
    if (next < -1048576.0)
        next = -1048576.0;

    return next;
}

double unix_to_epoch(double target_unix)
{
    time_t t = (time_t)target_unix;
    struct tm *gmt = gmtime(&t);
    if (!gmt)
        return get_current_real_time_epoch();
    int year = gmt->tm_year + 1900;
    double day_of_year = gmt->tm_yday + 1.0;
    double fraction_of_day = (gmt->tm_hour + gmt->tm_min / 60.0 + gmt->tm_sec / 3600.0) / 24.0;
    return (year * 1000.0) + day_of_year + fraction_of_day;
}

static bool string_contains_ignore_case(const char *haystack, const char *needle)
{
    if (!needle || !*needle)
        return true;
    int h_len = strlen(haystack);
    int n_len = strlen(needle);
    for (int i = 0; i <= h_len - n_len; i++)
    {
        int j;
        for (j = 0; j < n_len; j++)
        {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == n_len)
            return true;
    }
    return false;
}

static void SnapWindow(float *x, float *y, float w, float h, AppConfig *cfg)
{
    float margin = 10.0f * cfg->ui_scale;
    float threshold = 20.0f * cfg->ui_scale;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    if (fabsf(*x - margin) < threshold)
        *x = margin;
    if (fabsf((*x + w) - (sw - margin)) < threshold)
        *x = sw - margin - w;
    if (fabsf(*y - margin) < threshold)
        *y = margin;
    if (fabsf((*y + h) - (sh - margin)) < threshold)
        *y = sh - margin - h;
}

static void FindSmartWindowPosition(float w, float h, AppConfig *cfg, float *out_x, float *out_y)
{
    float margin = 10.0f * cfg->ui_scale;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    Rectangle active[12];
    int count = 0;
    if (show_help)
        active[count++] = (Rectangle){hw_x, hw_y, HELP_WINDOW_W * cfg->ui_scale, HELP_WINDOW_H * cfg->ui_scale};
    if (show_settings)
        active[count++] = (Rectangle){sw_x, sw_y, 250 * cfg->ui_scale, 520 * cfg->ui_scale};
    if (show_time_dialog)
        active[count++] = (Rectangle){td_x, td_y, 252 * cfg->ui_scale, 320 * cfg->ui_scale};
    if (show_passes_dialog)
        active[count++] = (Rectangle){pd_x, pd_y, 357 * cfg->ui_scale, 380 * cfg->ui_scale};
    if (show_polar_dialog)
        active[count++] = (Rectangle){pl_x, pl_y, 300 * cfg->ui_scale, 430 * cfg->ui_scale};
    if (show_doppler_dialog)
        active[count++] = (Rectangle){dop_x, dop_y, 320 * cfg->ui_scale, 480 * cfg->ui_scale};
    if (show_sat_mgr_dialog)
        active[count++] = (Rectangle){sm_x, sm_y, 400 * cfg->ui_scale, 500 * cfg->ui_scale};
    if (show_tle_mgr_dialog)
        active[count++] = (Rectangle){tm_x, tm_y, 400 * cfg->ui_scale, 500 * cfg->ui_scale};
    if (show_scope_dialog)
        active[count++] = (Rectangle){sc_x, sc_y, 360 * cfg->ui_scale, 560 * cfg->ui_scale};
    if (show_sat_info_dialog)
        active[count++] = (Rectangle){si_x, si_y, 320 * cfg->ui_scale, si_rolled_up ? 24 * cfg->ui_scale : 480 * cfg->ui_scale};
    if (RotatorIsWindowVisible())
        active[count++] = RotatorGetWindowRect(cfg);

    float candidates_x[] = {margin, sw - w - margin};
    float step_y = 20.0f * cfg->ui_scale;

    for (int i = 0; i < 2; i++)
    {
        float test_x = candidates_x[i];
        for (float test_y = margin; test_y <= sh - h - margin; test_y += step_y)
        {
            Rectangle test_rect = {test_x - margin / 2, test_y - margin / 2, w + margin, h + margin};
            bool collision = false;
            for (int j = 0; j < count; j++)
            {
                if (CheckCollisionRecs(test_rect, active[j]))
                {
                    collision = true;
                    break;
                }
            }
            if (!collision)
            {
                *out_x = test_x;
                *out_y = test_y;
                return;
            }
        }
    }

    static float cascade = 0;
    *out_x = 50 * cfg->ui_scale + cascade;
    *out_y = 50 * cfg->ui_scale + cascade;
    cascade += 20 * cfg->ui_scale;
    if (cascade > 200 * cfg->ui_scale)
        cascade = 0;
}

bool IsUITyping(void)
{
    return edit_year || edit_month || edit_day || edit_hour || edit_min || edit_sec || edit_unix || edit_doppler_freq || edit_doppler_res || edit_doppler_file || edit_sat_search || edit_min_el ||
           edit_hl_name || edit_hl_lat || edit_hl_lon || edit_hl_alt || edit_fps || edit_new_tle || edit_scope_az || edit_scope_el || edit_scope_beam ||
           rot_edit_host || rot_edit_port || rot_edit_get_fmt || rot_edit_set_fmt || rot_edit_custom_cmd || rot_edit_park_az || rot_edit_park_el || rot_edit_lead_time;
}

void ToggleTLEWarning(void) { show_tle_warning = !show_tle_warning; }

bool IsMouseOverUI(AppConfig *cfg)
{
    if (show_exit_dialog || cfg->show_first_run_dialog)
        return true;
    if (!ui_initialized)
    {
        pd_x = GetScreenWidth() - 400.0f;
        pd_y = GetScreenHeight() - 400.0f;
        LoadTLEState(cfg);
        LoadThemeList(cfg);

        if (!cfg->show_first_run_dialog && data_tle_epoch > 0)
        {
            long diff = time(NULL) - data_tle_epoch;
            if (diff > 2 * 86400)
                show_tle_warning = true;
        }

#if defined(_WIN32) || defined(_WIN64)
        _beginthread(UpdateCheckThreadWin, 0, NULL);
#else
        pthread_t update_thread;
        if (pthread_create(&update_thread, NULL, UpdateCheckThread, NULL) == 0)
            pthread_detach(update_thread);
#endif

        ui_initialized = true;
    }

    bool over_window = false;
    float pass_w = 357 * cfg->ui_scale, pass_h = 380 * cfg->ui_scale;

    if (show_help && CheckCollisionPointRec(GetMousePosition(), (Rectangle){hw_x, hw_y, HELP_WINDOW_W * cfg->ui_scale, HELP_WINDOW_H * cfg->ui_scale}))
        over_window = true;
    if (show_settings && CheckCollisionPointRec(GetMousePosition(), (Rectangle){sw_x, sw_y, 250 * cfg->ui_scale, 520 * cfg->ui_scale}))
        over_window = true;
    if (show_time_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){td_x, td_y, 252 * cfg->ui_scale, 320 * cfg->ui_scale}))
        over_window = true;
    if (show_passes_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){pd_x, pd_y, pass_w, pass_h}))
        over_window = true;
    if (show_polar_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){pl_x, pl_y, 300 * cfg->ui_scale, 430 * cfg->ui_scale}))
        over_window = true;
    if (show_doppler_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){dop_x, dop_y, 320 * cfg->ui_scale, 480 * cfg->ui_scale}))
        over_window = true;
    if (show_sat_mgr_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){sm_x, sm_y, 400 * cfg->ui_scale, 500 * cfg->ui_scale}))
        over_window = true;
    if (show_tle_mgr_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){tm_x, tm_y, 400 * cfg->ui_scale, 500 * cfg->ui_scale}))
        over_window = true;
    if (show_scope_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){sc_x, sc_y, 360 * cfg->ui_scale, 560 * cfg->ui_scale}))
        over_window = true;
    if (show_sat_info_dialog && CheckCollisionPointRec(GetMousePosition(), (Rectangle){si_x, si_y, 320 * cfg->ui_scale, si_rolled_up ? 24 * cfg->ui_scale : 480 * cfg->ui_scale}))
        over_window = true;
    if (RotatorIsPointInWindow(GetMousePosition(), cfg))
        over_window = true;
    if (show_tle_warning &&
        CheckCollisionPointRec(
            GetMousePosition(), (Rectangle){(GetScreenWidth() - 480 * cfg->ui_scale) / 2.0f, (GetScreenHeight() - 160 * cfg->ui_scale) / 2.0f, 480 * cfg->ui_scale, 160 * cfg->ui_scale}
        ))
        over_window = true;

    if (over_window)
        return true;

    float center_x_bottom = (GetScreenWidth() - (6 * 35 - 5) * cfg->ui_scale) / 2.0f;
    float center_x_top = (GetScreenWidth() - (13 * 35 - 5) * cfg->ui_scale) / 2.0f;

        Rectangle btnRecs[] = {
            {center_x_top, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 35 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 70 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 105 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 140 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 175 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 210 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 245 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 280 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 315 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 350 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 385 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_top + 420 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_bottom, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_bottom + 35 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_bottom + 70 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_bottom + 105 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_bottom + 140 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale},
            {center_x_bottom + 175 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale}
        };
        for (int i = 0; i < 19; i++)
    {
        if (CheckCollisionPointRec(GetMousePosition(), btnRecs[i]))
            return true;
    }
    return false;
}

void SaveSatSelection(void)
{
    char persistenceFile[1024];

    #if defined (BUILD_FOR_DIST)
    //The full path, with /settings.json at the end
    char fullPath[1024];
    const char *homeDir = getenv("XDG_CONFIG_HOME");
    if (!homeDir) homeDir = getenv("HOME");
    snprintf(fullPath,sizeof(fullPath),"%s/.config/TLEscope/persistence.bin", homeDir);
    
    printf("DEBUG: %s\n", fullPath);
    snprintf(persistenceFile,sizeof(persistenceFile),fullPath);
    #else 
    snprintf(persistenceFile, sizeof(persistenceFile),resource);
    #endif

    FILE *f = fopen(persistenceFile, "wb");
    if (!f)
        return;
    int count = 0;
    for (int i = 0; i < sat_count; i++)
        if (satellites[i].is_active)
            count++;
    fwrite(&count, sizeof(int), 1, f);
    for (int i = 0; i < sat_count; i++)
    {
        if (satellites[i].is_active)
        {
            unsigned char len = (unsigned char)strlen(satellites[i].name);
            fwrite(&len, 1, 1, f);
            fwrite(satellites[i].name, 1, len, f);
        }
    }
    fclose(f);
}

void LoadSatSelection(void)
{
    char persistenceFile[1024];

    #if defined (BUILD_FOR_DIST)
    //The full path, with /settings.json at the end
    char fullPath[1024];
    const char *homeDir = getenv("XDG_CONFIG_HOME");
    if (!homeDir) homeDir = getenv("HOME");
    snprintf(fullPath,sizeof(fullPath),"%s/.config/TLEscope/persistence.bin", homeDir);
    
    printf("DEBUG: %s\n", fullPath);
    snprintf(persistenceFile,sizeof(persistenceFile),fullPath);
    #else 
    snprintf(persistenceFile, sizeof(persistenceFile),resource);
    #endif

    FILE *f = fopen(persistenceFile, "rb");
    if (!f)
        return;
    int count;
    if (fread(&count, sizeof(int), 1, f) != 1)
    {
        fclose(f);
        return;
    }

    for (int i = 0; i < sat_count; i++)
        satellites[i].is_active = false;

    for (int i = 0; i < count; i++)
    {
        unsigned char len;
        char name[64] = {0};
        if (fread(&len, 1, 1, f) != 1)
            break;
        fread(name, 1, len, f);
        for (int j = 0; j < sat_count; j++)
        {
            if (strcmp(satellites[j].name, name) == 0)
            {
                satellites[j].is_active = true;
                break;
            }
        }
    }
    fclose(f);
}

/* custom input handler to support strict numeric filters, Ctrl+A selections, and copy/paste */
static void FilterNumericStr(char *text)
{
    int len = strlen(text);
    int j = 0;
    for (int i = 0; i < len; i++)
    {
        if ((text[i] >= '0' && text[i] <= '9') || text[i] == '.' || text[i] == '-')
        {
            text[j++] = text[i];
        }
    }
    text[j] = '\0';
}

static bool tb_select_all = false;
static void *active_tb_ptr = NULL;

static void AdvancedTextBox(Rectangle bounds, char *text, int bufSize, bool *editMode, bool numeric)
{
    /* intercept keystrokes when text is fully selected before the GUI framework eats them */
    if (*editMode && tb_select_all && active_tb_ptr == text)
    {
        int key = GetCharPressed();
        if (key > 0)
        {
            text[0] = (char)key;
            text[1] = '\0';
            tb_select_all = false;
        }
        else if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE))
        {
            text[0] = '\0';
            tb_select_all = false;
        }
    }

    if (GuiTextBox(bounds, text, bufSize, *editMode))
        *editMode = !*editMode;

    if (*editMode)
    {
        if (active_tb_ptr != text)
        {
            active_tb_ptr = text;
            tb_select_all = false;
        }

        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        {
            if (IsKeyPressed(KEY_A))
                tb_select_all = true;
            if (IsKeyPressed(KEY_C))
                SetClipboardText(text);
            if (IsKeyPressed(KEY_X))
            {
                SetClipboardText(text);
                text[0] = '\0';
                tb_select_all = false;
            }
            if (IsKeyPressed(KEY_V))
            {
                const char *cb = GetClipboardText();
                if (cb)
                {
                    strncpy(text, cb, bufSize - 1);
                    text[bufSize - 1] = '\0';
                    if (numeric)
                        FilterNumericStr(text);
                    tb_select_all = false;
                }
            }
        }

        /* mouse-driven text selection */
        if (CheckCollisionPointRec(GetMousePosition(), bounds))
        {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            {
                Vector2 delta = GetMouseDelta();
                if (fabs(delta.x) > 2.0f)
                    tb_select_all = true; // dragging selects all
            }

            static double last_click_time = 0;
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                if (GetTime() - last_click_time < 0.3)
                    tb_select_all = true; // double click selects all
                else
                    tb_select_all = false; // single click deselects to place cursor
                last_click_time = GetTime();
            }
        }

        if (tb_select_all)
        {
            DrawRectangle(bounds.x + 4, bounds.y + 4, MeasureTextEx(GuiGetFont(), text, GuiGetStyle(DEFAULT, TEXT_SIZE), 1).x, bounds.height - 8, Fade(BLUE, 0.5f));
        }

        if (numeric)
            FilterNumericStr(text);
    }
    else
    {
        if (active_tb_ptr == text)
        {
            active_tb_ptr = NULL;
            tb_select_all = false;
        }
    }
}

static bool DrawMaterialWindow(Rectangle bounds, const char *title, AppConfig *cfg, Font font, bool closable)
{
    float scale = cfg->ui_scale;
    float header_h = 24 * scale;

    DrawRectangleRounded(bounds, 0.05f, 4, cfg->ui_primary);

    // Subtle content-card fill to unify window interiors with sectioned dialogs.
    Rectangle content = {bounds.x + 4 * scale, bounds.y + header_h + 3 * scale, bounds.width - 8 * scale, bounds.height - header_h - 7 * scale};
    if (content.width > 0 && content.height > 0)
        DrawRectangleRounded(content, 0.04f, 6, ApplyAlpha(cfg->ui_bg, 0.10f));

#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
    DrawRectangleRoundedLines(bounds, 0.05f, 8, 1.5f * scale, cfg->window_border);
#else
    DrawRectangleRoundedLines(bounds, 0.05f, 8, cfg->window_border);
#endif

    const char *titleText = title;
    int titleIcon = -1;
    if (titleText[0] == '#')
    {
        const char *end = strchr(titleText + 1, '#');
        if (end)
        {
            char iconBuf[8] = {0};
            int iconLen = (int)(end - (titleText + 1));
            if (iconLen > 0 && iconLen < (int)sizeof(iconBuf))
            {
                memcpy(iconBuf, titleText + 1, (size_t)iconLen);
                titleIcon = atoi(iconBuf);
            }
            titleText = end + 1;
            if (titleText[0] == ' ')
                titleText++;
        }
    }

    float title_x = bounds.x + 12 * scale;
    if (titleIcon >= 0)
    {
        GuiLabel((Rectangle){bounds.x + 8 * scale, bounds.y + 3 * scale, 18 * scale, 18 * scale}, TextFormat("#%d#", titleIcon));
        title_x = bounds.x + 30 * scale;
    }
    DrawUIText(font, titleText, title_x, bounds.y + (header_h - 16 * scale) / 2.0f, 16 * scale, cfg->ui_accent);

    DrawLineEx((Vector2){bounds.x + 2 * scale, bounds.y + header_h}, (Vector2){bounds.x + bounds.width - 2 * scale, bounds.y + header_h}, 1.0f, cfg->window_border);

    if (!closable) return false;

    float btn_size = header_h - 8 * scale;
    Rectangle closeBtn = {bounds.x + bounds.width - btn_size - 6 * scale, bounds.y + 4 * scale, btn_size, btn_size};

    bool hover = CheckCollisionPointRec(GetMousePosition(), closeBtn);
    bool clicked = false;

    if (hover) {
        DrawRectangleRounded(closeBtn, 0.3f, 8, ApplyAlpha(RED, 0.8f));
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) clicked = true;
    } else {
        DrawRectangleRounded(closeBtn, 0.3f, 8, ApplyAlpha(cfg->window_border, 0.3f));
    }

    float pad = closeBtn.width * 0.3f;
    DrawLineEx((Vector2){closeBtn.x + pad, closeBtn.y + pad}, (Vector2){closeBtn.x + closeBtn.width - pad, closeBtn.y + closeBtn.height - pad}, 2.0f, cfg->text_main);
    DrawLineEx((Vector2){closeBtn.x + closeBtn.width - pad, closeBtn.y + pad}, (Vector2){closeBtn.x + pad, closeBtn.y + closeBtn.height - pad}, 2.0f, cfg->text_main);

    return clicked;
}

bool RotatorIsWindowVisible(void) { return rot_show_window; }

Rectangle RotatorGetWindowRect(AppConfig *cfg) { return (Rectangle){rot_x, rot_y, ROT_WINDOW_W * cfg->ui_scale, ROT_WINDOW_H * cfg->ui_scale}; }

bool RotatorIsPointInWindow(Vector2 point, AppConfig *cfg) { return rot_show_window && CheckCollisionPointRec(point, RotatorGetWindowRect(cfg)); }

void RotatorToggleWindow(AppConfig *cfg)
{
    if (!rot_show_window)
    {
        Rectangle r = RotatorGetWindowRect(cfg);
        rot_x = (GetScreenWidth() - r.width) / 2.0f;
        rot_y = (GetScreenHeight() - r.height) / 2.0f;
        SnapWindow(&rot_x, &rot_y, r.width, r.height, cfg);
    }
    rot_show_window = !rot_show_window;
}

bool RotatorBeginDrag(Vector2 point, AppConfig *cfg)
{
    Rectangle r = RotatorGetWindowRect(cfg);
    Rectangle title = (Rectangle){r.x, r.y, r.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale};
    if (!rot_show_window || !CheckCollisionPointRec(point, title))
        return false;
    rot_dragging = true;
    rot_drag_off = (Vector2){point.x - rot_x, point.y - rot_y};
    return true;
}

void RotatorUpdateDrag(AppConfig *cfg)
{
    if (!rot_dragging || !rot_show_window)
        return;
    Rectangle r = RotatorGetWindowRect(cfg);
    rot_x = GetMousePosition().x - rot_drag_off.x;
    rot_y = GetMousePosition().y - rot_drag_off.y;
    SnapWindow(&rot_x, &rot_y, r.width, r.height, cfg);
}

void RotatorEndDrag(void) { rot_dragging = false; }

static bool DrawRotatorFrame(Rectangle bounds, AppConfig *cfg, Font customFont)
{
    float scale = cfg->ui_scale;
    float header_h = 24 * scale;

    DrawRectangleRounded(bounds, 0.05f, 4, cfg->ui_primary);
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
    DrawRectangleRoundedLines(bounds, 0.05f, 8, 1.5f * scale, cfg->window_border);
#else
    DrawRectangleRoundedLines(bounds, 0.05f, 8, cfg->window_border);
#endif

    GuiLabel((Rectangle){bounds.x + 8 * scale, bounds.y + 3 * scale, 18 * scale, 18 * scale}, "#65#");
    DrawUIText(customFont, "Rotator Control", bounds.x + 30 * scale, bounds.y + (header_h - 16 * scale) / 2.0f, 16 * scale, cfg->ui_accent);
    DrawLineEx((Vector2){bounds.x + 2 * scale, bounds.y + header_h}, (Vector2){bounds.x + bounds.width - 2 * scale, bounds.y + header_h}, 1.0f, cfg->window_border);

    float btn_size = header_h - 8 * scale;
    Rectangle close_btn = {bounds.x + bounds.width - btn_size - 6 * scale, bounds.y + 4 * scale, btn_size, btn_size};
    bool hover = CheckCollisionPointRec(GetMousePosition(), close_btn);
    bool clicked = false;
    DrawRectangleRounded(close_btn, 0.3f, 8, hover ? ApplyAlpha(RED, 0.8f) : ApplyAlpha(cfg->window_border, 0.3f));
    if (hover && IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        clicked = true;

    float pad = close_btn.width * 0.3f;
    DrawLineEx((Vector2){close_btn.x + pad, close_btn.y + pad}, (Vector2){close_btn.x + close_btn.width - pad, close_btn.y + close_btn.height - pad}, 2.0f, cfg->text_main);
    DrawLineEx((Vector2){close_btn.x + close_btn.width - pad, close_btn.y + pad}, (Vector2){close_btn.x + pad, close_btn.y + close_btn.height - pad}, 2.0f, cfg->text_main);
    return clicked;
}

static void DrawRotatorSection(Rectangle sec, const char *title, AppConfig *cfg, Font customFont)
{
    float scale = cfg->ui_scale;
    DrawRectangleRounded(sec, 0.06f, 6, ApplyAlpha(cfg->ui_bg, 0.14f));
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
    DrawRectangleRoundedLines(sec, 0.06f, 6, 1.0f * scale, ApplyAlpha(cfg->window_border, 0.85f));
#else
    DrawRectangleRoundedLines(sec, 0.06f, 6, ApplyAlpha(cfg->window_border, 0.85f));
#endif
    DrawUIText(customFont, title, sec.x + 8 * scale, sec.y + 5 * scale, 15 * scale, cfg->ui_accent);
    DrawLineEx((Vector2){sec.x + 8 * scale, sec.y + 22 * scale}, (Vector2){sec.x + sec.width - 8 * scale, sec.y + 22 * scale}, 1.0f, ApplyAlpha(cfg->window_border, 0.7f));
}

void RotatorDrawWindow(AppConfig *cfg, Font customFont, bool interactive)
{
    if (!rot_show_window)
        return;

    Rectangle r = RotatorGetWindowRect(cfg);
    if (interactive && DrawRotatorFrame(r, cfg, customFont))
    {
        rot_show_window = false;
        return;
    }
    if (!interactive)
        DrawRotatorFrame(r, cfg, customFont);

    float scale = cfg->ui_scale;
    float pad = 10 * scale;
    float section_gap = 8 * scale;
    float content_x = r.x + pad;
    float content_w = r.width - 2 * pad;
    float y = r.y + 32 * scale;

    char *host = RotatorGetHostBuffer();
    char *port = RotatorGetPortBuffer();
    char *get_fmt = RotatorGetGetFmtBuffer();
    char *set_fmt = RotatorGetSetFmtBuffer();
    char *custom_cmd = RotatorGetCustomCmdBuffer();
    char *park_az = RotatorGetParkAzBuffer();
    char *park_el = RotatorGetParkElBuffer();

    Rectangle sec_conn = {content_x, y, content_w, 102 * scale};
    DrawRotatorSection(sec_conn, "Connection", cfg, customFont);

    float ry = sec_conn.y + 28 * scale;
    float inner_x = sec_conn.x + 8 * scale;
    float inner_w = sec_conn.width - 16 * scale;
    float host_label_w = 38 * scale;
    float port_label_w = 34 * scale;
    float port_w = 55 * scale;
    float conn_w = 78 * scale;
    float gap = 5 * scale;
    float host_w = inner_w - host_label_w - port_label_w - port_w - conn_w - gap * 4;
    if (host_w < 110 * scale)
        host_w = 110 * scale;

    GuiLabel((Rectangle){inner_x, ry, host_label_w, 24 * scale}, "Host:");
    bool host_toggled = GuiTextBox((Rectangle){inner_x + host_label_w + gap, ry, host_w, 24 * scale}, host, RotatorGetHostBufferSize(), interactive && rot_edit_host);
    if (interactive && host_toggled)
        rot_edit_host = !rot_edit_host;
    float port_x = inner_x + host_label_w + gap + host_w + gap;
    GuiLabel((Rectangle){port_x, ry, port_label_w, 24 * scale}, "Port:");
    bool port_toggled = GuiTextBox((Rectangle){port_x + port_label_w + gap, ry, port_w, 24 * scale}, port, RotatorGetPortBufferSize(), interactive && rot_edit_port);
    if (interactive && port_toggled)
        rot_edit_port = !rot_edit_port;
    bool connect_pressed = GuiButton((Rectangle){inner_x + inner_w - conn_w, ry, conn_w, 24 * scale}, RotatorIsConnected() ? "Disconn" : "Connect");
    if (interactive && connect_pressed)
    {
        if (!RotatorIsConnected())
            RotatorConnect();
        else
            RotatorDisconnect();
    }

    ry += 30 * scale;
    DrawUIText(customFont, TextFormat("Link: %s", RotatorIsConnected() ? "CONNECTED" : "DISCONNECTED"), inner_x, ry, 14 * scale, RotatorIsConnected() ? cfg->ui_accent : cfg->text_secondary);
    if (RotatorHasPosition())
        DrawUIText(customFont, TextFormat("Current: AZ %.1f  EL %.1f", RotatorGetAz(), RotatorGetEl()), inner_x, ry + 16 * scale, 14 * scale, cfg->text_main);

    bool poll_pressed = GuiButton((Rectangle){inner_x + inner_w - 82 * scale, ry, 82 * scale, 24 * scale}, "Poll");
    if (interactive && poll_pressed)
        RotatorPollNow();

    y = sec_conn.y + sec_conn.height + section_gap;
    Rectangle sec_proto = {content_x, y, content_w, 86 * scale};
    DrawRotatorSection(sec_proto, "Protocol", cfg, customFont);

    ry = sec_proto.y + 28 * scale;
    GuiLabel((Rectangle){sec_proto.x + 8 * scale, ry, 56 * scale, 24 * scale}, "Get:");
    bool get_toggled = GuiTextBox((Rectangle){sec_proto.x + 64 * scale, ry, sec_proto.width - 72 * scale, 24 * scale}, get_fmt, RotatorGetGetFmtBufferSize(), interactive && rot_edit_get_fmt);
    if (interactive && get_toggled)
        rot_edit_get_fmt = !rot_edit_get_fmt;
    ry += 28 * scale;
    GuiLabel((Rectangle){sec_proto.x + 8 * scale, ry, 56 * scale, 24 * scale}, "Set:");
    bool set_toggled = GuiTextBox((Rectangle){sec_proto.x + 64 * scale, ry, sec_proto.width - 72 * scale, 24 * scale}, set_fmt, RotatorGetSetFmtBufferSize(), interactive && rot_edit_set_fmt);
    if (interactive && set_toggled)
        rot_edit_set_fmt = !rot_edit_set_fmt;

    y = sec_proto.y + sec_proto.height + section_gap;
    Rectangle sec_steer = {content_x, y, content_w, 92 * scale};
    DrawRotatorSection(sec_steer, "Steering", cfg, customFont);

    ry = sec_steer.y + 28 * scale;
    bool auto_steer = RotatorGetAutoSteer();
    GuiCheckBox((Rectangle){sec_steer.x + 8 * scale, ry + 2 * scale, 20 * scale, 20 * scale}, "Auto steer", &auto_steer);
    if (interactive)
        RotatorSetAutoSteer(auto_steer);

    int old_border = GuiGetStyle(BUTTON, BORDER_COLOR_NORMAL);
    float polar_btn_w = 64 * scale;
    float scope_btn_w = 64 * scale;
    float btn_gap = 6 * scale;
    float polar_x = sec_steer.x + 128 * scale;
    float scope_x = polar_x + polar_btn_w + btn_gap;
    if (RotatorGetSteerMode() == ROTATOR_STEER_POLAR)
        GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, ColorToInt(cfg->ui_accent));
    bool polar_mode_pressed = GuiButton((Rectangle){polar_x, ry, polar_btn_w, 24 * scale}, "Polar");
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, old_border);
    if (interactive && polar_mode_pressed)
        RotatorSetSteerMode(ROTATOR_STEER_POLAR);

    if (RotatorGetSteerMode() == ROTATOR_STEER_SCOPE)
        GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, ColorToInt(cfg->ui_accent));
    bool scope_mode_pressed = GuiButton((Rectangle){scope_x, ry, scope_btn_w, 24 * scale}, "Scope");
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, old_border);
    if (interactive && scope_mode_pressed)
        RotatorSetSteerMode(ROTATOR_STEER_SCOPE);

    ry += 32 * scale;
    char *lead_time = RotatorGetLeadTimeBuffer();
    float lead_label_w = 64 * scale;
    float lead_input_w = 52 * scale;
    GuiLabel((Rectangle){sec_steer.x + 8 * scale, ry, lead_label_w, 24 * scale}, "Lead (s):");
    AdvancedTextBox((Rectangle){sec_steer.x + 8 * scale + lead_label_w + 4 * scale, ry, lead_input_w, 24 * scale}, lead_time, RotatorGetLeadTimeBufferSize(), &rot_edit_lead_time, true);
    float park_label_x = sec_steer.x + 8 * scale + lead_label_w + 4 * scale + lead_input_w + 16 * scale;
    GuiLabel((Rectangle){park_label_x, ry, 40 * scale, 24 * scale}, "Park:");
    float park_az_x = park_label_x + 44 * scale;
    float park_el_x = park_az_x + 60 * scale;
    AdvancedTextBox((Rectangle){park_az_x, ry, 56 * scale, 24 * scale}, park_az, RotatorGetParkAzBufferSize(), &rot_edit_park_az, true);
    AdvancedTextBox((Rectangle){park_el_x, ry, 56 * scale, 24 * scale}, park_el, RotatorGetParkElBufferSize(), &rot_edit_park_el, true);
    bool set_pressed = GuiButton((Rectangle){sec_steer.x + sec_steer.width - 62 * scale - 8 * scale, ry, 62 * scale, 24 * scale}, "Set");
    if (interactive && set_pressed)
        RotatorSetParkNow((float)atof(park_az), (float)atof(park_el));

    y = sec_steer.y + sec_steer.height + section_gap;
    Rectangle sec_custom = {content_x, y, content_w, 66 * scale};
    DrawRotatorSection(sec_custom, "Command", cfg, customFont);

    ry = sec_custom.y + 28 * scale;
    GuiLabel((Rectangle){sec_custom.x + 8 * scale, ry, 56 * scale, 24 * scale}, "Raw:");
    bool custom_toggled = GuiTextBox((Rectangle){sec_custom.x + 56 * scale, ry, sec_custom.width - 130 * scale, 24 * scale}, custom_cmd, RotatorGetCustomCmdBufferSize(), interactive && rot_edit_custom_cmd);
    if (interactive && custom_toggled)
        rot_edit_custom_cmd = !rot_edit_custom_cmd;
    bool send_pressed = GuiButton((Rectangle){sec_custom.x + sec_custom.width - 66 * scale - 8 * scale, ry, 66 * scale, 24 * scale}, "Send");
    if (interactive && send_pressed)
        RotatorSendCustomNow();

    DrawUIText(customFont, RotatorGetStatus(), r.x + 12 * scale, r.y + r.height - 18 * scale, 13 * scale, cfg->text_secondary);
}

float RotatorConnectedItemWidth(AppConfig *cfg, Font customFont)
{
    float rot_text_w = MeasureTextEx(customFont, "ROTATOR CONNECTED", 16 * cfg->ui_scale, 1.0f).x;
    return 10 * cfg->ui_scale + rot_text_w;
}

void RotatorDrawConnectedItem(AppConfig *cfg, Font customFont, float x, float y)
{
    Color rot_col = (Color){90, 240, 170, 255};
    float blink = (sinf(GetTime() * 6.0f) * 0.5f + 0.5f);
    DrawCircleV((Vector2){x, y + 6 * cfg->ui_scale}, 4.0f * cfg->ui_scale, ApplyAlpha(rot_col, 0.35f + 0.65f * blink));
    DrawUIText(customFont, "ROTATOR CONNECTED", x + 10 * cfg->ui_scale, y, 16 * cfg->ui_scale, rot_col);
}

void RotatorDrawPolarOverlay(AppConfig *cfg, Font customFont, float cx, float cy, float r_max, float pl_x, float pl_y)
{
    if (!RotatorHasPosition())
        return;
    float r_rot = r_max * (90.0f - RotatorGetEl()) / 90.0f;
    if (r_rot < 0.0f)
        r_rot = 0.0f;
    if (r_rot > r_max)
        r_rot = r_max;
    Vector2 pt_rot = {cx + r_rot * sinf(RotatorGetAz() * DEG2RAD), cy - r_rot * cosf(RotatorGetAz() * DEG2RAD)};
    DrawCircleV(pt_rot, 4.0f * cfg->ui_scale, (Color){90, 240, 170, 255});
    DrawCircleLines(pt_rot.x, pt_rot.y, 6.0f * cfg->ui_scale, WHITE);
}

void RotatorDrawScopeOverlay(
    AppConfig *cfg, Font customFont, float center_x, float center_y, float scope_radius, float scope_az, float scope_el, float scope_beam, float sc_x, float ctrl_y
)
{
    if (!RotatorHasPosition())
        return;

    float c_az_rad = scope_az * DEG2RAD;
    float c_el_rad = scope_el * DEG2RAD;
    float r_az_rad = RotatorGetAz() * DEG2RAD;
    float r_el_rad = RotatorGetEl() * DEG2RAD;
    float rad_beam_half = (scope_beam / 2.0f) * DEG2RAD;

    float r_cos_theta = sinf(c_el_rad) * sinf(r_el_rad) + cosf(c_el_rad) * cosf(r_el_rad) * cosf(r_az_rad - c_az_rad);
    if (r_cos_theta < -1.0f)
        r_cos_theta = -1.0f;
    if (r_cos_theta > 1.0f)
        r_cos_theta = 1.0f;
    float r_theta = acosf(r_cos_theta);
    if (r_theta <= rad_beam_half)
    {
        float r_dx = cosf(r_el_rad) * sinf(r_az_rad - c_az_rad);
        float r_dy = cosf(c_el_rad) * sinf(r_el_rad) - sinf(c_el_rad) * cosf(r_el_rad) * cosf(r_az_rad - c_az_rad);
        float r_dist = (r_theta / rad_beam_half) * scope_radius;
        float r_angle = atan2f(-r_dy, r_dx);
        Vector2 rot_pt = {center_x + r_dist * cosf(r_angle), center_y + r_dist * sinf(r_angle)};
        DrawCircleLines(rot_pt.x, rot_pt.y, 8.0f * cfg->ui_scale, (Color){90, 240, 170, 255});
        DrawCircleV(rot_pt, 2.5f * cfg->ui_scale, (Color){90, 240, 170, 255});
    }

    DrawUIText(customFont, TextFormat("Rotator: %.1f / %.1f", RotatorGetAz(), RotatorGetEl()), sc_x + 15 * cfg->ui_scale, ctrl_y + 24 * cfg->ui_scale, 14 * cfg->ui_scale, (Color){90, 240, 170, 255});
}

/* main ui rendering loop */
void DrawGUI(UIContext *ctx, AppConfig *cfg, Font customFont)
{
    FinishPullIfDone(ctx, cfg);

    *ctx->show_scope = show_scope_dialog;
    *ctx->scope_az = scope_az;
    *ctx->scope_el = scope_el;
    *ctx->scope_beam = scope_beam;

    if (*ctx->selected_sat != last_selected_sat)
    {
        if (*ctx->selected_sat != NULL)
        {
            show_sat_info_dialog = true;
            if (!si_has_been_placed)
            {
                FindSmartWindowPosition(320 * cfg->ui_scale, 480 * cfg->ui_scale, cfg, &si_x, &si_y);
                si_has_been_placed = true;
            }
            BringToFront(WND_SAT_INFO);
        }
        else
        {
            show_sat_info_dialog = false;
        }
        last_selected_sat = *ctx->selected_sat;
    }

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_F))
    {
        if (!show_sat_mgr_dialog)
        {
            FindSmartWindowPosition(400 * cfg->ui_scale, 500 * cfg->ui_scale, cfg, &sm_x, &sm_y);
        }
        show_sat_mgr_dialog = true;
        BringToFront(WND_SAT_MGR);
        
        edit_year = edit_month = edit_day = edit_hour = edit_min = edit_sec = edit_unix = false;
        edit_doppler_freq = edit_doppler_res = edit_doppler_file = false;
        edit_min_el = false;
        edit_hl_name = edit_hl_lat = edit_hl_lon = edit_hl_alt = false;
        edit_fps = false;
        edit_scope_az = edit_scope_el = edit_scope_beam = false;
        
        edit_sat_search = true;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (IsUITyping())
        {
            edit_year = edit_month = edit_day = edit_hour = edit_min = edit_sec = edit_unix = false;
            edit_doppler_freq = edit_doppler_res = edit_doppler_file = false;
            edit_sat_search = edit_min_el = false;
            edit_hl_name = edit_hl_lat = edit_hl_lon = edit_hl_alt = false;
            edit_fps = false;
            edit_scope_az = edit_scope_el = edit_scope_beam = false;
        }
        else if (*ctx->selected_sat != NULL)
        {
            *ctx->selected_sat = NULL;
        }
        else if (!cfg->show_first_run_dialog)
        {
            show_exit_dialog = !show_exit_dialog;
        }
    }

    /* calculate interactive window rects */
    Rectangle helpWindow = {hw_x, hw_y, HELP_WINDOW_W * cfg->ui_scale, HELP_WINDOW_H * cfg->ui_scale};
    Rectangle settingsWindow = {sw_x, sw_y, 250 * cfg->ui_scale, 520 * cfg->ui_scale};
    Rectangle timeWindow = {td_x, td_y, 252 * cfg->ui_scale, 320 * cfg->ui_scale};
    Rectangle tleWindow = {(GetScreenWidth() - 300 * cfg->ui_scale) / 2.0f, (GetScreenHeight() - 130 * cfg->ui_scale) / 2.0f, 300 * cfg->ui_scale, 130 * cfg->ui_scale};
    Rectangle passesWindow = {pd_x, pd_y, 357 * cfg->ui_scale, 380 * cfg->ui_scale};
    Rectangle polarWindow = {pl_x, pl_y, 300 * cfg->ui_scale, 430 * cfg->ui_scale};
    Rectangle dopplerWindow = {dop_x, dop_y, 320 * cfg->ui_scale, 480 * cfg->ui_scale};
    Rectangle smWindow = {sm_x, sm_y, 400 * cfg->ui_scale, 500 * cfg->ui_scale};
    Rectangle tmMgrWindow = {tm_x, tm_y, 400 * cfg->ui_scale, 500 * cfg->ui_scale};
    Rectangle scopeWindow = {sc_x, sc_y, 360 * cfg->ui_scale, 560 * cfg->ui_scale};
    Rectangle satInfoWindow = {si_x, si_y, 320 * cfg->ui_scale, si_rolled_up ? 24 * cfg->ui_scale : 480 * cfg->ui_scale};
    Rectangle rotWindow = RotatorGetWindowRect(cfg);

    /* process Z-Order mouse events safely by evaluating from top to bottom */
    int top_hovered_wnd = -1;
    Vector2 m = GetMousePosition();
    for (int i = WND_MAX - 1; i >= 0; i--)
    {
        WindowID id = z_order[i];
        if (id == WND_HELP && show_help && CheckCollisionPointRec(m, helpWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_SETTINGS && show_settings && CheckCollisionPointRec(m, settingsWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_TIME && show_time_dialog && CheckCollisionPointRec(m, timeWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_PASSES && show_passes_dialog && CheckCollisionPointRec(m, passesWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_POLAR && show_polar_dialog && CheckCollisionPointRec(m, polarWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_DOPPLER && show_doppler_dialog && CheckCollisionPointRec(m, dopplerWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_SAT_MGR && show_sat_mgr_dialog && CheckCollisionPointRec(m, smWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_TLE_MGR && show_tle_mgr_dialog && CheckCollisionPointRec(m, tmMgrWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_SCOPE && show_scope_dialog && CheckCollisionPointRec(m, scopeWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_SAT_INFO && show_sat_info_dialog && CheckCollisionPointRec(m, satInfoWindow))
        {
            top_hovered_wnd = id;
            break;
        }
        if (id == WND_ROTATOR && RotatorIsWindowVisible() && CheckCollisionPointRec(m, rotWindow))
        {
            top_hovered_wnd = id;
            break;
        }
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        if (top_hovered_wnd != -1)
        {
            BringToFront((WindowID)top_hovered_wnd);

            /* assign specific window drags if clicking titlebars */
            WindowID top = (WindowID)top_hovered_wnd;
            if (top == WND_DOPPLER && CheckCollisionPointRec(m, (Rectangle){dop_x, dop_y, dopplerWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_doppler = true;
                drag_doppler_off = Vector2Subtract(m, (Vector2){dop_x, dop_y});
            }
            else if (top == WND_POLAR && CheckCollisionPointRec(m, (Rectangle){pl_x, pl_y, polarWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_polar = true;
                drag_polar_off = Vector2Subtract(m, (Vector2){pl_x, pl_y});
            }
            else if (top == WND_PASSES && CheckCollisionPointRec(m, (Rectangle){pd_x, pd_y, passesWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_passes = true;
                drag_passes_off = Vector2Subtract(m, (Vector2){pd_x, pd_y});
            }
            else if (top == WND_TIME && CheckCollisionPointRec(m, (Rectangle){td_x, td_y, timeWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_time_dialog = true;
                drag_time_off = Vector2Subtract(m, (Vector2){td_x, td_y});
            }
            else if (top == WND_SETTINGS && CheckCollisionPointRec(m, (Rectangle){sw_x, sw_y, settingsWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_settings = true;
                drag_settings_off = Vector2Subtract(m, (Vector2){sw_x, sw_y});
            }
            else if (top == WND_HELP && CheckCollisionPointRec(m, (Rectangle){hw_x, hw_y, helpWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_help = true;
                drag_help_off = Vector2Subtract(m, (Vector2){hw_x, hw_y});
            }
            else if (top == WND_SAT_MGR && CheckCollisionPointRec(m, (Rectangle){sm_x, sm_y, smWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_sat_mgr = true;
                drag_sat_mgr_off = Vector2Subtract(m, (Vector2){sm_x, sm_y});
            }
            else if (top == WND_TLE_MGR && CheckCollisionPointRec(m, (Rectangle){tm_x, tm_y, tmMgrWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_tle_mgr = true;
                drag_tle_mgr_off = Vector2Subtract(m, (Vector2){tm_x, tm_y});
            }
            else if (top == WND_SCOPE && CheckCollisionPointRec(m, (Rectangle){sc_x, sc_y, scopeWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_scope = true;
                drag_scope_off = Vector2Subtract(m, (Vector2){sc_x, sc_y});
            }
            else if (top == WND_SAT_INFO && CheckCollisionPointRec(m, (Rectangle){si_x, si_y, satInfoWindow.width - 30 * cfg->ui_scale, 24 * cfg->ui_scale}))
            {
                drag_sat_info = true;
                drag_sat_info_off = Vector2Subtract(m, (Vector2){si_x, si_y});
            }
            else if (top == WND_ROTATOR)
                RotatorBeginDrag(m, cfg);
        }
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    {
        drag_help = drag_settings = drag_time_dialog = drag_passes = drag_polar = drag_doppler = drag_sat_mgr = drag_tle_mgr = drag_scope = drag_sat_info = false;
        RotatorEndDrag();
    }

    if (show_passes_dialog)
    {
        if (multi_pass_mode)
        {
            if (last_pass_calc_sat != NULL || (num_passes > 0 && *ctx->current_epoch > passes[0].los_epoch + 1.0 / 1440.0))
            {
                CalculatePasses(NULL, *ctx->current_epoch);
            }
        }
        else
        {
            if (*ctx->selected_sat == NULL)
            {
                num_passes = 0;
                last_pass_calc_sat = NULL;
            }
            else if (last_pass_calc_sat != *ctx->selected_sat || (num_passes > 0 && *ctx->current_epoch > passes[0].los_epoch + 1.0 / 1440.0))
            {
                CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
            }
        }
    }

    if (show_polar_dialog && locked_pass_sat != NULL)
    {
        int found_idx = -1;
        for (int i = 0; i < num_passes; i++)
        {
            if (passes[i].sat == locked_pass_sat && fabs(passes[i].aos_epoch - locked_pass_aos) < (1.0 / 86400.0))
            {
                found_idx = i;
                break;
            }
        }
        if (found_idx == -1 && *ctx->current_epoch > locked_pass_los)
        {
            for (int i = 0; i < num_passes; i++)
            {
                if (passes[i].sat == locked_pass_sat && passes[i].los_epoch > *ctx->current_epoch)
                {
                    found_idx = i;
                    locked_pass_aos = passes[i].aos_epoch;
                    locked_pass_los = passes[i].los_epoch;
                    break;
                }
            }
        }
        selected_pass_idx = found_idx;
    }

    static char last_hl_name[64] = "";

    if (!edit_hl_lat && !edit_hl_lon && !edit_hl_alt && !edit_hl_name)
    {
        if (home_location.lat != last_hl_lat || home_location.lon != last_hl_lon || 
            home_location.alt != last_hl_alt || strcmp(home_location.name, last_hl_name) != 0)
        {
            sprintf(text_hl_lat, "%.4f", home_location.lat);
            sprintf(text_hl_lon, "%.4f", home_location.lon);
            sprintf(text_hl_alt, "%.4f", home_location.alt);
            strncpy(text_hl_name, home_location.name, 63);
            text_hl_name[63] = '\0';
            
            last_hl_lat = home_location.lat;
            last_hl_lon = home_location.lon;
            last_hl_alt = home_location.alt;
            strncpy(last_hl_name, home_location.name, 63);
            last_hl_name[63] = '\0';
        }
    }

    /* configure raygui global styles */
    GuiSetFont(customFont);
    GuiSetIconScale((int)fmaxf(1.0f, roundf(cfg->ui_scale)));
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16 * cfg->ui_scale);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(cfg->ui_primary));
    GuiSetStyle(DEFAULT, LINE_COLOR, ColorToInt(cfg->ui_secondary));

    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, ColorToInt(cfg->ui_primary));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, ColorToInt(cfg->ui_secondary));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, ColorToInt(cfg->ui_accent));
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, ColorToInt(cfg->ui_primary));

    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(cfg->window_border));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, ColorToInt(cfg->window_border));

    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(cfg->text_main));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(cfg->text_main));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(cfg->text_main));
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, ColorToInt(cfg->text_secondary));

    GuiSetStyle(CHECKBOX, TEXT_PADDING, 8 * cfg->ui_scale);

    GuiSetStyle(LISTVIEW, SCROLLBAR_WIDTH, 12 * cfg->ui_scale);
    GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, 28 * cfg->ui_scale);
    GuiSetStyle(SCROLLBAR, SCROLL_SLIDER_SIZE, 16 * cfg->ui_scale);
    GuiSetStyle(SCROLLBAR, ARROWS_SIZE, 6 * cfg->ui_scale);
    GuiSetStyle(SCROLLBAR, SCROLL_SPEED, 12 * cfg->ui_scale);

    GuiSetStyle(TEXTBOX, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
    GuiSetStyle(TEXTBOX, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));
    GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED, ColorToInt(cfg->text_main));
    GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, ColorToInt(cfg->text_main));
    GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, ColorToInt(cfg->ui_primary));

    /* render context-sensitive apsis text markers */
    bool hide_apsis_text = (*ctx->is_pov_mode && *ctx->selected_sat != NULL && ctx->active_sat == *ctx->selected_sat);
    if (ctx->active_sat && ctx->active_sat->is_active && !hide_apsis_text)
    {
        Vector2 periScreen, apoScreen;
        bool show_peri = true, show_apo = true;
        double t_peri_unix, t_apo_unix;
        get_apsis_times(ctx->active_sat, *ctx->current_epoch, &t_peri_unix, &t_apo_unix);

        double real_rp = Vector3Length(calculate_position(ctx->active_sat, t_peri_unix));
        double real_ra = Vector3Length(calculate_position(ctx->active_sat, t_apo_unix));

        if (*ctx->is_2d_view)
        {
            Vector2 p2, a2;
            get_apsis_2d(ctx->active_sat, *ctx->current_epoch, false, ctx->gmst_deg, cfg->earth_rotation_offset, ctx->map_w, ctx->map_h, &p2);
            get_apsis_2d(ctx->active_sat, *ctx->current_epoch, true, ctx->gmst_deg, cfg->earth_rotation_offset, ctx->map_w, ctx->map_h, &a2);
            float cam_x = ctx->camera2d->target.x;

            while (p2.x - cam_x > ctx->map_w / 2.0f)
                p2.x -= ctx->map_w;
            while (p2.x - cam_x < -ctx->map_w / 2.0f)
                p2.x += ctx->map_w;
            while (a2.x - cam_x > ctx->map_w / 2.0f)
                a2.x -= ctx->map_w;
            while (a2.x - cam_x < -ctx->map_w / 2.0f)
                a2.x += ctx->map_w;

            periScreen = GetWorldToScreen2D(p2, *ctx->camera2d);
            apoScreen = GetWorldToScreen2D(a2, *ctx->camera2d);
        }
        else
        {
            Vector3 draw_p = Vector3Scale(calculate_position(ctx->active_sat, t_peri_unix), 1.0f / DRAW_SCALE);
            Vector3 draw_a = Vector3Scale(calculate_position(ctx->active_sat, t_apo_unix), 1.0f / DRAW_SCALE);
            if (IsOccludedByEarth(ctx->camera3d->position, draw_p, EARTH_RADIUS_KM / DRAW_SCALE))
                show_peri = false;
            if (IsOccludedByEarth(ctx->camera3d->position, draw_a, EARTH_RADIUS_KM / DRAW_SCALE))
                show_apo = false;
            periScreen = GetWorldToScreen(draw_p, *ctx->camera3d);
            apoScreen = GetWorldToScreen(draw_a, *ctx->camera3d);
        }

        float text_size = 14.0f * cfg->ui_scale;
        float x_offset = 20.0f * cfg->ui_scale, y_offset = text_size / 2.2f;
        if (show_peri)
            DrawUIText(customFont, TextFormat("Peri: %.0f km", real_rp - EARTH_RADIUS_KM), periScreen.x + x_offset, periScreen.y - y_offset, text_size, cfg->periapsis);
        if (show_apo)
            DrawUIText(customFont, TextFormat("Apo: %.0f km", real_ra - EARTH_RADIUS_KM), apoScreen.x + x_offset, apoScreen.y - y_offset, text_size, cfg->apoapsis);
    }

    int normal_border = ColorToInt(cfg->window_border);
    int accent_border = ColorToInt(cfg->window_border_focus);

    float buttons_w = (6 * 35 - 5) * cfg->ui_scale;
    float center_x_bottom = (GetScreenWidth() - buttons_w) / 2.0f;
    float btn_start_x = center_x_bottom;
    float center_x_top = (GetScreenWidth() - (13 * 35 - 5) * cfg->ui_scale) / 2.0f;

    /* top/bottom bar buttons */
    Rectangle btnSet = {center_x_top, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnTLEMgr = {center_x_top + 35 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnSatMgr = {center_x_top + 70 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnPasses = {center_x_top + 105 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnPolar = {center_x_top + 140 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnScope = {center_x_top + 175 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnHelp = {center_x_top + 210 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btn2D3D = {center_x_top + 245 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnHideUnselected = {center_x_top + 280 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnSunlit = {center_x_top + 315 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnSlantRange = {center_x_top + 350 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnFrame = {center_x_top + 385 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnPOV = {center_x_top + 420 * cfg->ui_scale, 10 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnRewind = {btn_start_x, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnPlayPause = {btn_start_x + 35 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnFastForward = {btn_start_x + 70 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnNow = {btn_start_x + 105 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnClock = {btn_start_x + 140 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};
    Rectangle btnRotator = {btn_start_x + 175 * cfg->ui_scale, GetScreenHeight() - 40 * cfg->ui_scale, 30 * cfg->ui_scale, 30 * cfg->ui_scale};

    /* main toolbar rendering */
    bool toolbar_blocked_by_window = (top_hovered_wnd != -1);
    int old_toolbar_disabled_text = GuiGetStyle(DEFAULT, TEXT_COLOR_DISABLED);
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, GuiGetStyle(DEFAULT, TEXT_COLOR_NORMAL));
    if (toolbar_blocked_by_window)
        GuiDisable();

    int normal_text = ColorToInt(cfg->text_main);
    int disabled_text = toolbar_blocked_by_window ? normal_text : ColorToInt(cfg->text_secondary);

#define HIGHLIGHT_START(cond)                                                                                                                                                                          \
    if (cond)                                                                                                                                                                                          \
    {                                                                                                                                                                                                  \
        GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, accent_border);                                                                                                                                      \
        GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, accent_border);                                                                                                                                    \
        GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, normal_text);                                                                                                                                        \
    }
#define HIGHLIGHT_END()                                                                                                                                                                                \
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, normal_border);                                                                                                                                          \
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, normal_border);                                                                                                                                        \
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, disabled_text);

    HIGHLIGHT_START(show_settings)
    if (GuiButton(btnSet, "#142#") || (!IsUITyping() && IsKeyPressed(KEY_ONE)))
    {
        if (!show_settings)
        {
            FindSmartWindowPosition(250 * cfg->ui_scale, 520 * cfg->ui_scale, cfg, &sw_x, &sw_y);
            sprintf(text_fps, "%d", cfg->target_fps);
        }
        show_settings = !show_settings;
        BringToFront(WND_SETTINGS);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_help)
    if (GuiButton(btnHelp, "#193#") || (!IsUITyping() && IsKeyPressed(KEY_SEVEN)))
    {
        if (!show_help)
        {
            FindSmartWindowPosition(HELP_WINDOW_W * cfg->ui_scale, HELP_WINDOW_H * cfg->ui_scale, cfg, &hw_x, &hw_y);
        }
        show_help = !show_help;
        BringToFront(WND_HELP);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(*ctx->is_2d_view)
    if (GuiButton(btn2D3D, *ctx->is_2d_view ? "#161#" : "#160#") || (!IsUITyping() && IsKeyPressed(KEY_EIGHT)))
        *ctx->is_2d_view = !*ctx->is_2d_view;
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_sat_mgr_dialog)
    if (GuiButton(btnSatMgr, "#43#") || (!IsUITyping() && IsKeyPressed(KEY_THREE)))
    {
        if (!show_sat_mgr_dialog)
        {
            FindSmartWindowPosition(400 * cfg->ui_scale, 500 * cfg->ui_scale, cfg, &sm_x, &sm_y);
        }
        show_sat_mgr_dialog = !show_sat_mgr_dialog;
        BringToFront(WND_SAT_MGR);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(*ctx->hide_unselected)
    if (GuiButton(btnHideUnselected, *ctx->hide_unselected ? "#44#" : "#45#") || (!IsUITyping() && IsKeyPressed(KEY_NINE)))
        *ctx->hide_unselected = !*ctx->hide_unselected;
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_tle_mgr_dialog)
    if (GuiButton(btnTLEMgr, "#1#") || (!IsUITyping() && IsKeyPressed(KEY_TWO)))
    {
        if (!show_tle_mgr_dialog)
        {
            FindSmartWindowPosition(400 * cfg->ui_scale, 500 * cfg->ui_scale, cfg, &tm_x, &tm_y);
        }
        show_tle_mgr_dialog = !show_tle_mgr_dialog;
        BringToFront(WND_TLE_MGR);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(cfg->highlight_sunlit)
    if (GuiButton(btnSunlit, "#147#") || (!IsUITyping() && IsKeyPressed(KEY_ZERO)))
        cfg->highlight_sunlit = !cfg->highlight_sunlit;
    HIGHLIGHT_END()

    HIGHLIGHT_START(cfg->show_slant_range)
    if (GuiButton(btnSlantRange, "#34#"))
        cfg->show_slant_range = !cfg->show_slant_range;
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_polar_dialog)
    if (GuiButton(btnPolar, "#64#") || (!IsUITyping() && IsKeyPressed(KEY_FIVE)))
    {
        if (!show_polar_dialog)
        {
            FindSmartWindowPosition(300 * cfg->ui_scale, 430 * cfg->ui_scale, cfg, &pl_x, &pl_y);
        }
        show_polar_dialog = !show_polar_dialog;
        BringToFront(WND_POLAR);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_scope_dialog)
    if (GuiButton(btnScope, "#103#") || (!IsUITyping() && IsKeyPressed(KEY_SIX)))
    {
        if (!show_scope_dialog)
        {
            FindSmartWindowPosition(360 * cfg->ui_scale, 560 * cfg->ui_scale, cfg, &sc_x, &sc_y);
        }
        show_scope_dialog = !show_scope_dialog;
        BringToFront(WND_SCOPE);
    }
    HIGHLIGHT_END()

    if (*ctx->is_pov_mode) GuiDisable();
    HIGHLIGHT_START(*ctx->is_ecliptic_frame)
    if (GuiButton(btnFrame, *ctx->is_ecliptic_frame ? "#104#" : "#105#"))
        *ctx->is_ecliptic_frame = !*ctx->is_ecliptic_frame;
    HIGHLIGHT_END()
    if (*ctx->is_pov_mode) GuiEnable();

    HIGHLIGHT_START(*ctx->is_pov_mode)
    if (GuiButton(btnPOV, *ctx->is_pov_mode ? "#44#" : "#42#"))
    {
        *ctx->is_pov_mode = !*ctx->is_pov_mode;
        if (*ctx->is_pov_mode) *ctx->is_ecliptic_frame = false;
    }
    HIGHLIGHT_END()

    if (GuiButton(btnRewind, "#118#"))
    {
        *ctx->is_auto_warping = false;
        *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, false);
    }

    HIGHLIGHT_START(*ctx->time_multiplier == 0.0)
    if (GuiButton(btnPlayPause, (*ctx->time_multiplier == 0.0) ? "#131#" : "#132#"))
    {
        *ctx->is_auto_warping = false;
        if (*ctx->time_multiplier != 0.0)
        {
            *ctx->saved_multiplier = *ctx->time_multiplier;
            *ctx->time_multiplier = 0.0;
        }
        else
        {
            *ctx->time_multiplier = *ctx->saved_multiplier != 0.0 ? *ctx->saved_multiplier : 1.0;
        }
    }
    HIGHLIGHT_END()

    if (GuiButton(btnFastForward, "#119#"))
    {
        *ctx->is_auto_warping = false;
        *ctx->time_multiplier = StepTimeMultiplier(*ctx->time_multiplier, true);
    }
    if (GuiButton(btnNow, "#211#"))
    {
        *ctx->is_auto_warping = false;
        *ctx->current_epoch = get_current_real_time_epoch();
        *ctx->time_multiplier = 1.0;
        *ctx->saved_multiplier = 1.0;
    }
    HIGHLIGHT_START(RotatorIsWindowVisible())
    if (GuiButton(btnRotator, "#65#") || (!IsUITyping() && IsKeyPressed(KEY_R)))
    {
        RotatorToggleWindow(cfg);
        BringToFront(WND_ROTATOR);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_time_dialog)
    if (GuiButton(btnClock, "#139#") || (!IsUITyping() && IsKeyPressed(KEY_GRAVE)))
    {
        if (!show_time_dialog)
        {
            FindSmartWindowPosition(252 * cfg->ui_scale, 320 * cfg->ui_scale, cfg, &td_x, &td_y);
            time_t t_unix = (time_t)get_unix_from_epoch(*ctx->current_epoch);
            struct tm *tm_info = gmtime(&t_unix);
            if (tm_info)
            {
                sprintf(text_year, "%d", tm_info->tm_year + 1900);
                sprintf(text_month, "%d", tm_info->tm_mon + 1);
                sprintf(text_day, "%d", tm_info->tm_mday);
                sprintf(text_hour, "%d", tm_info->tm_hour);
                sprintf(text_min, "%d", tm_info->tm_min);
                sprintf(text_sec, "%d", tm_info->tm_sec);
            }
            sprintf(text_unix, "%ld", (long)t_unix);
        }
        show_time_dialog = !show_time_dialog;
        BringToFront(WND_TIME);
    }
    HIGHLIGHT_END()

    HIGHLIGHT_START(show_passes_dialog)
    if (GuiButton(btnPasses, "#208#") || (!IsUITyping() && IsKeyPressed(KEY_FOUR)))
    {
        if (!show_passes_dialog)
        {
            FindSmartWindowPosition(357 * cfg->ui_scale, 380 * cfg->ui_scale, cfg, &pd_x, &pd_y);
            if (multi_pass_mode)
                CalculatePasses(NULL, *ctx->current_epoch);
            else if (*ctx->selected_sat)
                CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
            else
            {
                num_passes = 0;
                last_pass_calc_sat = NULL;
            }
        }
        show_passes_dialog = !show_passes_dialog;
        BringToFront(WND_PASSES);
    }
    HIGHLIGHT_END()

#undef HIGHLIGHT_START
#undef HIGHLIGHT_END

    RotatorUpdateControl(ctx, show_scope_dialog, show_polar_dialog, polar_lunar_mode, selected_pass_idx);

    if (toolbar_blocked_by_window)
        GuiEnable();
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, old_toolbar_disabled_text);

    /* Render dialogs respecting Z-Order to enforce click consumption logically */
    for (int win_idx = 0; win_idx < WND_MAX; win_idx++)
    {
        WindowID current_id = z_order[win_idx];
        bool is_topmost = (top_hovered_wnd == -1) || (top_hovered_wnd == current_id);
        bool window_blocked = !is_topmost;
        Vector2 mouse_pos = GetMousePosition();
        bool mouse_over_current_window = false;
        switch (current_id)
        {
        case WND_HELP:
            mouse_over_current_window = show_help && CheckCollisionPointRec(mouse_pos, helpWindow);
            break;
        case WND_SETTINGS:
            mouse_over_current_window = show_settings && CheckCollisionPointRec(mouse_pos, settingsWindow);
            break;
        case WND_TIME:
            mouse_over_current_window = show_time_dialog && CheckCollisionPointRec(mouse_pos, timeWindow);
            break;
        case WND_PASSES:
            mouse_over_current_window = show_passes_dialog && CheckCollisionPointRec(mouse_pos, passesWindow);
            break;
        case WND_POLAR:
            mouse_over_current_window = show_polar_dialog && CheckCollisionPointRec(mouse_pos, polarWindow);
            break;
        case WND_DOPPLER:
            mouse_over_current_window = show_doppler_dialog && CheckCollisionPointRec(mouse_pos, dopplerWindow);
            break;
        case WND_SAT_MGR:
            mouse_over_current_window = show_sat_mgr_dialog && CheckCollisionPointRec(mouse_pos, smWindow);
            break;
        case WND_TLE_MGR:
            mouse_over_current_window = show_tle_mgr_dialog && CheckCollisionPointRec(mouse_pos, tmMgrWindow);
            break;
        case WND_SCOPE:
            mouse_over_current_window = show_scope_dialog && CheckCollisionPointRec(mouse_pos, scopeWindow);
            break;
        case WND_SAT_INFO:
            mouse_over_current_window = show_sat_info_dialog && CheckCollisionPointRec(mouse_pos, satInfoWindow);
            break;
        case WND_ROTATOR:
            mouse_over_current_window = RotatorIsWindowVisible() && CheckCollisionPointRec(mouse_pos, rotWindow);
            break;
        default:
            break;
        }
        bool use_gui_disable = window_blocked && mouse_over_current_window && (current_id != WND_ROTATOR);
        int old_window_disabled_text = 0;
        int old_window_disabled_base = 0;
        int old_window_disabled_border = 0;
        int old_textbox_disabled_text = 0;
        int old_textbox_disabled_base = 0;
        int old_textbox_disabled_border = 0;
        int old_button_disabled_text = 0;
        int old_button_disabled_base = 0;
        int old_button_disabled_border = 0;
        int old_toggle_disabled_text = 0;
        int old_toggle_disabled_base = 0;
        int old_toggle_disabled_border = 0;
        int old_slider_disabled_text = 0;
        int old_slider_disabled_base = 0;
        int old_slider_disabled_border = 0;
        int old_checkbox_disabled_text = 0;
        int old_checkbox_disabled_base = 0;
        int old_checkbox_disabled_border = 0;
        if (use_gui_disable)
        {
            old_window_disabled_text = GuiGetStyle(DEFAULT, TEXT_COLOR_DISABLED);
            old_window_disabled_base = GuiGetStyle(DEFAULT, BASE_COLOR_DISABLED);
            old_window_disabled_border = GuiGetStyle(DEFAULT, BORDER_COLOR_DISABLED);
            old_textbox_disabled_text = GuiGetStyle(TEXTBOX, TEXT_COLOR_DISABLED);
            old_textbox_disabled_base = GuiGetStyle(TEXTBOX, BASE_COLOR_DISABLED);
            old_textbox_disabled_border = GuiGetStyle(TEXTBOX, BORDER_COLOR_DISABLED);
            old_button_disabled_text = GuiGetStyle(BUTTON, TEXT_COLOR_DISABLED);
            old_button_disabled_base = GuiGetStyle(BUTTON, BASE_COLOR_DISABLED);
            old_button_disabled_border = GuiGetStyle(BUTTON, BORDER_COLOR_DISABLED);
            old_toggle_disabled_text = GuiGetStyle(TOGGLE, TEXT_COLOR_DISABLED);
            old_toggle_disabled_base = GuiGetStyle(TOGGLE, BASE_COLOR_DISABLED);
            old_toggle_disabled_border = GuiGetStyle(TOGGLE, BORDER_COLOR_DISABLED);
            old_slider_disabled_text = GuiGetStyle(SLIDER, TEXT_COLOR_DISABLED);
            old_slider_disabled_base = GuiGetStyle(SLIDER, BASE_COLOR_DISABLED);
            old_slider_disabled_border = GuiGetStyle(SLIDER, BORDER_COLOR_DISABLED);
            old_checkbox_disabled_text = GuiGetStyle(CHECKBOX, TEXT_COLOR_DISABLED);
            old_checkbox_disabled_base = GuiGetStyle(CHECKBOX, BASE_COLOR_DISABLED);
            old_checkbox_disabled_border = GuiGetStyle(CHECKBOX, BORDER_COLOR_DISABLED);

            GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, GuiGetStyle(DEFAULT, TEXT_COLOR_NORMAL));
            GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL));
            GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, GuiGetStyle(DEFAULT, BORDER_COLOR_NORMAL));
            GuiSetStyle(TEXTBOX, TEXT_COLOR_DISABLED, GuiGetStyle(TEXTBOX, TEXT_COLOR_NORMAL));
            GuiSetStyle(TEXTBOX, BASE_COLOR_DISABLED, GuiGetStyle(TEXTBOX, BASE_COLOR_NORMAL));
            GuiSetStyle(TEXTBOX, BORDER_COLOR_DISABLED, GuiGetStyle(TEXTBOX, BORDER_COLOR_NORMAL));
            GuiSetStyle(BUTTON, TEXT_COLOR_DISABLED, GuiGetStyle(BUTTON, TEXT_COLOR_NORMAL));
            GuiSetStyle(BUTTON, BASE_COLOR_DISABLED, GuiGetStyle(BUTTON, BASE_COLOR_NORMAL));
            GuiSetStyle(BUTTON, BORDER_COLOR_DISABLED, GuiGetStyle(BUTTON, BORDER_COLOR_NORMAL));
            GuiSetStyle(TOGGLE, TEXT_COLOR_DISABLED, GuiGetStyle(TOGGLE, TEXT_COLOR_NORMAL));
            GuiSetStyle(TOGGLE, BASE_COLOR_DISABLED, GuiGetStyle(TOGGLE, BASE_COLOR_NORMAL));
            GuiSetStyle(TOGGLE, BORDER_COLOR_DISABLED, GuiGetStyle(TOGGLE, BORDER_COLOR_NORMAL));
            GuiSetStyle(SLIDER, TEXT_COLOR_DISABLED, GuiGetStyle(SLIDER, TEXT_COLOR_NORMAL));
            GuiSetStyle(SLIDER, BASE_COLOR_DISABLED, GuiGetStyle(SLIDER, BASE_COLOR_NORMAL));
            GuiSetStyle(SLIDER, BORDER_COLOR_DISABLED, GuiGetStyle(SLIDER, BORDER_COLOR_NORMAL));
            GuiSetStyle(CHECKBOX, TEXT_COLOR_DISABLED, GuiGetStyle(CHECKBOX, TEXT_COLOR_NORMAL));
            GuiSetStyle(CHECKBOX, BASE_COLOR_DISABLED, GuiGetStyle(CHECKBOX, BASE_COLOR_NORMAL));
            GuiSetStyle(CHECKBOX, BORDER_COLOR_DISABLED, GuiGetStyle(CHECKBOX, BORDER_COLOR_NORMAL));
            GuiDisable();
        }

        switch (current_id)
        {
        case WND_TLE_MGR:
        {
            if (!show_tle_mgr_dialog)
                break;
            LoadTLEState(cfg);

            if (drag_tle_mgr)
            {
                tm_x = GetMousePosition().x - drag_tle_mgr_off.x;
                tm_y = GetMousePosition().y - drag_tle_mgr_off.y;
                SnapWindow(&tm_x, &tm_y, tmMgrWindow.width, tmMgrWindow.height, cfg);
            }
            tmMgrWindow.x = tm_x; tmMgrWindow.y = tm_y;
            if (DrawMaterialWindow(tmMgrWindow, "#1# TLE Manager", cfg, customFont, true))
                show_tle_mgr_dialog = false;

            char age_str[64] = "TLE Age: Unknown";
            if (data_tle_epoch > 0)
            {
                long diff = time(NULL) - data_tle_epoch;
                if (diff < 3600)
                    sprintf(age_str, "TLE Age: %ld mins", diff / 60);
                else if (diff < 86400)
                    sprintf(age_str, "TLE Age: %ld hours, %ld mins", diff / 3600, (diff % 3600) / 60);
                else
                    sprintf(age_str, "TLE Age: %ld days, %ld hours", diff / 86400, (diff % 86400) / 3600);
            }
            DrawUIText(customFont, age_str, tm_x + 10 * cfg->ui_scale, tm_y + 35 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);

            {
                const char *btn_label = "Apply";
                if (pull_state == PULL_BUSY) btn_label = "Pulling..";
                else if (pull_state == PULL_ERROR) btn_label = "Error";
                else if (pull_partial) btn_label = "Partial";
                if (pull_state == PULL_BUSY) GuiDisable();
                if (GuiButton((Rectangle){tm_x + tmMgrWindow.width - 110 * cfg->ui_scale, tm_y + 30 * cfg->ui_scale, 100 * cfg->ui_scale, 26 * cfg->ui_scale}, btn_label))
                {
                    PullTLEData(cfg);
                }
                if (pull_state == PULL_BUSY) GuiEnable();
            }

            float total_height = 28 * cfg->ui_scale + (retlector_expanded ? NUM_RETLECTOR_SOURCES * 25 * cfg->ui_scale : 0);
            total_height += 28 * cfg->ui_scale + (celestrak_expanded ? 25 * 25 * cfg->ui_scale : 0);
            total_height += 28 * cfg->ui_scale + (other_expanded ? cfg->custom_tle_source_count * 25 * cfg->ui_scale : 0);
            total_height += 28 * cfg->ui_scale + (manual_expanded ? (60 * cfg->ui_scale + cfg->manual_tle_count * 25 * cfg->ui_scale) : 0);

            Rectangle contentRec = {0, 0, tmMgrWindow.width - 32 * cfg->ui_scale, total_height};
            Rectangle viewRec = {0};

            int oldFocusD = GuiGetStyle(DEFAULT, BORDER_COLOR_FOCUSED);
            int oldPressD = GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED);
            int oldFocusL = GuiGetStyle(LISTVIEW, BORDER_COLOR_FOCUSED);
            int oldPressL = GuiGetStyle(LISTVIEW, BORDER_COLOR_PRESSED);
            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));

            GuiScrollPanel((Rectangle){tm_x + 8 * cfg->ui_scale, tm_y + 65 * cfg->ui_scale, tmMgrWindow.width - 16 * cfg->ui_scale, tmMgrWindow.height - 65 * cfg->ui_scale - 8 * cfg->ui_scale}, NULL, contentRec, &tle_mgr_scroll, &viewRec);

            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, oldFocusD);
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, oldPressD);
            GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, oldFocusL);
            GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, oldPressL);

            BeginScissorMode(viewRec.x, viewRec.y, viewRec.width, viewRec.height);
            float current_y = viewRec.y + tle_mgr_scroll.y;

            Rectangle retlectorHead = {viewRec.x + tle_mgr_scroll.x, current_y, viewRec.width, 24 * cfg->ui_scale};
            if (is_topmost && CheckCollisionPointRec(GetMousePosition(), retlectorHead) && CheckCollisionPointRec(GetMousePosition(), viewRec))
            {
                DrawRectangleRec(retlectorHead, ApplyAlpha(cfg->ui_secondary, 0.4f));
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    retlector_expanded = !retlector_expanded;
            }
            else
                DrawRectangleRec(retlectorHead, ApplyAlpha(cfg->ui_secondary, 0.15f));
            DrawUIText(
                customFont, TextFormat("%s  RETLECTOR (Unrestricted)", retlector_expanded ? "v" : ">"), retlectorHead.x + 4 * cfg->ui_scale, retlectorHead.y + 4 * cfg->ui_scale, 16 * cfg->ui_scale,
                cfg->ui_accent
            );
            current_y += 28 * cfg->ui_scale;
            if (retlector_expanded)
            {
                for (int i = 0; i < NUM_RETLECTOR_SOURCES; i++)
                {
                    if (current_y + 25 * cfg->ui_scale >= viewRec.y && current_y <= viewRec.y + viewRec.height)
                    {
                        GuiCheckBox(
                            (Rectangle){viewRec.x + 10 * cfg->ui_scale + tle_mgr_scroll.x, current_y + 4 * cfg->ui_scale, 16 * cfg->ui_scale, 16 * cfg->ui_scale}, RETLECTOR_SOURCES[i].name,
                            &retlector_selected[i]
                        );
                    }
                    current_y += 25 * cfg->ui_scale;
                }
            }

            Rectangle celestrakHead = {viewRec.x + tle_mgr_scroll.x, current_y, viewRec.width, 24 * cfg->ui_scale};
            if (is_topmost && CheckCollisionPointRec(GetMousePosition(), celestrakHead) && CheckCollisionPointRec(GetMousePosition(), viewRec))
            {
                DrawRectangleRec(celestrakHead, ApplyAlpha(cfg->ui_secondary, 0.4f));
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    celestrak_expanded = !celestrak_expanded;
            }
            else
                DrawRectangleRec(celestrakHead, ApplyAlpha(cfg->ui_secondary, 0.15f));
            DrawUIText(
                customFont, TextFormat("%s  CELESTRAK (infrequent pulls only)", celestrak_expanded ? "v" : ">"), celestrakHead.x + 4 * cfg->ui_scale, celestrakHead.y + 4 * cfg->ui_scale,
                16 * cfg->ui_scale, cfg->ui_accent
            );
            current_y += 28 * cfg->ui_scale;
            if (celestrak_expanded)
            {
                for (int i = 0; i < 25; i++)
                {
                    if (current_y + 25 * cfg->ui_scale >= viewRec.y && current_y <= viewRec.y + viewRec.height)
                    {
                        GuiCheckBox(
                            (Rectangle){viewRec.x + 10 * cfg->ui_scale + tle_mgr_scroll.x, current_y + 4 * cfg->ui_scale, 16 * cfg->ui_scale, 16 * cfg->ui_scale}, SOURCES[i].name, &celestrak_selected[i]
                        );
                    }
                    current_y += 25 * cfg->ui_scale;
                }
            }

            Rectangle otherHead = {viewRec.x + tle_mgr_scroll.x, current_y, viewRec.width, 24 * cfg->ui_scale};
            if (is_topmost && CheckCollisionPointRec(GetMousePosition(), otherHead) && CheckCollisionPointRec(GetMousePosition(), viewRec))
            {
                DrawRectangleRec(otherHead, ApplyAlpha(cfg->ui_secondary, 0.4f));
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    other_expanded = !other_expanded;
            }
            else
                DrawRectangleRec(otherHead, ApplyAlpha(cfg->ui_secondary, 0.15f));
            DrawUIText(
                customFont, TextFormat("%s  CUSTOM SOURCES (settings.json)", other_expanded ? "v" : ">"), otherHead.x + 4 * cfg->ui_scale, otherHead.y + 4 * cfg->ui_scale, 16 * cfg->ui_scale,
                cfg->ui_accent
            );
            current_y += 28 * cfg->ui_scale;
            if (other_expanded)
            {
                for (int i = 0; i < cfg->custom_tle_source_count; i++)
                {
                    if (current_y + 25 * cfg->ui_scale >= viewRec.y && current_y <= viewRec.y + viewRec.height)
                    {
                        GuiCheckBox(
                            (Rectangle){viewRec.x + 10 * cfg->ui_scale + tle_mgr_scroll.x, current_y + 4 * cfg->ui_scale, 16 * cfg->ui_scale, 16 * cfg->ui_scale}, cfg->custom_tle_sources[i].name,
                            &cfg->custom_tle_sources[i].selected
                        );
                    }
                    current_y += 25 * cfg->ui_scale;
                }
            }

            Rectangle manualHead = {viewRec.x + tle_mgr_scroll.x, current_y, viewRec.width, 24 * cfg->ui_scale};
            if (is_topmost && CheckCollisionPointRec(GetMousePosition(), manualHead) && CheckCollisionPointRec(GetMousePosition(), viewRec))
            {
                DrawRectangleRec(manualHead, ApplyAlpha(cfg->ui_secondary, 0.4f));
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    manual_expanded = !manual_expanded;
            }
            else
                DrawRectangleRec(manualHead, ApplyAlpha(cfg->ui_secondary, 0.15f));
            DrawUIText(
                customFont, TextFormat("%s  MANUAL TLE ENTRY", manual_expanded ? "v" : ">"), manualHead.x + 4 * cfg->ui_scale, manualHead.y + 4 * cfg->ui_scale, 16 * cfg->ui_scale,
                cfg->ui_accent
            );
            current_y += 28 * cfg->ui_scale;
            if (manual_expanded)
            {
                if (current_y + 25 * cfg->ui_scale >= viewRec.y && current_y <= viewRec.y + viewRec.height)
                {
                    AdvancedTextBox((Rectangle){viewRec.x + 4 * cfg->ui_scale + tle_mgr_scroll.x, current_y, viewRec.width - 60 * cfg->ui_scale, 24 * cfg->ui_scale}, new_tle_buf, 512, &edit_new_tle, false);
                    if (GuiButton((Rectangle){viewRec.x + viewRec.width - 50 * cfg->ui_scale + tle_mgr_scroll.x, current_y, 45 * cfg->ui_scale, 24 * cfg->ui_scale}, "Add"))
                    {
                        if (strlen(new_tle_buf) > 10 && cfg->manual_tle_count < MAX_MANUAL_TLES)
                        {
                            char safe_tle[512] = {0};
                            int j = 0;
                            for(int i = 0; new_tle_buf[i] && j < 511; i++) {
                                if (new_tle_buf[i] == '\n' || new_tle_buf[i] == '\r') {
                                    if (j > 0 && safe_tle[j-1] != '|') safe_tle[j++] = '|';
                                } else {
                                    safe_tle[j++] = new_tle_buf[i];
                                }
                            }
                            safe_tle[j] = '\0';
                            strcpy(cfg->manual_tles[cfg->manual_tle_count++], safe_tle);
                            SaveAppConfig("settings.json", cfg);
                            ReloadTLEsLocally(ctx, cfg);
                            new_tle_buf[0] = '\0';
                        }
                    }
                }
                current_y += 30 * cfg->ui_scale;

                for (int i = 0; i < cfg->manual_tle_count; i++)
                {
                    if (current_y + 25 * cfg->ui_scale >= viewRec.y && current_y <= viewRec.y + viewRec.height)
                    {
                        char display_name[32] = {0};
                        strncpy(display_name, cfg->manual_tles[i], 24);
                        char *delim = strchr(display_name, '|');
                        if (delim) *delim = '\0';

                        GuiLabel((Rectangle){viewRec.x + 10 * cfg->ui_scale + tle_mgr_scroll.x, current_y, viewRec.width - 85 * cfg->ui_scale, 24 * cfg->ui_scale}, display_name);
                        
                        if (GuiButton((Rectangle){viewRec.x + viewRec.width - 70 * cfg->ui_scale + tle_mgr_scroll.x, current_y, 30 * cfg->ui_scale, 24 * cfg->ui_scale}, "C"))
                        {
                            char copy_buf[512];
                            strcpy(copy_buf, cfg->manual_tles[i]);
                            for (int c = 0; copy_buf[c] != '\0'; c++) {
                                if (copy_buf[c] == '|') copy_buf[c] = '\n';
                            }
                            SetClipboardText(copy_buf);
                        }

                        if (GuiButton((Rectangle){viewRec.x + viewRec.width - 35 * cfg->ui_scale + tle_mgr_scroll.x, current_y, 30 * cfg->ui_scale, 24 * cfg->ui_scale}, "X"))
                        {
                            for (int k = i; k < cfg->manual_tle_count - 1; k++) {
                                strcpy(cfg->manual_tles[k], cfg->manual_tles[k+1]);
                            }
                            cfg->manual_tle_count--;
                            SaveAppConfig("settings.json", cfg);
                            ReloadTLEsLocally(ctx, cfg);
                        }
                    }
                    current_y += 25 * cfg->ui_scale;
                }
            }

            EndScissorMode();
            break;
        }

        case WND_SAT_MGR:
        {
            if (!show_sat_mgr_dialog)
                break;
            if (drag_sat_mgr)
            {
                sm_x = GetMousePosition().x - drag_sat_mgr_off.x;
                sm_y = GetMousePosition().y - drag_sat_mgr_off.y;
                SnapWindow(&sm_x, &sm_y, smWindow.width, smWindow.height, cfg);
            }
            smWindow.x = sm_x; smWindow.y = sm_y;
            if (DrawMaterialWindow(smWindow, "#43# Satellite Manager", cfg, customFont, true))
                show_sat_mgr_dialog = false;

            AdvancedTextBox((Rectangle){sm_x + 10 * cfg->ui_scale, sm_y + 35 * cfg->ui_scale, smWindow.width - 90 * cfg->ui_scale, 24 * cfg->ui_scale}, sat_search_text, 64, &edit_sat_search, false);

            bool doCheckAll = GuiButton((Rectangle){sm_x + smWindow.width - 75 * cfg->ui_scale, sm_y + 35 * cfg->ui_scale, 30 * cfg->ui_scale, 24 * cfg->ui_scale}, "#80#");
            bool doUncheckAll = GuiButton((Rectangle){sm_x + smWindow.width - 40 * cfg->ui_scale, sm_y + 35 * cfg->ui_scale, 30 * cfg->ui_scale, 24 * cfg->ui_scale}, "#79#");

            int filtered_indices[MAX_SATELLITES], filtered_count = 0;
            for (int i = 0; i < sat_count; i++)
            {
                if (string_contains_ignore_case(satellites[i].name, sat_search_text) || 
                    string_contains_ignore_case(satellites[i].norad_id, sat_search_text) || 
                    string_contains_ignore_case(satellites[i].intl_designator, sat_search_text))

                {
                    filtered_indices[filtered_count++] = i;
                    if (doCheckAll)
                        satellites[i].is_active = true;
                    if (doUncheckAll)
                        satellites[i].is_active = false;
                }
            }

            if (doCheckAll || doUncheckAll)
            {
                SaveSatSelection();
                if (show_passes_dialog)
                {
                    if (multi_pass_mode)
                        CalculatePasses(NULL, *ctx->current_epoch);
                    else if (*ctx->selected_sat)
                        CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
                }
            }

            if (filtered_count > 0)
            {
                Rectangle contentRec = {0, 0, smWindow.width - 32 * cfg->ui_scale, filtered_count * 25 * cfg->ui_scale};
                Rectangle viewRec = {0};

                int oldFocusD = GuiGetStyle(DEFAULT, BORDER_COLOR_FOCUSED);
                int oldPressD = GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED);
                int oldFocusL = GuiGetStyle(LISTVIEW, BORDER_COLOR_FOCUSED);
                int oldPressL = GuiGetStyle(LISTVIEW, BORDER_COLOR_PRESSED);
                GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
                GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));
                GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
                GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));

                GuiScrollPanel((Rectangle){sm_x + 8 * cfg->ui_scale, sm_y + 70 * cfg->ui_scale, smWindow.width - 16 * cfg->ui_scale, smWindow.height - 70 * cfg->ui_scale - 8 * cfg->ui_scale}, NULL, contentRec, &sat_mgr_scroll, &viewRec);

                GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, oldFocusD);
                GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, oldPressD);
                GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, oldFocusL);
                GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, oldPressL);

                BeginScissorMode(viewRec.x, viewRec.y, viewRec.width, viewRec.height);
                for (int k = 0; k < filtered_count; k++)
                {
                    int sat_idx = filtered_indices[k];
                    float item_y = viewRec.y + sat_mgr_scroll.y + k * 25 * cfg->ui_scale;
                    if (item_y + 25 * cfg->ui_scale < viewRec.y || item_y > viewRec.y + viewRec.height)
                        continue;

                    Rectangle cbRec = {viewRec.x + 4 * cfg->ui_scale + sat_mgr_scroll.x, item_y + 4 * cfg->ui_scale, 16 * cfg->ui_scale, 16 * cfg->ui_scale};
                    Rectangle textRec = {viewRec.x + 24 * cfg->ui_scale + sat_mgr_scroll.x, item_y, viewRec.width - 28 * cfg->ui_scale, 25 * cfg->ui_scale};

                    bool was_active = satellites[sat_idx].is_active;
                    GuiCheckBox(cbRec, "", &satellites[sat_idx].is_active);

                    if (was_active != satellites[sat_idx].is_active)
                    {
                        SaveSatSelection();
                        if (show_passes_dialog)
                        {
                            if (multi_pass_mode)
                                CalculatePasses(NULL, *ctx->current_epoch);
                            else if (*ctx->selected_sat)
                                CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
                        }
                    }

                    bool isTargeted = (*ctx->selected_sat == &satellites[sat_idx]);
                    bool isHovered = is_topmost && CheckCollisionPointRec(GetMousePosition(), textRec) && CheckCollisionPointRec(GetMousePosition(), viewRec);

                    if (isTargeted)
                        DrawRectangleLinesEx(textRec, 1.5f * cfg->ui_scale, cfg->ui_accent);
                    else if (isHovered)
                        DrawRectangleLinesEx(textRec, 1.0f * cfg->ui_scale, ApplyAlpha(cfg->ui_secondary, 0.5f));

                    if (isHovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    {
                        if (isTargeted)
                            *ctx->selected_sat = NULL;
                        else
                            *ctx->selected_sat = &satellites[sat_idx];
                    }
                    DrawUIText(customFont, satellites[sat_idx].name, textRec.x + 4 * cfg->ui_scale, textRec.y + 4 * cfg->ui_scale, 16 * cfg->ui_scale, isTargeted ? cfg->ui_accent : cfg->text_main);
                }
                EndScissorMode();
            }
            else
            {
                const char *empty_msg = (sat_count == 0) ? "No orbital data loaded." : "No satellites match search.";
                Vector2 msg_size = MeasureTextEx(customFont, empty_msg, 16 * cfg->ui_scale, 1.0f);
                DrawUIText(customFont, empty_msg, sm_x + (smWindow.width - msg_size.x) / 2.0f, sm_y + 180 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
                if (GuiButton((Rectangle){sm_x + (smWindow.width - 200 * cfg->ui_scale) / 2.0f, sm_y + 220 * cfg->ui_scale, 200 * cfg->ui_scale, 30 * cfg->ui_scale}, "Import orbital data"))
                {
                    if (!show_tle_mgr_dialog)
                    {
                        FindSmartWindowPosition(400 * cfg->ui_scale, 500 * cfg->ui_scale, cfg, &tm_x, &tm_y);
                        show_tle_mgr_dialog = true;
                        BringToFront(WND_TLE_MGR);
                    }
                }
            }
            break;
        }

        case WND_HELP:
        {
            if (!show_help)
                break;
            if (drag_help)
            {
                hw_x = GetMousePosition().x - drag_help_off.x;
                hw_y = GetMousePosition().y - drag_help_off.y;
                SnapWindow(&hw_x, &hw_y, helpWindow.width, helpWindow.height, cfg);
            }
            helpWindow.x = hw_x; helpWindow.y = hw_y;
            if (DrawMaterialWindow(helpWindow, "#193# Help & Controls", cfg, customFont, true))
                show_help = false;

            Rectangle contentRec = {0, 0, helpWindow.width - 32 * cfg->ui_scale, 660 * cfg->ui_scale};
            Rectangle viewRec = {0};

            int oldFocusD = GuiGetStyle(DEFAULT, BORDER_COLOR_FOCUSED);
            int oldPressD = GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED);
            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));

            GuiScrollPanel((Rectangle){hw_x + 8 * cfg->ui_scale, hw_y + 35 * cfg->ui_scale, helpWindow.width - 16 * cfg->ui_scale, helpWindow.height - 35 * cfg->ui_scale - 8 * cfg->ui_scale}, NULL, contentRec, &help_scroll, &viewRec);

            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, oldFocusD);
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, oldPressD);

            BeginScissorMode(viewRec.x, viewRec.y, viewRec.width, viewRec.height);
            float cur_x = viewRec.x + 4 * cfg->ui_scale + help_scroll.x;
            float cur_y = viewRec.y + 4 * cfg->ui_scale + help_scroll.y;

            #define DRAW_HEADER(title) \
                DrawUIText(customFont, title, cur_x, cur_y, 18 * cfg->ui_scale, cfg->ui_accent); \
                cur_y += 26 * cfg->ui_scale

            #define DRAW_ROW(key, desc) \
                DrawUIText(customFont, key, cur_x + 5 * cfg->ui_scale, cur_y, 16 * cfg->ui_scale, cfg->text_main); \
                DrawUIText(customFont, desc, cur_x + 120 * cfg->ui_scale, cur_y, 16 * cfg->ui_scale, cfg->text_secondary); \
                cur_y += 22 * cfg->ui_scale

            DRAW_HEADER("Windows & Menus");
            DRAW_ROW("1 - 0", "Top menu tools");
            DRAW_ROW("` (Tilde)", "Date & Time clock");
            DRAW_ROW("Ctrl + F", "Quick search satellites");
            DRAW_ROW("Esc", "Cancel typing or exit");
            DRAW_ROW("Tab", "Cycle text inputs");

            cur_y += 10 * cfg->ui_scale;
            DRAW_HEADER("Time Controls");
            DRAW_ROW("Space", "Play / Pause");
            DRAW_ROW(". / ,", "Faster / Slower (holdable)");
            DRAW_ROW("/", "Real-time speed (1x)");
            DRAW_ROW("Shift + /", "Reset time multiplier");

            cur_y += 10 * cfg->ui_scale;
            DRAW_HEADER("Camera & Visuals");
            DRAW_ROW("F11", "Toggle Fullscreen");
            DRAW_ROW("RMB / Drag", "Orbit camera / Pan 2D map");
            DRAW_ROW("Shift + RMB", "Pan camera in 3D");
            DRAW_ROW("Home", "Reset camera position");
            DRAW_ROW("M", "Toggle 2D/3D map");
            DRAW_ROW("C", "Toggle Clouds");
            DRAW_ROW("N", "Toggle Night Lights");
            DRAW_ROW("L", "Toggle Labels/Markers");
            DRAW_ROW("- / +", "Adjust UI scale");

            cur_y += 15 * cfg->ui_scale;
            DRAW_HEADER("About & Open Source");
            DrawUIText(customFont, "TLEscope is open-source software.", cur_x, cur_y, 14 * cfg->ui_scale, cfg->text_secondary);
            cur_y += 18 * cfg->ui_scale;
            DrawUIText(customFont, "Licensed under the AGPL-3.0 license.", cur_x, cur_y, 14 * cfg->ui_scale, cfg->text_secondary);
            cur_y += 24 * cfg->ui_scale;

            Rectangle repoBtn = {cur_x, cur_y, 200 * cfg->ui_scale, 28 * cfg->ui_scale};
            if (is_topmost && CheckCollisionPointRec(GetMousePosition(), viewRec)) {
                if (GuiButton(repoBtn, "#198# GitHub Repository")) {
                    OpenURL("https://github.com/aweeri/TLEscope");
                }
            } else {
                GuiDisable();
                GuiButton(repoBtn, "#198# GitHub Repository");
                GuiEnable();
            }

            #undef DRAW_HEADER
            #undef DRAW_ROW

            EndScissorMode();
            break;
        }

        case WND_SETTINGS:
        {
            if (!show_settings)
                break;
            if (IsKeyPressed(KEY_TAB))
            {
                if (edit_hl_name)
                {
                    edit_hl_name = false;
                    edit_hl_lat = true;
                }
                else if (edit_hl_lat)
                {
                    edit_hl_lat = false;
                    edit_hl_lon = true;
                }
                else if (edit_hl_lon)
                {
                    edit_hl_lon = false;
                    edit_hl_alt = true;
                }
                else if (edit_hl_alt)
                {
                    edit_hl_alt = false;
                    edit_fps = true;
                }
                else if (edit_fps)
                {
                    edit_fps = false;
                    edit_hl_name = true;
                }
            }
            if (drag_settings)
            {
                sw_x = GetMousePosition().x - drag_settings_off.x;
                sw_y = GetMousePosition().y - drag_settings_off.y;
                SnapWindow(&sw_x, &sw_y, settingsWindow.width, settingsWindow.height, cfg);
            }
            settingsWindow.x = sw_x; settingsWindow.y = sw_y;
            if (DrawMaterialWindow(settingsWindow, "#142# Settings", cfg, customFont, true))
                show_settings = false;

            float sy = sw_y + 40 * cfg->ui_scale;
            GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 80 * cfg->ui_scale, 24 * cfg->ui_scale}, "Theme:");
            Rectangle themeDropBounds = {sw_x + 90 * cfg->ui_scale, sy, 140 * cfg->ui_scale, 24 * cfg->ui_scale};
            sy += 35 * cfg->ui_scale;

            if (theme_dropdown_edit) GuiLock();

            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Show Statistics", &cfg->show_statistics);
            sy += 25 * cfg->ui_scale;
            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Show Clouds", &cfg->show_clouds);
            sy += 25 * cfg->ui_scale;
            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Night Lights", &cfg->show_night_lights);
            sy += 25 * cfg->ui_scale;
            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Show Markers", &cfg->show_markers);
            sy += 25 * cfg->ui_scale;
            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Scattering", &cfg->show_scattering);
            sy += 25 * cfg->ui_scale;
            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Skybox", &cfg->show_skybox);
            sy += 25 * cfg->ui_scale;
            GuiCheckBox((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "VSync", &cfg->hint_vsync);            
            sy += 30 * cfg->ui_scale;

            DrawLine(sw_x + 10 * cfg->ui_scale, sy, sw_x + settingsWindow.width - 10 * cfg->ui_scale, sy, cfg->ui_secondary);
            sy += 15 * cfg->ui_scale;
            GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 200 * cfg->ui_scale, 24 * cfg->ui_scale}, "Home Location:");
            sy += 25 * cfg->ui_scale;

            GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "Name:");
            AdvancedTextBox((Rectangle){sw_x + 60 * cfg->ui_scale, sy, 170 * cfg->ui_scale, 24 * cfg->ui_scale}, text_hl_name, 64, &edit_hl_name, false);
            sy += 30 * cfg->ui_scale;

            GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "Lat:");
            AdvancedTextBox((Rectangle){sw_x + 60 * cfg->ui_scale, sy, 170 * cfg->ui_scale, 24 * cfg->ui_scale}, text_hl_lat, 32, &edit_hl_lat, true);
            sy += 30 * cfg->ui_scale;

            GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "Lon:");
            AdvancedTextBox((Rectangle){sw_x + 60 * cfg->ui_scale, sy, 170 * cfg->ui_scale, 24 * cfg->ui_scale}, text_hl_lon, 32, &edit_hl_lon, true);
            sy += 30 * cfg->ui_scale;

            GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "Alt:");
            AdvancedTextBox((Rectangle){sw_x + 60 * cfg->ui_scale, sy, 170 * cfg->ui_scale, 24 * cfg->ui_scale}, text_hl_alt, 32, &edit_hl_alt, true);
            
            if(!cfg->hint_vsync)
            {
                sy += 30 * cfg->ui_scale;
                GuiLabel((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 80 * cfg->ui_scale, 24 * cfg->ui_scale}, "Max FPS:");
                AdvancedTextBox((Rectangle){sw_x + 90 * cfg->ui_scale, sy, 140 * cfg->ui_scale, 24 * cfg->ui_scale}, text_fps, 8, &edit_fps, true);
            }

            sy += 35 * cfg->ui_scale;

            if (GuiButton((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 220 * cfg->ui_scale, 28 * cfg->ui_scale}, *ctx->picking_home ? "Cancel Picking" : "Pick on Map"))
                *ctx->picking_home = !*ctx->picking_home;
            sy += 35 * cfg->ui_scale;
            if (GuiButton((Rectangle){sw_x + 10 * cfg->ui_scale, sy, 220 * cfg->ui_scale, 28 * cfg->ui_scale}, "Save Settings"))
            {
                strncpy(home_location.name, text_hl_name, 63);
                home_location.lat = atof(text_hl_lat);
                home_location.lon = atof(text_hl_lon);
                home_location.alt = atof(text_hl_alt);
                cfg->target_fps = atoi(text_fps);
                if (cfg->target_fps < 1) cfg->target_fps = 60;
                if (!cfg->hint_vsync) SetTargetFPS(cfg->target_fps);
                else SetTargetFPS(0);
                SaveAppConfig("settings.json", cfg);
                if (show_passes_dialog)
                {
                    if (multi_pass_mode)
                        CalculatePasses(NULL, *ctx->current_epoch);
                    else if (*ctx->selected_sat)
                        CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
                }
            }

            if (theme_dropdown_edit) GuiUnlock();

            int old_drop_base_p = GuiGetStyle(DROPDOWNBOX, BASE_COLOR_PRESSED);
            int old_drop_base_f = GuiGetStyle(DROPDOWNBOX, BASE_COLOR_FOCUSED);
            GuiSetStyle(DROPDOWNBOX, BASE_COLOR_PRESSED, GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL));
            GuiSetStyle(DROPDOWNBOX, BASE_COLOR_FOCUSED, GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL));

            if (GuiDropdownBox(themeDropBounds, theme_names, &active_theme_idx, theme_dropdown_edit)) {
                theme_dropdown_edit = !theme_dropdown_edit;
                if (!theme_dropdown_edit) {
                    char *start = theme_names;
                    for(int i = 0; i < active_theme_idx; i++) {
                        char *next = strchr(start, ';');
                        if(next) start = next + 1;
                    }
                    char *end = strchr(start, ';');
                    int len = end ? (int)(end - start) : strlen(start);
                    if (len > 0 && len < 64) {
                        strncpy(cfg->theme, start, len);
                        cfg->theme[len] = '\0';
                        cfg->reload_theme = true;
                        SaveAppConfig("settings.json", cfg);
                    }
                }
            }

            GuiSetStyle(DROPDOWNBOX, BASE_COLOR_PRESSED, old_drop_base_p);
            GuiSetStyle(DROPDOWNBOX, BASE_COLOR_FOCUSED, old_drop_base_f);
            break;
        }

        case WND_TIME:
        {
            if (!show_time_dialog)
                break;
            if (IsKeyPressed(KEY_TAB))
            {
                if (edit_year)
                {
                    edit_year = false;
                    edit_month = true;
                }
                else if (edit_month)
                {
                    edit_month = false;
                    edit_day = true;
                }
                else if (edit_day)
                {
                    edit_day = false;
                    edit_hour = true;
                }
                else if (edit_hour)
                {
                    edit_hour = false;
                    edit_min = true;
                }
                else if (edit_min)
                {
                    edit_min = false;
                    edit_sec = true;
                }
                else if (edit_sec)
                {
                    edit_sec = false;
                    edit_unix = true;
                }
                else if (edit_unix)
                {
                    edit_unix = false;
                    edit_year = true;
                }
                else
                {
                    edit_year = true;
                }
            }
            if (drag_time_dialog)
            {
                td_x = GetMousePosition().x - drag_time_off.x;
                td_y = GetMousePosition().y - drag_time_off.y;
                SnapWindow(&td_x, &td_y, timeWindow.width, timeWindow.height, cfg);
            }
            timeWindow.x = td_x; timeWindow.y = td_y;
            if (DrawMaterialWindow(timeWindow, "#139# Set Date & Time (UTC)", cfg, customFont, true))
                show_time_dialog = false;

            float cur_y = td_y + 35 * cfg->ui_scale;
            GuiLabel((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 100 * cfg->ui_scale, 24 * cfg->ui_scale}, "Date (Y-M-D):");
            cur_y += 25 * cfg->ui_scale;
            AdvancedTextBox((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 60 * cfg->ui_scale, 28 * cfg->ui_scale}, text_year, 8, &edit_year, true);
            AdvancedTextBox((Rectangle){td_x + 80 * cfg->ui_scale, cur_y, 40 * cfg->ui_scale, 28 * cfg->ui_scale}, text_month, 4, &edit_month, true);
            AdvancedTextBox((Rectangle){td_x + 125 * cfg->ui_scale, cur_y, 40 * cfg->ui_scale, 28 * cfg->ui_scale}, text_day, 4, &edit_day, true);

            cur_y += 35 * cfg->ui_scale;
            GuiLabel((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 100 * cfg->ui_scale, 24 * cfg->ui_scale}, "Time (H:M:S):");
            cur_y += 25 * cfg->ui_scale;
            AdvancedTextBox((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 40 * cfg->ui_scale, 28 * cfg->ui_scale}, text_hour, 4, &edit_hour, true);
            AdvancedTextBox((Rectangle){td_x + 60 * cfg->ui_scale, cur_y, 40 * cfg->ui_scale, 28 * cfg->ui_scale}, text_min, 4, &edit_min, true);
            AdvancedTextBox((Rectangle){td_x + 105 * cfg->ui_scale, cur_y, 40 * cfg->ui_scale, 28 * cfg->ui_scale}, text_sec, 4, &edit_sec, true);

            cur_y += 35 * cfg->ui_scale;
            if (GuiButton((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 222 * cfg->ui_scale, 30 * cfg->ui_scale}, "Apply Date/Time"))
            {
                *ctx->is_auto_warping = false;
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
                if (unix_time != -1)
                    *ctx->current_epoch = unix_to_epoch((double)unix_time);
                show_time_dialog = false;
            }

            cur_y += 40 * cfg->ui_scale;
            DrawLine(td_x + 15 * cfg->ui_scale, cur_y, td_x + timeWindow.width - 15 * cfg->ui_scale, cur_y, cfg->ui_secondary);
            cur_y += 10 * cfg->ui_scale;
            GuiLabel((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 150 * cfg->ui_scale, 24 * cfg->ui_scale}, "Unix Epoch:");
            cur_y += 25 * cfg->ui_scale;
            AdvancedTextBox((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 222 * cfg->ui_scale, 28 * cfg->ui_scale}, text_unix, 64, &edit_unix, true);
            cur_y += 35 * cfg->ui_scale;
            if (GuiButton((Rectangle){td_x + 15 * cfg->ui_scale, cur_y, 222 * cfg->ui_scale, 30 * cfg->ui_scale}, "Apply Epoch"))
            {
                *ctx->is_auto_warping = false;
                double ep;
                if (sscanf(text_unix, "%lf", &ep) == 1)
                    *ctx->current_epoch = unix_to_epoch(ep);
                show_time_dialog = false;
            }
            break;
        }

        case WND_PASSES:
        {
            if (!show_passes_dialog)
                break;
            if (drag_passes)
            {
                pd_x = GetMousePosition().x - drag_passes_off.x;
                pd_y = GetMousePosition().y - drag_passes_off.y;
                SnapWindow(&pd_x, &pd_y, passesWindow.width, passesWindow.height, cfg);
            }
            passesWindow.x = pd_x; passesWindow.y = pd_y;
            if (DrawMaterialWindow(passesWindow, "#208# Upcoming Passes", cfg, customFont, true))
                show_passes_dialog = false;

            if (GuiButton(
                    (Rectangle){passesWindow.x + 20 * cfg->ui_scale, passesWindow.y + 30 * cfg->ui_scale, passesWindow.width - 160 * cfg->ui_scale, 24 * cfg->ui_scale},
                    multi_pass_mode ? "Mode: All Passes" : "Mode: Targeted only"
                ))
            {
                multi_pass_mode = !multi_pass_mode;
                if (multi_pass_mode)
                    CalculatePasses(NULL, *ctx->current_epoch);
                else if (*ctx->selected_sat)
                    CalculatePasses(*ctx->selected_sat, *ctx->current_epoch);
                else
                {
                    num_passes = 0;
                    last_pass_calc_sat = NULL;
                }
            }

            GuiLabel((Rectangle){passesWindow.x + passesWindow.width - 132 * cfg->ui_scale, passesWindow.y + 30 * cfg->ui_scale, 60 * cfg->ui_scale, 24 * cfg->ui_scale}, "Min Elv:");
            AdvancedTextBox(
                (Rectangle){passesWindow.x + passesWindow.width - 55 * cfg->ui_scale, passesWindow.y + 30 * cfg->ui_scale, 45 * cfg->ui_scale, 24 * cfg->ui_scale}, text_min_el, 8, &edit_min_el, true
            );

            float min_el_threshold = atof(text_min_el);
            int valid_passes[MAX_PASSES], valid_count = 0;
            for (int i = 0; i < num_passes; i++)
                if (passes[i].max_el >= min_el_threshold)
                    valid_passes[valid_count++] = i;

            Rectangle contentRec = {0, 0, passesWindow.width - 32 * cfg->ui_scale, (valid_count == 0 ? 1 : valid_count) * 55 * cfg->ui_scale};
            Rectangle viewRec = {0};

            int oldFocusD = GuiGetStyle(DEFAULT, BORDER_COLOR_FOCUSED);
            int oldPressD = GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED);
            int oldFocusL = GuiGetStyle(LISTVIEW, BORDER_COLOR_FOCUSED);
            int oldPressL = GuiGetStyle(LISTVIEW, BORDER_COLOR_PRESSED);
            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
            GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));

            GuiScrollPanel((Rectangle){passesWindow.x + 8 * cfg->ui_scale, passesWindow.y + 60 * cfg->ui_scale, passesWindow.width - 16 * cfg->ui_scale, passesWindow.height - 60 * cfg->ui_scale - 8 * cfg->ui_scale}, NULL, contentRec, &passes_scroll, &viewRec);

            GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, oldFocusD);
            GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, oldPressD);
            GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, oldFocusL);
            GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, oldPressL);

            BeginScissorMode(viewRec.x, viewRec.y, viewRec.width, viewRec.height);
            if (!multi_pass_mode && !*ctx->selected_sat)
            {
                DrawUIText(
                    customFont, "No satellite targeted.", viewRec.x + 10 * cfg->ui_scale + passes_scroll.x, viewRec.y + 10 * cfg->ui_scale + passes_scroll.y, 16 * cfg->ui_scale,
                    cfg->text_main
                );
            }
            else if (valid_count == 0)
            {
                DrawUIText(
                    customFont, "No passes meet your criteria.", viewRec.x + 10 * cfg->ui_scale + passes_scroll.x, viewRec.y + 10 * cfg->ui_scale + passes_scroll.y, 16 * cfg->ui_scale,
                    cfg->text_main
                );
            }
            else
            {
                for (int k = 0; k < valid_count; k++)
                {
                    int i = valid_passes[k];
                    float item_y = viewRec.y + 4 * cfg->ui_scale + passes_scroll.y + k * 55 * cfg->ui_scale;
                    if (item_y + 55 * cfg->ui_scale < viewRec.y || item_y > viewRec.y + viewRec.height)
                        continue;

                    Rectangle rowBtn = {viewRec.x + 4 * cfg->ui_scale + passes_scroll.x, item_y, viewRec.width - 8 * cfg->ui_scale, 50 * cfg->ui_scale};
                    bool isHovered = is_topmost && CheckCollisionPointRec(GetMousePosition(), rowBtn) && CheckCollisionPointRec(GetMousePosition(), viewRec);
                    bool isSelected = (show_polar_dialog && passes[i].sat == locked_pass_sat && fabs(passes[i].aos_epoch - locked_pass_aos) < (1.0 / 86400.0));

                    if (isHovered || isSelected)
                    {
                        DrawRectangleLinesEx(rowBtn, 1.0f, cfg->ui_accent);
                        if (isHovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                        {
                            if (!show_polar_dialog)
                            {
                                FindSmartWindowPosition(300 * cfg->ui_scale, 430 * cfg->ui_scale, cfg, &pl_x, &pl_y);
                                show_polar_dialog = true;
                                BringToFront(WND_POLAR);
                            }
                            polar_lunar_mode = false;
                            locked_pass_sat = passes[i].sat;
                            locked_pass_aos = passes[i].aos_epoch;
                            locked_pass_los = passes[i].los_epoch;
                            *ctx->selected_sat = passes[i].sat;
                        }
                    }

                    char aos_str[16], los_str[16];
                    epoch_to_time_str(passes[i].aos_epoch, aos_str);
                    epoch_to_time_str(passes[i].los_epoch, los_str);

                    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(cfg->ui_accent));
                    GuiLabel((Rectangle){rowBtn.x + 10 * cfg->ui_scale, rowBtn.y + 2 * cfg->ui_scale, rowBtn.width - 20 * cfg->ui_scale, 20 * cfg->ui_scale}, passes[i].sat->name);

                    char info_str[128];
                    sprintf(info_str, "%s -> %s   Max: %.1fdeg", aos_str, los_str, passes[i].max_el);
                    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt(cfg->text_main));
                    GuiLabel((Rectangle){rowBtn.x + 10 * cfg->ui_scale, rowBtn.y + 20 * cfg->ui_scale, rowBtn.width - 20 * cfg->ui_scale, 20 * cfg->ui_scale}, info_str);

                    if (*ctx->current_epoch >= passes[i].aos_epoch && *ctx->current_epoch <= passes[i].los_epoch)
                    {
                        float prog = (*ctx->current_epoch - passes[i].aos_epoch) / (passes[i].los_epoch - passes[i].aos_epoch);
                        Rectangle pb_bg = {rowBtn.x + 5 * cfg->ui_scale, rowBtn.y + 42 * cfg->ui_scale, rowBtn.width - 10 * cfg->ui_scale, 4 * cfg->ui_scale};
                        DrawRectangleRec(pb_bg, cfg->ui_secondary);
                        DrawRectangleRec((Rectangle){pb_bg.x, pb_bg.y, pb_bg.width * prog, pb_bg.height}, cfg->ui_accent);
                    }
                }
            }
            EndScissorMode();
            break;
        }

        case WND_POLAR:
        {
            if (!show_polar_dialog)
                break;
            if (drag_polar)
            {
                pl_x = GetMousePosition().x - drag_polar_off.x;
                pl_y = GetMousePosition().y - drag_polar_off.y;
                SnapWindow(&pl_x, &pl_y, polarWindow.width, polarWindow.height, cfg);
            }
            polarWindow.x = pl_x; polarWindow.y = pl_y;
            if (DrawMaterialWindow(polarWindow, "#64# Polar Tracking Plot", cfg, customFont, true))
                show_polar_dialog = false;

            if (GuiButton((Rectangle){pl_x + 20 * cfg->ui_scale, pl_y + 30 * cfg->ui_scale, polarWindow.width - 40 * cfg->ui_scale, 24 * cfg->ui_scale}, polar_lunar_mode ? "Mode: Lunar Tracking" : "Mode: Satellite Pass")) {
                polar_lunar_mode = !polar_lunar_mode;
            }

            float cx = pl_x + polarWindow.width / 2;
            float cy = pl_y + 175 * cfg->ui_scale;
            float r_max = 100 * cfg->ui_scale;
            
            bool has_data = false;
            if (polar_lunar_mode) {
                has_data = true;
                if (fabs(*ctx->current_epoch - last_lunar_calc_time) > 0.5 || lunar_num_pts == 0) {
                    CalculateLunarPass(*ctx->current_epoch, &lunar_aos, &lunar_los, lunar_path_pts, &lunar_num_pts);
                    last_lunar_calc_time = *ctx->current_epoch;
                }
            } else if (selected_pass_idx >= 0 && selected_pass_idx < num_passes) {
                has_data = true;
            }

            if (has_data)
            {
                DrawCircleLines(cx, cy, r_max, cfg->ui_secondary);
                DrawCircleLines(cx, cy, r_max * 0.666f, cfg->ui_secondary);
                DrawCircleLines(cx, cy, r_max * 0.333f, cfg->ui_secondary);
                DrawLine(cx - r_max, cy, cx + r_max, cy, cfg->ui_secondary);
                DrawLine(cx, cy - r_max, cx, cy + r_max, cfg->ui_secondary);

                DrawUIText(customFont, "N", cx - 5 * cfg->ui_scale, cy - r_max - 20 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
                DrawUIText(customFont, "E", cx + r_max + 5 * cfg->ui_scale, cy - 8 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
                DrawUIText(customFont, "S", cx - 5 * cfg->ui_scale, cy + r_max + 5 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
                DrawUIText(customFont, "W", cx - r_max - 20 * cfg->ui_scale, cy - 8 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);

                int num_pts = polar_lunar_mode ? lunar_num_pts : passes[selected_pass_idx].num_pts;
                Vector2 *path_pts = polar_lunar_mode ? lunar_path_pts : passes[selected_pass_idx].path_pts;
                double p_aos = polar_lunar_mode ? lunar_aos : passes[selected_pass_idx].aos_epoch;
                double p_los = polar_lunar_mode ? lunar_los : passes[selected_pass_idx].los_epoch;
                Satellite *p_sat = polar_lunar_mode ? NULL : passes[selected_pass_idx].sat;

                for (int k = 0; k < num_pts - 1; k++)
                {
                    float r1 = r_max * (90 - path_pts[k].y) / 90.0f;
                    float r2 = r_max * (90 - path_pts[k + 1].y) / 90.0f;
                    if (r1 > r_max) r1 = r_max;
                    if (r2 > r_max) r2 = r_max;
                    
                    Vector2 pt1 = {cx + r1 * sin(path_pts[k].x * DEG2RAD), cy - r1 * cos(path_pts[k].x * DEG2RAD)};
                    Vector2 pt2 = {cx + r2 * sin(path_pts[k + 1].x * DEG2RAD), cy - r2 * cos(path_pts[k + 1].x * DEG2RAD)};

                    Color lineCol = cfg->ui_accent;
                    if (!polar_lunar_mode && cfg->highlight_sunlit)
                    {
                        double pt_epoch = p_aos + k * ((p_los - p_aos) / (double)(num_pts - 1));
                        if (!is_sat_eclipsed(calculate_position(p_sat, get_unix_from_epoch(pt_epoch)), Vector3Normalize(calculate_sun_position(pt_epoch))))
                            lineCol = cfg->sat_highlighted;
                        else
                            lineCol = cfg->orbit_normal;
                    }
                    DrawLineEx(pt1, pt2, 2.0f, lineCol);
                }

                Rectangle polar_area = {cx - r_max, cy - r_max, r_max * 2, r_max * 2};
                if (is_topmost && CheckCollisionPointRec(GetMousePosition(), polar_area) && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
                {
                    float min_d = 999999;
                    int best_k = 0;
                    for (int k = 0; k < num_pts; k++)
                    {
                        float r = r_max * (90 - path_pts[k].y) / 90.0f;
                        if (r > r_max) r = r_max;
                        Vector2 pt = {cx + r * sin(path_pts[k].x * DEG2RAD), cy - r * cos(path_pts[k].x * DEG2RAD)};
                        float d = Vector2Distance(GetMousePosition(), pt);
                        if (d < min_d)
                        {
                            min_d = d;
                            best_k = k;
                        }
                    }
                    double progress = (num_pts > 1) ? (best_k / (double)(num_pts - 1)) : 0.0;
                    *ctx->current_epoch = p_aos + progress * (p_los - p_aos);
                    *ctx->is_auto_warping = false;
                }

                if (*ctx->current_epoch >= p_aos && *ctx->current_epoch <= p_los)
                {
                    double c_az, c_el;
                    if (polar_lunar_mode) {
                        get_az_el(calculate_moon_position(*ctx->current_epoch), epoch_to_gmst(*ctx->current_epoch), home_location.lat, home_location.lon, home_location.alt, &c_az, &c_el);
                    } else {
                        get_az_el(calculate_position(p_sat, get_unix_from_epoch(*ctx->current_epoch)), epoch_to_gmst(*ctx->current_epoch), home_location.lat, home_location.lon, home_location.alt, &c_az, &c_el);
                    }

                    float r_c = r_max * (90 - c_el) / 90.0f;
                    if (r_c > r_max) r_c = r_max;
                    Vector2 pt_c = {cx + r_c * sin(c_az * DEG2RAD), cy - r_c * cos(c_az * DEG2RAD)};
                    DrawCircleV(pt_c, 5.0f * cfg->ui_scale, RED);
                    DrawCircleLines(pt_c.x, pt_c.y, 7.0f * cfg->ui_scale, WHITE);

                    char c_info[128];
                    sprintf(c_info, "Az: %05.1f  El: %04.1f", c_az, c_el);
                    DrawUIText(customFont, c_info, pl_x + 20 * cfg->ui_scale, pl_y + 295 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);

                    if (!polar_lunar_mode) {
                        double s_range = get_sat_range(p_sat, *ctx->current_epoch, home_location);
                        char rng_info[64];
                        sprintf(rng_info, "Range: %.0f km", s_range);
                        DrawUIText(customFont, rng_info, pl_x + 20 * cfg->ui_scale, pl_y + 315 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
                    } else {
                        double m_range = Vector3Length(calculate_moon_position(*ctx->current_epoch)) - EARTH_RADIUS_KM;
                        char rng_info[64];
                        sprintf(rng_info, "Range: %.0f km", m_range);
                        DrawUIText(customFont, rng_info, pl_x + 20 * cfg->ui_scale, pl_y + 315 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
                    }

                    int sec_till_los = (int)((p_los - *ctx->current_epoch) * 86400.0);
                    DrawUIText(
                        customFont, TextFormat("%s in: %02d:%02d", polar_lunar_mode ? "Set" : "LOS", sec_till_los / 60, sec_till_los % 60), pl_x + 20 * cfg->ui_scale, pl_y + 335 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->ui_accent
                    );
                }
                else if (*ctx->current_epoch < p_aos)
                {
                    int sec_till_aos = (int)((p_aos - *ctx->current_epoch) * 86400.0);
                    DrawUIText(
                        customFont, TextFormat("%s in: %02d:%02d:%02d", polar_lunar_mode ? "Rise" : "AOS", sec_till_aos / 3600, (sec_till_aos % 3600) / 60, sec_till_aos % 60), pl_x + 20 * cfg->ui_scale, pl_y + 310 * cfg->ui_scale,
                        16 * cfg->ui_scale, cfg->text_secondary
                    );
                }
                else
                    DrawUIText(customFont, polar_lunar_mode ? "Set Complete" : "Pass Complete", pl_x + 20 * cfg->ui_scale, pl_y + 310 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);

                RotatorDrawPolarOverlay(cfg, customFont, cx, cy, r_max, pl_x, pl_y);

                if (GuiButton((Rectangle){pl_x + 20 * cfg->ui_scale, pl_y + 360 * cfg->ui_scale, 260 * cfg->ui_scale, 30 * cfg->ui_scale}, TextFormat("#134# Jump to %s", polar_lunar_mode ? "Rise" : "AOS")))
                {
                    *ctx->auto_warp_target = p_aos;
                    *ctx->auto_warp_initial_diff = (*ctx->auto_warp_target - *ctx->current_epoch) * 86400.0;
                    if (fabs(*ctx->auto_warp_initial_diff) > 0.0)
                        *ctx->is_auto_warping = true;
                }

                if (!polar_lunar_mode && GuiButton((Rectangle){pl_x + 20 * cfg->ui_scale, pl_y + 395 * cfg->ui_scale, 260 * cfg->ui_scale, 30 * cfg->ui_scale}, "#125# Doppler Shift Analysis"))
                {
                    if (!show_doppler_dialog)
                    {
                        FindSmartWindowPosition(320 * cfg->ui_scale, 480 * cfg->ui_scale, cfg, &dop_x, &dop_y);
                        show_doppler_dialog = true;
                        BringToFront(WND_DOPPLER);
                    }
                }
            }
            else
            {
                DrawUIText(customFont, "Select a pass first or toggle", pl_x + 20 * cfg->ui_scale, pl_y + 100 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
                DrawUIText(customFont, "Lunar Mode to track the Moon.", pl_x + 20 * cfg->ui_scale, pl_y + 120 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
            }
            break;
        }

        case WND_DOPPLER:
        {
            if (!show_doppler_dialog)
                break;
            if (IsKeyPressed(KEY_TAB))
            {
                if (edit_doppler_freq)
                {
                    edit_doppler_freq = false;
                    edit_doppler_res = true;
                }
                else if (edit_doppler_res)
                {
                    edit_doppler_res = false;
                    edit_doppler_file = true;
                }
                else if (edit_doppler_file)
                {
                    edit_doppler_file = false;
                    edit_doppler_freq = true;
                }
                else
                {
                    edit_doppler_freq = true;
                }
            }
            if (drag_doppler)
            {
                dop_x = GetMousePosition().x - drag_doppler_off.x;
                dop_y = GetMousePosition().y - drag_doppler_off.y;
                SnapWindow(&dop_x, &dop_y, dopplerWindow.width, dopplerWindow.height, cfg);
            }
            dopplerWindow.x = dop_x; dopplerWindow.y = dop_y;
            if (DrawMaterialWindow(dopplerWindow, "#125# Doppler Shift Analysis", cfg, customFont, true))
                show_doppler_dialog = false;

            if (selected_pass_idx >= 0 && selected_pass_idx < num_passes)
            {
                SatPass *p = &passes[selected_pass_idx];
                Satellite *d_sat = p->sat;

                float dy = dop_y + 35 * cfg->ui_scale;
                GuiLabel((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 200 * cfg->ui_scale, 24 * cfg->ui_scale}, "Freq (Hz):");
                dy += 25 * cfg->ui_scale;
                AdvancedTextBox((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 290 * cfg->ui_scale, 28 * cfg->ui_scale}, text_doppler_freq, 32, &edit_doppler_freq, true);

                dy += 35 * cfg->ui_scale;
                GuiLabel((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 200 * cfg->ui_scale, 24 * cfg->ui_scale}, "CSV Res (s):");
                dy += 25 * cfg->ui_scale;
                AdvancedTextBox((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 290 * cfg->ui_scale, 28 * cfg->ui_scale}, text_doppler_res, 32, &edit_doppler_res, true);

                dy += 35 * cfg->ui_scale;
                GuiLabel((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 200 * cfg->ui_scale, 24 * cfg->ui_scale}, "Export:");
                dy += 25 * cfg->ui_scale;
                AdvancedTextBox((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 290 * cfg->ui_scale, 28 * cfg->ui_scale}, text_doppler_file, 128, &edit_doppler_file, false);

                dy += 35 * cfg->ui_scale;
                if (GuiButton((Rectangle){dop_x + 15 * cfg->ui_scale, dy, 290 * cfg->ui_scale, 30 * cfg->ui_scale}, "Export CSV"))
                {
                    double base_freq = atof(text_doppler_freq), res = fmax(atof(text_doppler_res), 0.1);
                    double pass_dur = (p->los_epoch - p->aos_epoch) * 86400.0;
                    FILE *fp = fopen(text_doppler_file, "w");
                    if (fp)
                    {
                        fprintf(fp, "Time(s),Frequency(Hz)\n");
                        for (int k = 0; k <= (int)(pass_dur * res); k++)
                        {
                            double t_sec = k / res;
                            fprintf(fp, "%.3f,%.3f\n", t_sec, calculate_doppler_freq(d_sat, p->aos_epoch + t_sec / 86400.0, home_location, base_freq));
                        }
                        fclose(fp);
                    }
                }

                dy += 45 * cfg->ui_scale;
                double base_freq = atof(text_doppler_freq), pass_dur = (p->los_epoch - p->aos_epoch) * 86400.0;

                if (pass_dur > 0 && base_freq > 0)
                {
                    float graph_x = dop_x + 75 * cfg->ui_scale, graph_y = dy, graph_w = dopplerWindow.width - 90 * cfg->ui_scale, graph_h = dopplerWindow.height - (dy - dop_y) - 20 * cfg->ui_scale;
                    DrawRectangleLines(graph_x, graph_y, graph_w, graph_h, cfg->ui_secondary);

                    double min_f = base_freq * 2.0, max_f = 0.0;
                    int plot_pts = (int)graph_w;
                    for (int k = 0; k <= plot_pts; k++)
                    {
                        double f = calculate_doppler_freq(d_sat, p->aos_epoch + (k / (double)plot_pts) * (pass_dur / 86400.0), home_location, base_freq);
                        if (f < min_f)
                            min_f = f;
                        if (f > max_f)
                            max_f = f;
                    }

                    double max_abs_d = fmax(fmax(fabs(max_f - base_freq), fabs(min_f - base_freq)), 1.0);
                    double max_d = max_abs_d * 1.1, min_d = -max_abs_d * 1.1;

                    DrawUIText(customFont, TextFormat("%+.0f Hz", max_d), dop_x + 5 * cfg->ui_scale, graph_y, 14 * cfg->ui_scale, cfg->text_main);
                    DrawUIText(customFont, TextFormat("%+.0f Hz", min_d), dop_x + 5 * cfg->ui_scale, graph_y + graph_h - 14 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_main);
                    DrawUIText(customFont, "0s", graph_x + 10 * cfg->ui_scale, graph_y + graph_h + 5 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_main);
                    DrawUIText(customFont, TextFormat("%.0fs", pass_dur), graph_x + graph_w - 55 * cfg->ui_scale, graph_y + graph_h + 5 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_main);

                    float zero_y = graph_y + graph_h - (float)((0 - min_d) / (max_d - min_d)) * graph_h;
                    DrawLine(graph_x, zero_y, graph_x + graph_w, zero_y, ApplyAlpha(cfg->ui_secondary, 0.8f)); // center line

                    BeginScissorMode((int)graph_x, (int)graph_y, (int)graph_w, (int)graph_h);
                    Vector2 prev_pt = {0};
                    for (int k = 0; k <= plot_pts; k++)
                    {
                        double delta = calculate_doppler_freq(d_sat, p->aos_epoch + (k / (double)plot_pts) * (pass_dur / 86400.0), home_location, base_freq) - base_freq;
                        float px = graph_x + k, py = graph_y + graph_h - (float)((delta - min_d) / (max_d - min_d)) * graph_h;
                        if (k > 0)
                            DrawLineEx(prev_pt, (Vector2){px, py}, 2.0f, cfg->ui_accent);
                        prev_pt = (Vector2){px, py};
                    }

                    if (*ctx->current_epoch >= p->aos_epoch && *ctx->current_epoch <= p->los_epoch)
                    {
                        float cx = graph_x + (((*ctx->current_epoch - p->aos_epoch) * 86400.0) / pass_dur) * graph_w;
                        float cy = graph_y + graph_h - (float)((calculate_doppler_freq(d_sat, *ctx->current_epoch, home_location, base_freq) - base_freq - min_d) / (max_d - min_d)) * graph_h;
                        DrawCircleV((Vector2){cx, cy}, 5.0f * cfg->ui_scale, RED);
                        DrawCircleLines(cx, cy, 7.0f * cfg->ui_scale, WHITE);
                    }
                    EndScissorMode();

                    /* Doppler timeline dragging logic */
                    Rectangle chartRec = {graph_x, graph_y, graph_w, graph_h};
                    if (is_topmost && CheckCollisionPointRec(GetMousePosition(), chartRec))
                    {
                        float mouseX = GetMousePosition().x, mouseY = GetMousePosition().y;
                        double t_sec = ((mouseX - graph_x) / graph_w) * pass_dur;
                        double f_hz = calculate_doppler_freq(d_sat, p->aos_epoch + t_sec / 86400.0, home_location, base_freq);

                        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
                        {
                            *ctx->current_epoch = p->aos_epoch + t_sec / 86400.0;
                            *ctx->is_auto_warping = false;
                        }

                        float dot_y = graph_y + graph_h - (float)((f_hz - base_freq - min_d) / (max_d - min_d)) * graph_h;
                        DrawLine(mouseX, graph_y, mouseX, graph_y + graph_h, cfg->ui_accent);
                        DrawCircle(mouseX, dot_y, 4.0f * cfg->ui_scale, cfg->ui_accent);

                        char tooltip[128];
                        sprintf(tooltip, "%.0f Hz\n%.1f s\n%.3f km/s", f_hz, t_sec, 299792.458 * (base_freq / f_hz - 1.0));
                        Vector2 textSize = MeasureTextEx(customFont, tooltip, 14 * cfg->ui_scale, 1.0f);
                        float tt_x = mouseX + 10 * cfg->ui_scale;
                        if (tt_x + textSize.x + 10 * cfg->ui_scale > dop_x + dopplerWindow.width)
                            tt_x = mouseX - textSize.x - 10 * cfg->ui_scale;

                        Rectangle ttRec = {tt_x, mouseY, textSize.x + 10 * cfg->ui_scale, textSize.y + 10 * cfg->ui_scale};
                        DrawRectangleRounded(ttRec, 0.1f, 8, ApplyAlpha(cfg->ui_bg, 0.9f));
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
                        DrawRectangleRoundedLines(ttRec, 0.1f, 8, 1.5f * cfg->ui_scale, cfg->ui_secondary);
#else
                        DrawRectangleRoundedLines(ttRec, 0.1f, 8, cfg->ui_secondary);
#endif
                        DrawUIText(customFont, tooltip, tt_x + 5 * cfg->ui_scale, mouseY + 5 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_main);
                    }
                }
            }
            else
                DrawUIText(customFont, "No valid pass selected.", dop_x + 20 * cfg->ui_scale, dop_y + 60 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
            break;
        }

case WND_SCOPE:
        {
            if (!show_scope_dialog) break;

            /* tab cycling for text inputs */
            if (IsKeyPressed(KEY_TAB)) {
                if (edit_scope_az) { edit_scope_az = false; edit_scope_el = true; }
                else if (edit_scope_el) { edit_scope_el = false; edit_scope_beam = true; }
                else if (edit_scope_beam) { edit_scope_beam = false; edit_scope_az = true; }
                else { edit_scope_az = true; }
            }

            /* handle window dragging and snapping */
            if (drag_scope)
            {
                sc_x = GetMousePosition().x - drag_scope_off.x;
                sc_y = GetMousePosition().y - drag_scope_off.y;
                SnapWindow(&sc_x, &sc_y, scopeWindow.width, scopeWindow.height, cfg);
            }
            scopeWindow.x = sc_x; scopeWindow.y = sc_y;
            if (DrawMaterialWindow(scopeWindow, "#103# Satellite Scope", cfg, customFont, true))
                show_scope_dialog = false;

            /* auto-aim scope if locked to a satellite */
            if (scope_lock && *ctx->selected_sat) {
                double l_az, l_el;
                Vector3 sat_pos = (*ctx->selected_sat)->is_active ? (*ctx->selected_sat)->current_pos : calculate_position(*ctx->selected_sat, get_unix_from_epoch(*ctx->current_epoch));
                get_az_el(sat_pos, ctx->gmst_deg, home_location.lat, home_location.lon, home_location.alt, &l_az, &l_el);
                scope_az = (float)l_az;
                scope_el = (float)l_el;
                snprintf(text_scope_az, sizeof(text_scope_az), "%.1f", scope_az);
                snprintf(text_scope_el, sizeof(text_scope_el), "%.1f", scope_el);
            }

            float scope_radius = 160 * cfg->ui_scale;
            Vector2 center = {sc_x + scopeWindow.width / 2.0f, sc_y + 40 * cfg->ui_scale + scope_radius};

            /* direct mouse control of scope orientation and beam */
            if (is_topmost)
            {
                Vector2 mouse = GetMousePosition();
                float dx = mouse.x - center.x;
                float dy = mouse.y - center.y;
                float dist = sqrtf(dx * dx + dy * dy);

                /* WHEN NOT LOCKED left-drag inside the viewfinder adjusts az/el  */
                if (!scope_lock)
                {
                    if (!scope_drag_active && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && dist <= scope_radius)
                    {
                        scope_drag_active = true;
                        scope_drag_moved = false;
                        scope_drag_last = mouse;
                    }
                    if (scope_drag_active && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
                    {
                        Vector2 cur = mouse;
                        Vector2 delta = Vector2Subtract(cur, scope_drag_last);
                        if (fabsf(delta.x) > 1.0f || fabsf(delta.y) > 1.0f) scope_drag_moved = true;
                        scope_drag_last = cur;

                        /* map pixels to angular offsets based on current beam */
                        float pix_to_deg = (scope_beam / 2.0f) / scope_radius;
                        scope_az += delta.x * pix_to_deg;
                        scope_el -= delta.y * pix_to_deg;

                        /* normalize / clamp */
                        while (scope_az < 0.0f) scope_az += 360.0f;
                        while (scope_az >= 360.0f) scope_az -= 360.0f;
                        if (scope_el > 90.0f) scope_el = 90.0f;
                        if (scope_el < -90.0f) scope_el = -90.0f;

                        if (!edit_scope_az) snprintf(text_scope_az, sizeof(text_scope_az), "%.1f", scope_az);
                        if (!edit_scope_el) snprintf(text_scope_el, sizeof(text_scope_el), "%.1f", scope_el);
                    }
                }

                if (scope_drag_active && IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
                {
                    scope_drag_active = false;
                    scope_drag_moved = false;
                }

                /* scroll wheel over viewfinder changes beam width */
                if (dist <= scope_radius)
                {
                    float wheel = GetMouseWheelMove();
                    if (wheel != 0.0f && !edit_scope_beam)
                    {
                        scope_beam -= wheel * 5.0f;
                        if (scope_beam < 1.0f) scope_beam = 1.0f;
                        if (scope_beam > 120.0f) scope_beam = 120.0f;
                        snprintf(text_scope_beam, sizeof(text_scope_beam), "%.1f", scope_beam);
                    }
                }
            }

            // push state back to context for 3d render
            *ctx->show_scope = true;
            *ctx->scope_az = scope_az;
            *ctx->scope_el = scope_el;
            *ctx->scope_beam = scope_beam;

            /* draw the viewfinder and horizon grid */
            DrawCircleV(center, scope_radius, cfg->scope_bg);

            for (int y = (int)(center.y - scope_radius); y <= (int)(center.y + scope_radius); y++) {
                float dy_screen = y - center.y; 
                float ang_offset = -(dy_screen / scope_radius) * (scope_beam / 2.0f); 
                float approx_el = scope_el + ang_offset;
                
                // shade area below horizon
                if (approx_el < 0) {
                    float half_width = sqrtf(fmaxf(0.0f, scope_radius * scope_radius - dy_screen * dy_screen));
                    DrawLine((int)(center.x - half_width), y, (int)(center.x + half_width), y, cfg->scope_horizon);
                }
            }

            // crosshairs and borders
            DrawLineV((Vector2){center.x, center.y - scope_radius}, (Vector2){center.x, center.y + scope_radius}, ApplyAlpha(cfg->ui_secondary, 0.4f));
            DrawLineV((Vector2){center.x - scope_radius, center.y}, (Vector2){center.x + scope_radius, center.y}, ApplyAlpha(cfg->ui_secondary, 0.4f));
            DrawCircleLines(center.x, center.y, scope_radius * 0.5f, ApplyAlpha(cfg->ui_secondary, 0.2f));
            DrawCircleLines(center.x, center.y, scope_radius, cfg->ui_secondary);

            Satellite* hover_sat_scope = NULL;
            float min_hover_dist = 9999.0f;
            Vector2 hover_pos = {0};

            /* setup eci vectors for SPEEDY culling */
            float rad_beam_half = (scope_beam / 2.0f) * DEG2RAD;
            float cos_beam_half = cosf(rad_beam_half);
            float cos_beam_half_sqr = cos_beam_half * cos_beam_half;
            float c_az_rad = scope_az * DEG2RAD;
            float c_el_rad = scope_el * DEG2RAD;

            float h_lat_rad = home_location.lat * DEG2RAD;
            float h_lon_rad = (home_location.lon + ctx->gmst_deg) * DEG2RAD;
            double ecef_x, ecef_y, ecef_z;
            geodetic_to_ecef(home_location.lat, home_location.lon + ctx->gmst_deg, home_location.alt, &ecef_x, &ecef_y, &ecef_z);
            Vector3 O_eci = { (float)ecef_x, (float)ecef_z, (float)-ecef_y };

            Vector3 up = Vector3Normalize(O_eci);
            Vector3 east = {-sinf(h_lon_rad), 0.0f, -cosf(h_lon_rad)};
            Vector3 north = {-cosf(h_lon_rad) * sinf(h_lat_rad), cosf(h_lat_rad), sinf(h_lon_rad) * sinf(h_lat_rad)};

            Vector3 scope_dir = Vector3Add(
                Vector3Add(Vector3Scale(north, cosf(c_el_rad) * cosf(c_az_rad)), 
                           Vector3Scale(east, cosf(c_el_rad) * sinf(c_az_rad))),
                Vector3Scale(up, sinf(c_el_rad))
            );
            scope_dir = Vector3Normalize(scope_dir);

            double current_unix = get_unix_from_epoch(*ctx->current_epoch);
            Vector3 sun_dir = {0};
            if (cfg->highlight_sunlit) {
                sun_dir = Vector3Normalize(calculate_sun_position(*ctx->current_epoch));
            }
            
            double past_epoch = *ctx->current_epoch - (60.0 / 86400.0);
            double past_unix = get_unix_from_epoch(past_epoch);
            double past_gmst = epoch_to_gmst(past_epoch);

            /* iterate all sats, cull, and project valid ones onto the 2d scope */
            for (int i = 0; i < sat_count; i++) {
                double revs_per_day = (satellites[i].mean_motion * 86400.0) / (2.0 * PI);
                bool is_leo = (revs_per_day > 11.25);
                bool is_geo = (revs_per_day >= 0.99 && revs_per_day <= 1.01);
                bool is_heo = !is_leo && !is_geo;

                if (is_leo && !scope_show_leo) continue;
                if (is_heo && !scope_show_heo) continue;
                if (is_geo && !scope_show_geo) continue;

                Vector3 sat_pos = satellites[i].is_active ? satellites[i].current_pos : calculate_position(&satellites[i], current_unix);

                Vector3 V = Vector3Subtract(sat_pos, O_eci);
                float dist = Vector3Length(V);
                if (dist < 0.001f) continue;
                
                Vector3 V_norm = Vector3Scale(V, 1.0f / dist);
                float cos_theta = Vector3DotProduct(V_norm, scope_dir);

                // check if inside the cone
                if (cos_theta >= cos_beam_half) {
                    double s_az, s_el;
                    get_az_el(sat_pos, ctx->gmst_deg, home_location.lat, home_location.lon, home_location.alt, &s_az, &s_el);

                    float s_az_rad = s_az * DEG2RAD;
                    float s_el_rad = s_el * DEG2RAD;

                    if (cos_theta > 1.0f) cos_theta = 1.0f;
                    float theta = acosf(cos_theta);

                    float dx = cosf(s_el_rad) * sinf(s_az_rad - c_az_rad);
                    float dy = cosf(c_el_rad) * sinf(s_el_rad) - sinf(c_el_rad) * cosf(s_el_rad) * cosf(s_az_rad - c_az_rad);
                    
                    float r_dist = (theta / rad_beam_half) * scope_radius;
                    float angle = atan2f(-dy, dx); 
                    
                    Vector2 dot_pos = { center.x + r_dist * cosf(angle), center.y + r_dist * sinf(angle) };

                    // Figure out dot colors for better contrast in dark scope themes.
                    // Inactive satellites use text_secondary instead of ui_secondary
                    // to keep them visibly muted but not lost in the background.
                    Color dotColor = WHITE;
                    if (cfg->highlight_sunlit) {
                        bool eclipsed = is_sat_eclipsed(sat_pos, sun_dir);
                        dotColor = eclipsed ? GRAY : GOLD;
                        if (!satellites[i].is_active) dotColor = ApplyAlpha(dotColor, 0.65f);
                    } else {
                        dotColor = satellites[i].is_active ? cfg->ui_accent : ApplyAlpha(cfg->text_secondary, 0.9f);
                    }

                    if (*ctx->selected_sat == &satellites[i]) {
                        DrawCircleV(dot_pos, 4.0f * cfg->ui_scale, cfg->sat_selected);
                    } else {
                        DrawCircleV(dot_pos, 2.0f * cfg->ui_scale, dotColor);
                    }

                    // draw movement vectors if requested
                    if (scope_show_trails) {
                        Vector3 past_pos = calculate_position(&satellites[i], past_unix);
                        double p_az, p_el;
                        get_az_el(past_pos, past_gmst, home_location.lat, home_location.lon, home_location.alt, &p_az, &p_el);

                        float p_az_rad = p_az * DEG2RAD;
                        float p_el_rad = p_el * DEG2RAD;

                        float p_cos_theta = sinf(c_el_rad) * sinf(p_el_rad) + cosf(c_el_rad) * cosf(p_el_rad) * cosf(p_az_rad - c_az_rad);
                        if (p_cos_theta < -1.0f) p_cos_theta = -1.0f;
                        if (p_cos_theta > 1.0f) p_cos_theta = 1.0f;
                        float p_theta = acosf(p_cos_theta);

                        float p_dx = cosf(p_el_rad) * sinf(p_az_rad - c_az_rad);
                        float p_dy = cosf(c_el_rad) * sinf(p_el_rad) - sinf(c_el_rad) * cosf(p_el_rad) * cosf(p_az_rad - c_az_rad);
                        
                        float p_r_dist = (p_theta / rad_beam_half) * scope_radius;
                        float p_angle = atan2f(-p_dy, p_dx); 
                        
                        Vector2 past_dot_pos = { center.x + p_r_dist * cosf(p_angle), center.y + p_r_dist * sinf(p_angle) };

                        // clip trail if it's out of the viewfinder circle
                        if (p_r_dist > scope_radius) {
                            float t = (scope_radius - r_dist) / (p_r_dist - r_dist);
                            past_dot_pos.x = dot_pos.x + t * (past_dot_pos.x - dot_pos.x);
                            past_dot_pos.y = dot_pos.y + t * (past_dot_pos.y - dot_pos.y);
                        }
                        
                        DrawLineEx(past_dot_pos, dot_pos, 1.5f * cfg->ui_scale, ApplyAlpha(dotColor, 0.4f));
                    }

                    // pick closest satellite for hover tooltip
                    float hover_dist = Vector2Distance(GetMousePosition(), dot_pos);
                    if (hover_dist < 8.0f * cfg->ui_scale && hover_dist < min_hover_dist) {
                        min_hover_dist = hover_dist;
                        hover_sat_scope = &satellites[i];
                        hover_pos = dot_pos;
                    }
                }
            }

            /* render tooltip for hovered satellite */
            if (hover_sat_scope && is_topmost) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !scope_drag_active && !scope_drag_moved) {
                    *ctx->selected_sat = hover_sat_scope;
                }
                char tt[128];
                snprintf(tt, sizeof(tt), "%s\nNORAD: %.6s", hover_sat_scope->name, hover_sat_scope->norad_id);
                Vector2 tt_size = MeasureTextEx(customFont, tt, 14 * cfg->ui_scale, 1.0f);
                
                float t_x = hover_pos.x + 10 * cfg->ui_scale;
                float t_y = hover_pos.y;
                if (t_x + tt_size.x + 8 * cfg->ui_scale > sc_x + scopeWindow.width) t_x = hover_pos.x - tt_size.x - 10 * cfg->ui_scale;
                
                Rectangle ttRec = {t_x, t_y, tt_size.x + 8 * cfg->ui_scale, tt_size.y + 7 * cfg->ui_scale};
                DrawRectangleRounded(ttRec, 0.1f, 8, ApplyAlpha(cfg->ui_bg, 0.9f));
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
                DrawRectangleRoundedLines(ttRec, 0.1f, 8, 1.5f * cfg->ui_scale, cfg->ui_secondary);
#else
                DrawRectangleRoundedLines(ttRec, 0.1f, 8, cfg->ui_secondary);
#endif
                DrawUIText(customFont, tt, t_x + 4 * cfg->ui_scale, t_y + 4 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_main);
            }

            /* bottom control panel area */
            float ctrl_y = center.y + scope_radius + 15 * cfg->ui_scale;

            // sep toggle button from standard button styles
            int old_toggle_base = GuiGetStyle(TOGGLE, BASE_COLOR_PRESSED);
            int old_toggle_bord = GuiGetStyle(TOGGLE, BORDER_COLOR_PRESSED);
            int old_toggle_text = GuiGetStyle(TOGGLE, TEXT_COLOR_PRESSED);

            GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, GuiGetStyle(DEFAULT, BASE_COLOR_NORMAL));
            GuiSetStyle(TOGGLE, BORDER_COLOR_PRESSED, ColorToInt(cfg->ui_accent));
            GuiSetStyle(TOGGLE, TEXT_COLOR_PRESSED, ColorToInt(cfg->ui_accent));

            GuiToggle((Rectangle){sc_x + 15 * cfg->ui_scale, ctrl_y, scopeWindow.width - 60 * cfg->ui_scale, 24 * cfg->ui_scale}, "Lock onto targeted", &scope_lock);

            GuiSetStyle(TOGGLE, BASE_COLOR_PRESSED, old_toggle_base);
            GuiSetStyle(TOGGLE, BORDER_COLOR_PRESSED, old_toggle_bord);
            GuiSetStyle(TOGGLE, TEXT_COLOR_PRESSED, old_toggle_text);

            // visibility toggle for quick additions to satellite manager
            if (!*ctx->selected_sat) GuiDisable();
            bool is_vis = *ctx->selected_sat && (*ctx->selected_sat)->is_active;
            if (GuiButton((Rectangle){sc_x + scopeWindow.width - 39 * cfg->ui_scale, ctrl_y, 24 * cfg->ui_scale, 24 * cfg->ui_scale}, is_vis ? "#44#" : "#45#")) {
                if (*ctx->selected_sat) {
                    (*ctx->selected_sat)->is_active = !(*ctx->selected_sat)->is_active;
                    SaveSatSelection();
                }
            }
            if (!*ctx->selected_sat) GuiEnable();

            ctrl_y += 30 * cfg->ui_scale;

            if (scope_lock) GuiDisable();

            GuiLabel((Rectangle){sc_x + 15 * cfg->ui_scale, ctrl_y, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "Az:");
            GuiSlider((Rectangle){sc_x + 55 * cfg->ui_scale, ctrl_y, 200 * cfg->ui_scale, 20 * cfg->ui_scale}, "", "", &scope_az, 0.0f, 360.0f);
            AdvancedTextBox((Rectangle){sc_x + 265 * cfg->ui_scale, ctrl_y, 80 * cfg->ui_scale, 24 * cfg->ui_scale}, text_scope_az, 16, &edit_scope_az, true);
            if (!edit_scope_az && !scope_lock) snprintf(text_scope_az, sizeof(text_scope_az), "%.1f", scope_az);
            else if (edit_scope_az && !scope_lock) scope_az = atof(text_scope_az);
            ctrl_y += 30 * cfg->ui_scale;

            GuiLabel((Rectangle){sc_x + 15 * cfg->ui_scale, ctrl_y, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "El:");
            GuiSlider((Rectangle){sc_x + 55 * cfg->ui_scale, ctrl_y, 200 * cfg->ui_scale, 20 * cfg->ui_scale}, "", "", &scope_el, -90.0f, 90.0f);
            AdvancedTextBox((Rectangle){sc_x + 265 * cfg->ui_scale, ctrl_y, 80 * cfg->ui_scale, 24 * cfg->ui_scale}, text_scope_el, 16, &edit_scope_el, true);
            if (!edit_scope_el && !scope_lock) snprintf(text_scope_el, sizeof(text_scope_el), "%.1f", scope_el);
            else if (edit_scope_el && !scope_lock) scope_el = atof(text_scope_el);
            ctrl_y += 30 * cfg->ui_scale;
            
            if (scope_lock) GuiEnable();

            GuiLabel((Rectangle){sc_x + 15 * cfg->ui_scale, ctrl_y, 40 * cfg->ui_scale, 24 * cfg->ui_scale}, "Beam:");
            GuiSlider((Rectangle){sc_x + 55 * cfg->ui_scale, ctrl_y, 200 * cfg->ui_scale, 20 * cfg->ui_scale}, "", "", &scope_beam, 1.0f, 120.0f);
            AdvancedTextBox((Rectangle){sc_x + 265 * cfg->ui_scale, ctrl_y, 80 * cfg->ui_scale, 24 * cfg->ui_scale}, text_scope_beam, 16, &edit_scope_beam, true);
            if (!edit_scope_beam) snprintf(text_scope_beam, sizeof(text_scope_beam), "%.1f", scope_beam);
            else scope_beam = atof(text_scope_beam);
            ctrl_y += 35 * cfg->ui_scale;

            GuiCheckBox((Rectangle){sc_x + 15 * cfg->ui_scale, ctrl_y, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "LEO", &scope_show_leo);
            GuiCheckBox((Rectangle){sc_x + 90 * cfg->ui_scale, ctrl_y, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "HEO/MEO", &scope_show_heo);
            GuiCheckBox((Rectangle){sc_x + 200 * cfg->ui_scale, ctrl_y, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "GEO", &scope_show_geo);
            GuiCheckBox((Rectangle){sc_x + 275 * cfg->ui_scale, ctrl_y, 20 * cfg->ui_scale, 20 * cfg->ui_scale}, "Trails", &scope_show_trails);
            RotatorDrawScopeOverlay(cfg, customFont, center.x, center.y, scope_radius, scope_az, scope_el, scope_beam, sc_x, ctrl_y);

            break;
        }

        case WND_SAT_INFO:
        {
            if (!show_sat_info_dialog || !*ctx->selected_sat)
                break;
            if (drag_sat_info)
            {
                si_x = GetMousePosition().x - drag_sat_info_off.x;
                si_y = GetMousePosition().y - drag_sat_info_off.y;
                SnapWindow(&si_x, &si_y, satInfoWindow.width, satInfoWindow.height, cfg);
            }
            satInfoWindow.x = si_x; satInfoWindow.y = si_y;
            if (DrawMaterialWindow(satInfoWindow, TextFormat("#15# %s", (*ctx->selected_sat)->name), cfg, customFont, true))
            {
                show_sat_info_dialog = false;
                *ctx->selected_sat = NULL;
                last_selected_sat = NULL;
            }

            float btn_sz = 16 * cfg->ui_scale;
            Rectangle rollupBtn = {satInfoWindow.x + satInfoWindow.width - 2 * btn_sz - 10 * cfg->ui_scale, satInfoWindow.y + 4 * cfg->ui_scale, btn_sz, btn_sz};
            int old_base = GuiGetStyle(BUTTON, BASE_COLOR_NORMAL);
            int old_bord = GuiGetStyle(BUTTON, BORDER_COLOR_NORMAL);
            GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, ColorToInt(BLANK));
            GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, ColorToInt(BLANK));
            
            if (GuiButton(rollupBtn, si_rolled_up ? "#120#" : "#121#")) {
                si_rolled_up = !si_rolled_up;
            }
            
            GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, old_base);
            GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, old_bord);

            if (si_rolled_up) break;

            if (*ctx->selected_sat)
            {
                Satellite *sat = *ctx->selected_sat;
                double r_km = Vector3Length(sat->current_pos);
                double v_kms = sqrt(MU * (2.0 / r_km - 1.0 / sat->semi_major_axis));
                float lat_deg = asinf(sat->current_pos.y / r_km) * RAD2DEG;
                float lon_deg = (atan2f(-sat->current_pos.z, sat->current_pos.x) - ((ctx->gmst_deg + cfg->earth_rotation_offset) * DEG2RAD)) * RAD2DEG;
                while (lon_deg > 180.0f) lon_deg -= 360.0f;
                while (lon_deg < -180.0f) lon_deg += 360.0f;

                Vector3 sun_pos = calculate_sun_position(*ctx->current_epoch);
                Vector3 sun_dir = Vector3Normalize(sun_pos);
                bool eclipsed = is_sat_eclipsed(sat->current_pos, sun_dir);

                double c_az, c_el;
                get_az_el(sat->current_pos, ctx->gmst_deg, home_location.lat, home_location.lon, home_location.alt, &c_az, &c_el);
                double s_range = get_sat_range(sat, *ctx->current_epoch, home_location);

                double t_peri_unix, t_apo_unix;
                get_apsis_times(sat, *ctx->current_epoch, &t_peri_unix, &t_apo_unix);
                double real_rp = Vector3Length(calculate_position(sat, t_peri_unix)) - EARTH_RADIUS_KM;
                double real_ra = Vector3Length(calculate_position(sat, t_apo_unix)) - EARTH_RADIUS_KM;

                double period_min = (2.0 * PI / sat->mean_motion) / 60.0;
                double revs_per_day = (sat->mean_motion * 86400.0) / (2.0 * PI);

                double dt = 0.1 / 86400.0;
                double r1 = get_sat_range(sat, *ctx->current_epoch - dt, home_location);
                double r2 = get_sat_range(sat, *ctx->current_epoch + dt, home_location);
                double range_rate = (r2 - r1) / 0.2;

                Rectangle contentRec = {0, 0, satInfoWindow.width - 32 * cfg->ui_scale, 580 * cfg->ui_scale};
                Rectangle viewRec = {0};

                int oldFocusD = GuiGetStyle(DEFAULT, BORDER_COLOR_FOCUSED);
                int oldPressD = GuiGetStyle(DEFAULT, BORDER_COLOR_PRESSED);
                int oldFocusL = GuiGetStyle(LISTVIEW, BORDER_COLOR_FOCUSED);
                int oldPressL = GuiGetStyle(LISTVIEW, BORDER_COLOR_PRESSED);
                GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
                GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));
                GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, ColorToInt(cfg->window_border_focus));
                GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, ColorToInt(cfg->window_border_focus));

                GuiScrollPanel(
                    (Rectangle){si_x + 8 * cfg->ui_scale, si_y + 35 * cfg->ui_scale, satInfoWindow.width - 16 * cfg->ui_scale, satInfoWindow.height - 35 * cfg->ui_scale - 8 * cfg->ui_scale},
                    NULL,
                    contentRec,
                    &si_scroll,
                    &viewRec
                );

                GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, oldFocusD);
                GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, oldPressD);
                GuiSetStyle(LISTVIEW, BORDER_COLOR_FOCUSED, oldFocusL);
                GuiSetStyle(LISTVIEW, BORDER_COLOR_PRESSED, oldPressL);

                BeginScissorMode(viewRec.x, viewRec.y, viewRec.width, viewRec.height);
                float cur_x = viewRec.x + 4 * cfg->ui_scale + si_scroll.x;
                float cur_y = viewRec.y + 4 * cfg->ui_scale + si_scroll.y;
                bool show_copy_icon = false;
                static double sat_info_copied_time = 0.0;

                #define DRAW_HEADER(title) \
                    DrawUIText(customFont, title, cur_x, cur_y, 18 * cfg->ui_scale, cfg->ui_accent); \
                    cur_y += 26 * cfg->ui_scale

                #define DRAW_ROW(key, val) \
                    { \
                        const char* vstr = (val); \
                        DrawUIText(customFont, key, cur_x + 5 * cfg->ui_scale, cur_y, 16 * cfg->ui_scale, cfg->text_main); \
                        DrawUIText(customFont, vstr, cur_x + 140 * cfg->ui_scale, cur_y, 16 * cfg->ui_scale, cfg->text_secondary); \
                        Vector2 v_sz = MeasureTextEx(customFont, vstr, 16 * cfg->ui_scale, 1.0f); \
                        Rectangle v_rec = {cur_x + 140 * cfg->ui_scale, cur_y, v_sz.x, 16 * cfg->ui_scale}; \
                        if (is_topmost && CheckCollisionPointRec(GetMousePosition(), v_rec) && CheckCollisionPointRec(GetMousePosition(), viewRec)) { \
                            show_copy_icon = true; \
                            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { \
                                SetClipboardText(vstr); \
                                sat_info_copied_time = GetTime(); \
                            } \
                        } \
                        cur_y += 22 * cfg->ui_scale; \
                    }

                DRAW_HEADER("Identification");
                DRAW_ROW("NORAD ID:", TextFormat("%.6s", sat->norad_id));
                DRAW_ROW("Intl Desig:", TextFormat("%.8s", sat->intl_designator));

                cur_y += 10 * cfg->ui_scale;
                DRAW_HEADER("Orbital Information");
                DRAW_ROW("Altitude:", TextFormat("%.1f km", r_km - EARTH_RADIUS_KM));
                DRAW_ROW("Speed:", TextFormat("%.3f km/s", v_kms));
                DRAW_ROW("Latitude:", TextFormat("%.3f deg", lat_deg));
                DRAW_ROW("Longitude:", TextFormat("%.3f deg", lon_deg));
                DRAW_ROW("Inclination:", TextFormat("%.3f deg", sat->inclination * RAD2DEG));
                DRAW_ROW("Eccentricity:", TextFormat("%.5f", sat->eccentricity));
                DRAW_ROW("RAAN:", TextFormat("%.3f deg", sat->raan * RAD2DEG));
                DRAW_ROW("Arg Perigee:", TextFormat("%.3f deg", sat->arg_perigee * RAD2DEG));
                DRAW_ROW("Mean Anom:", TextFormat("%.3f deg", sat->mean_anomaly * RAD2DEG));
                DRAW_ROW("Mean Motion:", TextFormat("%.4f rev/d", revs_per_day));
                DRAW_ROW("Period:", TextFormat("%.1f min", period_min));
                DRAW_ROW("Semi-Major:", TextFormat("%.1f km", sat->semi_major_axis));
                DRAW_ROW("Perigee Alt:", TextFormat("%.1f km", real_rp));
                DRAW_ROW("Apogee Alt:", TextFormat("%.1f km", real_ra));
                DRAW_ROW("Eclipsed:", eclipsed ? "Yes" : "No");

                cur_y += 10 * cfg->ui_scale;
                DRAW_HEADER("Relative");
                DRAW_ROW("Azimuth:", TextFormat("%.1f deg", c_az));
                DRAW_ROW("Elevation:", TextFormat("%.1f deg", c_el));
                DRAW_ROW("Slant Range:", TextFormat("%.1f km", s_range));
                DRAW_ROW("Relative Speed:", TextFormat("%.3f km/s", range_rate));

                #undef DRAW_HEADER
                #undef DRAW_ROW

                EndScissorMode();

                if (show_copy_icon) {
                    Vector2 m = GetMousePosition();
                    const char *icon = (GetTime() - sat_info_copied_time < 1.0) ? "#112#" : "#16#";
                    int oldLbl = GuiGetStyle(LABEL, TEXT_COLOR_NORMAL);
                    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, ColorToInt((GetTime() - sat_info_copied_time < 1.0) ? cfg->ui_accent : cfg->text_main));
                    GuiLabel((Rectangle){m.x + 12 * cfg->ui_scale, m.y, 24 * cfg->ui_scale, 24 * cfg->ui_scale}, icon);
                    GuiSetStyle(LABEL, TEXT_COLOR_NORMAL, oldLbl);
                }
            }
            break;
        }

        case WND_ROTATOR:
        {
            if (!RotatorIsWindowVisible())
                break;
            RotatorUpdateDrag(cfg);
            RotatorDrawWindow(cfg, customFont, is_topmost);
            break;
        }

        default:
            break;
        }

        if (use_gui_disable)
        {
            GuiEnable();
            GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, old_window_disabled_text);
            GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, old_window_disabled_base);
            GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, old_window_disabled_border);
            GuiSetStyle(TEXTBOX, TEXT_COLOR_DISABLED, old_textbox_disabled_text);
            GuiSetStyle(TEXTBOX, BASE_COLOR_DISABLED, old_textbox_disabled_base);
            GuiSetStyle(TEXTBOX, BORDER_COLOR_DISABLED, old_textbox_disabled_border);
            GuiSetStyle(BUTTON, TEXT_COLOR_DISABLED, old_button_disabled_text);
            GuiSetStyle(BUTTON, BASE_COLOR_DISABLED, old_button_disabled_base);
            GuiSetStyle(BUTTON, BORDER_COLOR_DISABLED, old_button_disabled_border);
            GuiSetStyle(TOGGLE, TEXT_COLOR_DISABLED, old_toggle_disabled_text);
            GuiSetStyle(TOGGLE, BASE_COLOR_DISABLED, old_toggle_disabled_base);
            GuiSetStyle(TOGGLE, BORDER_COLOR_DISABLED, old_toggle_disabled_border);
            GuiSetStyle(SLIDER, TEXT_COLOR_DISABLED, old_slider_disabled_text);
            GuiSetStyle(SLIDER, BASE_COLOR_DISABLED, old_slider_disabled_base);
            GuiSetStyle(SLIDER, BORDER_COLOR_DISABLED, old_slider_disabled_border);
            GuiSetStyle(CHECKBOX, TEXT_COLOR_DISABLED, old_checkbox_disabled_text);
            GuiSetStyle(CHECKBOX, BASE_COLOR_DISABLED, old_checkbox_disabled_base);
            GuiSetStyle(CHECKBOX, BORDER_COLOR_DISABLED, old_checkbox_disabled_border);
        }
    }

    if (show_tle_warning)
    {
        Rectangle tleWarnWindow = {(GetScreenWidth() - 480 * cfg->ui_scale) / 2.0f, (GetScreenHeight() - 160 * cfg->ui_scale) / 2.0f, 480 * cfg->ui_scale, 160 * cfg->ui_scale};
        if (DrawMaterialWindow(tleWarnWindow, "#193# TLEs Outdated", cfg, customFont, true))
            show_tle_warning = false;
        
        long days_old = data_tle_epoch > 0 ? (time(NULL) - data_tle_epoch) / 86400 : 999;
        char warn_msg[128];
        sprintf(warn_msg, "Your orbital data (TLEs) is %ld days old.", days_old);
        
        Vector2 msgSize = MeasureTextEx(customFont, warn_msg, 16 * cfg->ui_scale, 1.0f);
        DrawUIText(customFont, warn_msg, tleWarnWindow.x + (tleWarnWindow.width - msgSize.x) / 2.0f, tleWarnWindow.y + 40 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);

        const char *sub_msg = "Would you like to update it now?";
        Vector2 subSize = MeasureTextEx(customFont, sub_msg, 16 * cfg->ui_scale, 1.0f);
        DrawUIText(customFont, sub_msg, tleWarnWindow.x + (tleWarnWindow.width - subSize.x) / 2.0f, tleWarnWindow.y + 65 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
        
        float btnWidth = 140 * cfg->ui_scale;
        float spacing = 15 * cfg->ui_scale;
        float startX = tleWarnWindow.x + (tleWarnWindow.width - (3 * btnWidth + 2 * spacing)) / 2.0f;

        if (pull_state == PULL_BUSY) GuiDisable();
        if (GuiButton((Rectangle){startX, tleWarnWindow.y + 105 * cfg->ui_scale, btnWidth, 35 * cfg->ui_scale},
            pull_state == PULL_BUSY ? "Pulling.." : "#112# Update All")) {
            PullTLEData(cfg);
            show_tle_warning = false;
        }
        if (pull_state == PULL_BUSY) GuiEnable();
        if (GuiButton((Rectangle){startX + btnWidth + spacing, tleWarnWindow.y + 105 * cfg->ui_scale, btnWidth, 35 * cfg->ui_scale}, "#1# Manage")) {
            show_tle_warning = false;
            if (!show_tle_mgr_dialog) {
                FindSmartWindowPosition(400 * cfg->ui_scale, 500 * cfg->ui_scale, cfg, &tm_x, &tm_y);
                show_tle_mgr_dialog = true;
                BringToFront(WND_TLE_MGR);
            }
        }
        if (GuiButton((Rectangle){startX + 2 * (btnWidth + spacing), tleWarnWindow.y + 105 * cfg->ui_scale, btnWidth, 35 * cfg->ui_scale}, "#113# Ignore")) {
            show_tle_warning = false;
        }
    }

    if (cfg->show_first_run_dialog)
    {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), cfg->overlay_dim);
        Rectangle frRec = {(GetScreenWidth() - 520 * cfg->ui_scale) / 2.0f, (GetScreenHeight() - 240 * cfg->ui_scale) / 2.0f, 520 * cfg->ui_scale, 240 * cfg->ui_scale};
        DrawMaterialWindow(frRec, "#198# Welcome to TLEscope!", cfg, customFont, false);

        const char* msg1 = "Please select a graphics profile for your first run:";
        Vector2 msg1Size = MeasureTextEx(customFont, msg1, 16 * cfg->ui_scale, 1.0f);
        DrawUIText(customFont, msg1, frRec.x + (frRec.width - msg1Size.x) / 2.0f, frRec.y + 45 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
        
        float btnW = 220 * cfg->ui_scale;
        float btnH = 50 * cfg->ui_scale;
        float spacing = 20 * cfg->ui_scale;
        float startX = frRec.x + (frRec.width - (2 * btnW + spacing)) / 2.0f;

        Rectangle perfBtnRec = {startX, frRec.y + 90 * cfg->ui_scale, btnW, btnH};
        bool perfClicked = GuiButton(perfBtnRec, "");
        Vector2 pTitleSize = MeasureTextEx(customFont, "Performance", 16 * cfg->ui_scale, 1.0f);
        Vector2 pSubSize = MeasureTextEx(customFont, "(Low VFX)", 14 * cfg->ui_scale, 1.0f);
        DrawUIText(customFont, "Performance", perfBtnRec.x + (perfBtnRec.width - pTitleSize.x) / 2.0f, perfBtnRec.y + 8 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
        DrawUIText(customFont, "(Low VFX)", perfBtnRec.x + (perfBtnRec.width - pSubSize.x) / 2.0f, perfBtnRec.y + 26 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_secondary);

        if (perfClicked) {
            cfg->show_clouds = false;
            cfg->show_scattering = false;
            cfg->show_night_lights = true;
            cfg->target_fps = 60;
            cfg->hint_vsync = true;
            cfg->show_first_run_dialog = false;
            if (!cfg->hint_vsync) SetTargetFPS(cfg->target_fps);
            else SetTargetFPS(0);
            SaveAppConfig("settings.json", cfg);
            if (!show_help) {
                FindSmartWindowPosition(HELP_WINDOW_W * cfg->ui_scale, HELP_WINDOW_H * cfg->ui_scale, cfg, &hw_x, &hw_y);
                show_help = true;
                BringToFront(WND_HELP);
            }
            if (data_tle_epoch > 0 && time(NULL) - data_tle_epoch > 2 * 86400) show_tle_warning = true;
        }

        Rectangle aesBtnRec = {startX + btnW + spacing, frRec.y + 90 * cfg->ui_scale, btnW, btnH};
        bool aesClicked = GuiButton(aesBtnRec, "");
        Vector2 aTitleSize = MeasureTextEx(customFont, "Aesthetic", 16 * cfg->ui_scale, 1.0f);
        Vector2 aSubSize = MeasureTextEx(customFont, "(High VFX)", 14 * cfg->ui_scale, 1.0f);
        DrawUIText(customFont, "Aesthetic", aesBtnRec.x + (aesBtnRec.width - aTitleSize.x) / 2.0f, aesBtnRec.y + 8 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
        DrawUIText(customFont, "(High VFX)", aesBtnRec.x + (aesBtnRec.width - aSubSize.x) / 2.0f, aesBtnRec.y + 26 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_secondary);

        if (aesClicked) {
            cfg->show_clouds = true;
            cfg->show_scattering = true;
            cfg->show_night_lights = true;
            cfg->target_fps = 120;
            cfg->hint_vsync = true;
            cfg->show_first_run_dialog = false;
            if (!cfg->hint_vsync) SetTargetFPS(cfg->target_fps);
            else SetTargetFPS(0);
            SaveAppConfig("settings.json", cfg);
            if (!show_help) {
                FindSmartWindowPosition(HELP_WINDOW_W * cfg->ui_scale, HELP_WINDOW_H * cfg->ui_scale, cfg, &hw_x, &hw_y);
                show_help = true;
                BringToFront(WND_HELP);
            }
            if (data_tle_epoch > 0 && time(NULL) - data_tle_epoch > 2 * 86400) show_tle_warning = true;
        }
        
        const char* msg2 = "Settings can be tweaked later in the settings menu.";
        Vector2 msg2Size = MeasureTextEx(customFont, msg2, 14 * cfg->ui_scale, 1.0f);
        DrawUIText(customFont, msg2, frRec.x + (frRec.width - msg2Size.x) / 2.0f, frRec.y + 175 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_secondary);
    }

    /* render persistent bottom bar, stats, and tooltips */
    char top_text_render[128];
    sprintf(top_text_render, "%s", ctx->datetime_str);
    DrawUIText(
        customFont, top_text_render, btn_start_x - MeasureTextEx(customFont, top_text_render, 20 * cfg->ui_scale, 1.0f).x - 20 * cfg->ui_scale, GetScreenHeight() - 35 * cfg->ui_scale,
        20 * cfg->ui_scale, cfg->text_main
    );
    DrawUIText(
        customFont, TextFormat("Speed: %.10gx %s", *ctx->time_multiplier, (*ctx->time_multiplier == 0.0) ? "[PAUSED]" : ""), btn_start_x + buttons_w + 20 * cfg->ui_scale,
        GetScreenHeight() - 35 * cfg->ui_scale, 20 * cfg->ui_scale, cfg->text_main
    );

    if (update_available) {
        static double update_popup_start = 0.0;
        if (update_popup_start == 0.0) update_popup_start = GetTime();
        
        if (GetTime() - update_popup_start < 10.0) {
            const char *upd_text = "Update Available!";
            float upd_w = MeasureTextEx(customFont, upd_text, 16 * cfg->ui_scale, 1.0f).x;
            float upd_x = GetScreenWidth() - upd_w - 20 * cfg->ui_scale;
            float upd_y = GetScreenHeight() - 32 * cfg->ui_scale;
            
            Rectangle upd_rec = {upd_x, upd_y, upd_w, 16 * cfg->ui_scale};
            bool hover_upd = CheckCollisionPointRec(GetMousePosition(), upd_rec) && top_hovered_wnd == -1;
            
            if (hover_upd) {
                DrawRectangleRec((Rectangle){upd_rec.x, upd_rec.y + upd_rec.height, upd_rec.width, 1.0f}, cfg->ui_accent);
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    OpenURL("https://github.com/aweeri/TLEscope/releases/latest");
                }
            }
            DrawUIText(customFont, upd_text, upd_rec.x, upd_rec.y, 16 * cfg->ui_scale, hover_upd ? cfg->ui_accent : cfg->text_secondary);
        }
    }

    const char *tt_texts[19] = {
        "Settings",
        "TLE Manager",
        "Satellite Manager",
        "Pass Predictor",
        "Polar Plot",
        "Satellite Scope",
        "Help & Controls",
        "Toggle 2D/3D View",
        "Toggle Unselected Orbits",
        "Highlight Sunlit Orbits",
        "Slant Range Line",
        "Toggle Frame (ECI/Ecliptic)",
        "Toggle POV Mode",
        "Slower / Reverse",
        "Play / Pause",
        "Faster",
        "Real Time",
        "Set Date & Time",
        "Rotator Control"
    };

    for (int i = 0; i < 19; i++)
    {
        if (top_hovered_wnd == -1 && CheckCollisionPointRec(
                                         GetMousePosition(), (Rectangle[]){btnSet, btnTLEMgr, btnSatMgr, btnPasses, btnPolar, btnScope, btnHelp, btn2D3D, btnHideUnselected, btnSunlit, btnSlantRange, btnFrame, btnPOV, btnRewind,
                                                                          btnPlayPause, btnFastForward, btnNow, btnClock, btnRotator}[i]
                                     ))
        {
            tt_hover[i] += GetFrameTime();
            if (tt_hover[i] > 0.3f)
            {
                Vector2 m = GetMousePosition();
                float tw = MeasureTextEx(customFont, tt_texts[i], 14 * cfg->ui_scale, 1.0f).x + 12 * cfg->ui_scale;
                float tt_x = m.x + 10 * cfg->ui_scale, tt_y = m.y + 15 * cfg->ui_scale;
                if (tt_x + tw > GetScreenWidth())
                    tt_x = GetScreenWidth() - tw - 5 * cfg->ui_scale;
                if (tt_y + 24 * cfg->ui_scale > GetScreenHeight())
                    tt_y = m.y - 25 * cfg->ui_scale;
                Rectangle ttRec = {tt_x, tt_y, tw, 24 * cfg->ui_scale};
                DrawRectangleRounded(ttRec, 0.1f, 8, ApplyAlpha(cfg->ui_bg, 0.8f));
#if (defined(_WIN32) || defined(_WIN64)) && !defined(_M_ARM64)
                DrawRectangleRoundedLines(ttRec, 0.1f, 8, 1.0f * cfg->ui_scale, cfg->ui_secondary);
#else
                DrawRectangleRoundedLines(ttRec, 0.1f, 8, cfg->ui_secondary);
#endif
                DrawUIText(customFont, tt_texts[i], tt_x + 6 * cfg->ui_scale, tt_y + 4 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_main);
            }
        }
        else
            tt_hover[i] = 0.0f;
    }

    if (cfg->show_statistics)
    {
        // calculate dynamic values based on current simulation state
        int active_render_count = 0;
        int cached_count = 0;
        for (int i = 0; i < sat_count; i++)
        {
            if (satellites[i].is_active)
            {
                active_render_count++;
                if (satellites[i].orbit_cached)
                    cached_count++;
            }
        }

        int global_orbit_step = 1;
        if (active_render_count > 10000)
            global_orbit_step = 100;
        else if (active_render_count > 5000)
            global_orbit_step = 18;
        else if (active_render_count > 2000)
            global_orbit_step = 8;
        else if (active_render_count > 500)
            global_orbit_step = 4;
        else if (active_render_count > 200)
            global_orbit_step = 2;

        float stats_x = 10 * cfg->ui_scale;

        // UI Statistics
        DrawUIText(customFont, TextFormat("%3i FPS", GetFPS()), stats_x, 10 * cfg->ui_scale, 20 * cfg->ui_scale, cfg->ui_accent);
        DrawUIText(customFont, TextFormat("%i Sats (%i active)", sat_count, active_render_count), stats_x, 34 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
        DrawUIText(customFont, TextFormat("Orbit Step: %i", global_orbit_step), stats_x, 52 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);
        DrawUIText(customFont, TextFormat("Cache: %i/%i", cached_count, active_render_count), stats_x, 70 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);

        size_t sat_mem = sat_count * sizeof(Satellite);
        DrawUIText(customFont, TextFormat("Mem: %.2f MB", sat_mem / (1024.0f * 1024.0f)), stats_x, 88 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_secondary);

        int prop_per_sec = GetFPS() * 50; // based on the 50-sat async step in main.c
        DrawUIText(customFont, TextFormat("Prop Rate: %i/s", prop_per_sec), stats_x, 106 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->text_secondary);

        Vector3 sun_pos = calculate_sun_position(*ctx->current_epoch);
        DrawUIText(customFont, TextFormat("GMST: %.4f deg", ctx->gmst_deg), stats_x, 128 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->ui_accent);
        DrawUIText(customFont, TextFormat("Sun ECI: %.3f, %.3f, %.3f", sun_pos.x, sun_pos.y, sun_pos.z), stats_x, 144 * cfg->ui_scale, 14 * cfg->ui_scale, cfg->ui_accent);
    }

    bool show_real_time = (*ctx->time_multiplier == 1.0 && fabs(*ctx->current_epoch - get_current_real_time_epoch()) < (5.0 / 86400.0) && !*ctx->is_auto_warping);
    float y = GetScreenHeight() - 69 * cfg->ui_scale;
    float pair_spacing = 20 * cfg->ui_scale;
    bool rot_connected = RotatorIsConnected();

    float rt_text_w = MeasureTextEx(customFont, "REAL TIME", 16 * cfg->ui_scale, 1.0f).x;
    float rt_item_w = 10 * cfg->ui_scale + rt_text_w;
    float rot_item_w = RotatorConnectedItemWidth(cfg, customFont);

    int shown = (show_real_time ? 1 : 0) + (rot_connected ? 1 : 0);
    float total_w = 0.0f;
    if (shown == 1)
        total_w = show_real_time ? rt_item_w : rot_item_w;
    else if (shown == 2)
        total_w = rt_item_w + pair_spacing + rot_item_w;

    float cur_x = (GetScreenWidth() - total_w) / 2.0f;

    if (show_real_time)
    {
        float blink = (sinf(GetTime() * 6.0f) * 0.5f + 0.5f);
        Color rt_col = cfg->ui_accent;
        DrawCircleV((Vector2){cur_x, y + 7 * cfg->ui_scale}, 4.0f * cfg->ui_scale, ApplyAlpha(rt_col, 0.35f + 0.65f * blink));
        DrawUIText(customFont, "REAL TIME", cur_x + 10 * cfg->ui_scale, y, 16 * cfg->ui_scale, rt_col);
        cur_x += rt_item_w + (rot_connected ? pair_spacing : 0.0f);
    }

    if (rot_connected)
        RotatorDrawConnectedItem(cfg, customFont, cur_x, y);

    if (show_exit_dialog)
    {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), ApplyAlpha(cfg->overlay_dim, 150.0f / 180.0f));
        Rectangle exitRec = {(GetScreenWidth() - 300 * cfg->ui_scale) / 2.0f, (GetScreenHeight() - 140 * cfg->ui_scale) / 2.0f, 300 * cfg->ui_scale, 140 * cfg->ui_scale};
        if (DrawMaterialWindow(exitRec, "#159# Exit Application", cfg, customFont, true))
            show_exit_dialog = false;
        DrawUIText(customFont, "Are you sure you want to exit?", exitRec.x + 25 * cfg->ui_scale, exitRec.y + 45 * cfg->ui_scale, 16 * cfg->ui_scale, cfg->text_main);
        if (GuiButton((Rectangle){exitRec.x + 20 * cfg->ui_scale, exitRec.y + 85 * cfg->ui_scale, 120 * cfg->ui_scale, 30 * cfg->ui_scale}, "Yes"))
            *ctx->exit_app = true;
        if (GuiButton((Rectangle){exitRec.x + 160 * cfg->ui_scale, exitRec.y + 85 * cfg->ui_scale, 120 * cfg->ui_scale, 30 * cfg->ui_scale}, "No"))
            show_exit_dialog = false;
    }
}
