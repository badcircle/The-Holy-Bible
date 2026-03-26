//
// app_render.cpp
// Top-level frame entry point — composes all rendering subsystems.
//
#include "app_internal.h"
#include "imgui.h"
#include "colors.h"

void app_render(AppState* app) {
    // Full-screen window (no decoration, no padding)
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   {0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, COL_BG_DARK);

    ImGui::Begin("##root", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
                  ImGuiWindowFlags_NoScrollbar);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // Toolbar row
    render_toolbar(app);

    // Below toolbar: [sidebar |] reader / cover
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
    if (app->sidebar_visible) {
        render_sidebar(app);
        ImGui::SameLine();
    }
    ImGui::PopStyleVar();

    if (app->show_cover)
        render_cover(app);
    else
        render_reader(app);

    // Tray is a child of the root window submitted after the reader,
    // so it always draws on top without any z-order tricks.
    render_tray(app);

    if (app->request_shortcuts_popup) {
        app->request_shortcuts_popup = false;
        if (!ImGui::IsPopupOpen("##shortcuts"))
            ImGui::OpenPopup("##shortcuts");
    }
    render_shortcuts_popup(app);

    ImGui::End();
}
