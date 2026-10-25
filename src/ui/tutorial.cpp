/*
 * tutorial.cpp - first-boot tutorial, a thin layer over the hint system.
 *
 * The 7-step table below is fed to the generic hint system (hints.h), which
 * renders the dim/spotlight/hint box over everything and handles advancing.
 * This file only owns the step data, the step -> on-screen target mapping and
 * the show_tutorial persistence flag.
 */

#include "tutorial.h"

#include "hints.h"
#include "ui.h"
#include "ui_layout.h"
#include "core/config.h"

#include "IconsFontAwesome6.h"

/* -- Step table ------------------------------------------------------------ */

static const Hint kTutorialHints[TUTORIAL_STEP_COUNT] = {
    { { ICON_FA_DATABASE,      "Data Sources",      "Add your TLE / OMM data here, then pull to cache it offline." } },
    { { ICON_FA_SATELLITE,     "Satellite Manager", "Enable the satellites you want to track, or star your favourites." } },
    { { ICON_FA_LAYER_GROUP,   "Layers",            "Choose what gets drawn on the map here: orbits, ground coverage, grids and more. Each row has its own scope and options." } },
    { { ICON_FA_ROUTE,         "Transit Tracking",  "Upcoming passes are listed here, with extra information available under the [Full Overview] window." } },
    { { ICON_FA_WRENCH,        "Tools",             "Extra panels live here. Here, you can also choose which sidebar they appear on." } },
    { { ICON_FA_GEAR,          "Settings",          "Set your home location under Locations so that transit calculations use your site." } },
    { { ICON_FA_GLOBE,         "3D / 2D View",      "Flip between the 3D globe and the flat 2D map (M)." } },
    { { ICON_FA_TABLE_COLUMNS, "Panel layout",      "Drag panel headers to rearrange their placement." } },
};

/* -- Target resolution ----------------------------------------------------- */

/* pick the on-screen rect the current step points at; falls back gracefully
 * when the specific panel is not currently drawn. */
static bool ResolveStepTarget(int step, UIRect *out)
{
    switch (step)
    {
    case 0: if (LayoutGetPanelRect(PANEL_DATA_SOURCES, out)) return true; break;
    case 1: if (LayoutGetPanelRect(PANEL_SAT_MGR, out))      return true; break;
    case 2: if (LayoutGetPanelRect(PANEL_LAYERS, out))       return true; break;
    case 3: if (LayoutGetPanelRect(PANEL_TRANSIT, out))      return true; break;
    case 4: if (LayoutGetToolsButtonRect(out))               return true; break;
    case 5: if (LayoutGetSettingsButtonRect(out))            return true; break;
    case 6: if (LayoutGetViewToggleRect(out))                return true; break;
    case 7: if (LayoutGetSidebarRect(SIDEBAR_LEFT, out))     return true; break;
    }
    if (LayoutGetSidebarRect(SIDEBAR_LEFT, out))  return true;
    if (LayoutGetSidebarRect(SIDEBAR_RIGHT, out)) return true;
    if (LayoutGetNotchRect(SIDEBAR_LEFT, out))    return true;
    return false;
}

/* -- Public API ------------------------------------------------------------ */

static bool s_running = false; /* tutorial sequence currently driven */

bool TutorialActive(const AppConfig *cfg)
{
    return cfg && cfg->show_tutorial;
}

void TutorialLaunch(AppConfig *cfg)
{
    if (!cfg) return;
    cfg->show_tutorial = true;
    SaveAppConfig("settings.json", cfg);
    HintStartSequence(kTutorialHints, TUTORIAL_STEP_COUNT, true);
    s_running = true;
}

void DrawTutorialOverlay(UIContext *ctx, AppConfig *cfg)
{
    (void)ctx;
    if (!cfg) return;

    if (TutorialActive(cfg) && !s_running)
    {
        HintStartSequence(kTutorialHints, TUTORIAL_STEP_COUNT, true);
        s_running = true;
    }

    if (!s_running) return;

    if (!HintIsActive())
    {
        /* finished or dismissed: clear the persisted flag */
        s_running = false;
        cfg->show_tutorial = false;
        SaveAppConfig("settings.json", cfg);
        return;
    }

    /* refresh the current step's spotlight target from this frame's layout */
    UIRect t;
    HintSetTarget(ResolveStepTarget(HintStepIndex(), &t) ? &t : NULL);
}
