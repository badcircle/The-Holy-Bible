//
// render_sidebar.cpp
// Book list sidebar.
//
#include "app_internal.h"
#include "imgui.h"
#include "book_names.h"
#include "colors.h"
#include <cstdio>

static void push_body_style(const AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_BODY);
    if (app->fonts.body) ImGui::PushFont(app->fonts.body);
}

static void pop_body_style(const AppState* app) {
    if (app->fonts.body) ImGui::PopFont();
    ImGui::PopStyleColor();
}

void render_sidebar(AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SIDEBAR_BG);
    ImGui::BeginChild("##sidebar", ImVec2(app->sidebar_width, 0), false, 0);

    ImGui::Spacing();
    ImGui::Spacing();

    if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);

    // More breathing room between items
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {8.0f, 5.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  {6.0f, 4.0f});

    int current_testament = 0;
    for (auto& book : app->books) {
        if (book.testament_id != current_testament) {
            current_testament = book.testament_id;
            if (current_testament > 1) { ImGui::Spacing(); ImGui::Spacing(); }
            ImGui::SetCursorPosX(14.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
            const char* tsmt_label =
                current_testament == 1 ? "OLD TESTAMENT" :
                current_testament == 2 ? "NEW TESTAMENT" :
                                         "DEUTEROCANONICAL";
            ImGui::TextUnformatted(tsmt_label);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosX(14.0f);
            ImGui::Separator();
            ImGui::Spacing();
        }

        bool is_selected = (book.testament_id == app->current_testament_id &&
                            book.book_id      == app->current_book_id);

        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Header,        COL_SELECTED);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, COL_SELECTED);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  COL_SELECTED);
        }

        // Indent the selectable and give it a bit of internal padding
        ImGui::SetCursorPosX(8.0f);
        const char* dn = book_display_name(book.testament_id, book.book_id);
        if (!dn) dn = book.short_name;
        char label[80];
        snprintf(label, sizeof(label), "  %s##bk%d%d",
                 dn, book.testament_id, book.book_id);

        if (ImGui::Selectable(label, is_selected,
                              ImGuiSelectableFlags_None,
                              ImVec2(app->sidebar_width - 16.0f, 0))) {
            app->show_cover = false;
            app_open_book(app, book.testament_id, book.book_id);
        }

        if (is_selected) ImGui::PopStyleColor(3);
    }

    ImGui::PopStyleVar(2);
    if (app->fonts.ui) ImGui::PopFont();

    // Scroll-past-end padding
    ImGui::Dummy({0.0f, ImGui::GetWindowHeight() * 0.75f});

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}
