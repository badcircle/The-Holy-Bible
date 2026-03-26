//
// render_cover.cpp
// Cover splash screen shown until a book is selected.
//
#include "app_internal.h"
#include "imgui.h"
#include "colors.h"

void render_cover(AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_BG_DARK);
    ImGui::BeginChild("##cover", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);

    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;

    if (app->cover_tex && app->cover_tex_w > 0 && app->cover_tex_h > 0) {
        // Fit the image inside the available area while preserving aspect ratio
        float img_w = (float)app->cover_tex_w;
        float img_h = (float)app->cover_tex_h;
        float scale = (avail_w / img_w < avail_h / img_h) ? avail_w / img_w : avail_h / img_h;
        float draw_w = img_w * scale;
        float draw_h = img_h * scale;

        // Centre it
        ImGui::SetCursorPos({(avail_w - draw_w) * 0.5f,
                             (avail_h - draw_h) * 0.5f});
        ImGui::Image((ImTextureID)(uintptr_t)app->cover_tex,
                     ImVec2(draw_w, draw_h));
        if (ImGui::IsItemClicked() && !app->books.empty()) {
            app->show_cover = false;
            app_open_book(app, app->books[0].testament_id, app->books[0].book_id);
        }
    } else {
        // No image — show a simple centred prompt
        const char* msg = "Select a book to begin";
        ImVec2 sz = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos({(avail_w - sz.x) * 0.5f,
                             (avail_h - sz.y) * 0.5f});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted(msg);
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}
