#include "expo_mode.h"
#include "ui/ui.h"
#include "ui/ui_layout.h"
#include "ui/tools/tools_settings.h"
#include "core/theme.h"
#include <math.h>
#include <raylib.h>
#include "imgui.h"

bool ExpoModeEnabled(AppConfig *cfg)
{
    return ToolSettingGetBool(cfg, EXPO_KEY_ENABLED, false);
}

void ExpoModeSetEnabled(AppConfig *cfg, bool on)
{
    ToolSettingSetBool(cfg, EXPO_KEY_ENABLED, on);
}

float ExpoModeGetSize(AppConfig *cfg)
{
    return ToolSettingGetFloat(cfg, EXPO_KEY_SIZE, EXPO_SIZE_DEFAULT);
}

void ExpoModeSetSize(AppConfig *cfg, float size)
{
    if (size < EXPO_SIZE_MIN) size = EXPO_SIZE_MIN;
    if (size > EXPO_SIZE_MAX) size = EXPO_SIZE_MAX;
    ToolSettingSetFloat(cfg, EXPO_KEY_SIZE, size);
}

void ExpoModeDrawOverlay(Texture2D logo, Font font, AppConfig *cfg, Rectangle viewport)
{
    if (!ExpoModeEnabled(cfg)) return;
    float scale  = cfg->ui_scale * ExpoModeGetSize(cfg);
    float margin = 20.0f * cfg->ui_scale;
    /* keep clear of the top nav bar (hidden in clean view) so the overlay never bleeds onto it */
    float nav_h = g_layout.clean_view ? 0.0f : ImGui::GetFrameHeight();
    const char *title = "TLEscope";
    const char *gh    = "available on GitHub";
    float title_sz = 34.0f*scale, gh_sz = 18.0f*scale, logo_h = 44.0f*scale, gap = 8.0f*scale;
    Vector2 tm = MeasureTextEx(font,title,title_sz,1), gm = MeasureTextEx(font,gh,gh_sz,1);
    float logo_w = (logo.id!=0) ? logo.width*(logo_h/logo.height) : 0.0f;
    float text_h = tm.y + gap + gm.y;
    float group_h = (logo_h > text_h) ? logo_h : text_h;
    float text_w = (tm.x > gm.x) ? tm.x : gm.x;
    float group_w = logo_w + gap + text_w;
    float group_left = viewport.x + viewport.width - margin - group_w;
    float group_top = viewport.y + nav_h + margin;
    float text_x = group_left;
    float logo_x = group_left + text_w + gap;
    float logo_y = group_top + (group_h - logo_h)*0.5f;
    float text_y = group_top + (group_h - text_h)*0.5f;
    if (logo.id!=0) DrawTextureEx(logo, (Vector2){logo_x, logo_y}, 0.0f, logo_h/logo.height, Fade(WHITE,0.95f));
    DrawUIText(font, title, text_x, text_y,               title_sz, g_theme.ui.text);
    DrawUIText(font, gh,    text_x, text_y+tm.y+gap,      gh_sz,   g_theme.ui.text_dim);
}
