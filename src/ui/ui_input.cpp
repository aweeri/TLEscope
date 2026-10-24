#include "ui_input.h"

#include "ui_layout.h"
#include "notifications.h"
#include "hints.h"
#include "tutorial.h"
#include "core/config.h"
#include "core/location.h"
#include "util/log.h"

#include <imgui.h>

bool UIHandleGlobalEscape(UIContext *ctx, AppConfig *cfg)
{
    if (!ctx || !cfg)
        return false;

    /* never steal Escape from a focused text field: ImGui handles it there */
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput)
        return false;

    if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
        return false;

    if (ImGui::IsPopupOpen("",
                           ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    {
        UICloseTopModal(cfg);
        return true;
    }

    if (TutorialActive(cfg))
    {
        HintDismiss();
        cfg->show_tutorial = false;
        return true;
    }

    if (NotifyCount() > 0)
    {
        NotifyClearAll();
        return true;
    }

    if (ctx->picking_home && *ctx->picking_home)
    {
        *ctx->picking_home = false;
        pick_location_index = -1;
        LOG_INFO("Home location picking cancelled");
        return true;
    }

    if (ctx->selected_sat && *ctx->selected_sat)
    {
        *ctx->selected_sat = NULL;
        return true;
    }

    if (g_layout.clean_view)
    {
        LayoutToggleCleanView();
        return true;
    }

    UIRequestExit();
    return true;
}
