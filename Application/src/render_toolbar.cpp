//
// render_toolbar.cpp
// Title bar / toolbar (borderless drag zone + navigation + window controls)
// and the keyboard shortcuts popup.
//
#include "app_internal.h"
#include "imgui.h"
#include "book_names.h"
#include "colors.h"
#include <cstdio>

// ---------------------------------------------------------------------------
// Keyboard shortcuts popup
// ---------------------------------------------------------------------------

void render_shortcuts_popup(AppState* app) {
    ImGuiIO& io = ImGui::GetIO();
    float pw = 480.0f;
    if (pw > io.DisplaySize.x - 40.0f) pw = io.DisplaySize.x - 40.0f;

    ImGui::SetNextWindowPos(
        {io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({pw, 80.0f}, {pw, io.DisplaySize.y * 0.85f});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    bool open = ImGui::BeginPopupModal("##shortcuts", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!open) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();

    if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);

    // Title + close
    static constexpr ImVec4 C_TITLE = {0.92f, 0.87f, 0.65f, 1.0f};
    ImGui::PushStyleColor(ImGuiCol_Text, C_TITLE);
    ImGui::TextUnformatted("Keyboard Shortcuts");
    ImGui::PopStyleColor();
    {
        float cbw = ImGui::CalcTextSize("close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - cbw - ImGui::GetStyle().WindowPadding.x);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f, 0.25f, 0.18f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.38f, 0.33f, 0.22f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        if (ImGui::SmallButton("close")) ImGui::CloseCurrentPopup();
        ImGui::PopStyleColor(4);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Row helper: key column + description column
    static constexpr float KEY_W = 160.0f;
    static constexpr ImVec4 C_KEY = {0.85f, 0.75f, 0.50f, 1.0f};
    auto row = [&](const char* keys, const char* desc) {
        ImGui::PushStyleColor(ImGuiCol_Text, C_KEY);
        ImGui::TextUnformatted(keys);
        ImGui::PopStyleColor();
        ImGui::SameLine(KEY_W);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_BODY);
        ImGui::TextUnformatted(desc);
        ImGui::PopStyleColor();
    };
    auto section = [&](const char* label) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Separator, {0.35f, 0.30f, 0.18f, 0.6f});
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    };

    section("NAVIGATION");
    row("Left / Right",       "Previous / next chapter");
    row("Page Up / Page Down", "Previous / next chapter");
    row("Up / Down",          "Scroll");

    section("VIEW");
    row("F2",  "Toggle book sidebar");
    row("Tab", "Toggle parallel-verse tray");
    row("F11", "Toggle fullscreen");

    section("TEXT SIZE");
    row("Ctrl  +  /  -",       "Increase / decrease font size");
    row("Ctrl  0",             "Reset font size");
    row("Numpad  +  /  -",     "Increase / decrease font size");
    row("A+  /  A-  (toolbar)", "Increase / decrease font size");

    section("OTHER");
    row("F1",  "Show this help");
    row("Esc", "Close popups");

    ImGui::Spacing();
    if (app->fonts.ui) ImGui::PopFont();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void render_toolbar(AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_BG_DARK);
    ImGui::BeginChild("##toolbar", ImVec2(0, TOOLBAR_H), false,
                       ImGuiWindowFlags_NoScrollbar);

    // ---- Navigation -------------------------------------------------------
    // Push UI font first so GetFrameHeight() matches all toolbar items
    if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);
    ImGui::SetCursorPos({8.0f, (TOOLBAR_H - ImGui::GetFrameHeight()) * 0.5f});

    // Sidebar toggle (subtle arrow button)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f,0.20f,0.16f,1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.30f,0.27f,0.20f,1.0f});
    if (ImGui::ArrowButton("##sbToggle",
            app->sidebar_visible ? ImGuiDir_Left : ImGuiDir_Right))
        app->sidebar_visible = !app->sidebar_visible;
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0, 12);

    // Pre-compute nav geometry
    float nav_frame_h  = ImGui::GetFrameHeight();
    float nav_ch_btn_w = ImGui::CalcTextSize("Ch 999 / 999").x
                       + ImGui::GetStyle().FramePadding.x * 2.0f;
    float nav_w = nav_frame_h + 6.0f + nav_ch_btn_w + 6.0f + nav_frame_h;
    float nav_x = (ImGui::GetWindowWidth() - nav_w) * 0.5f;

    if (app->show_cover) {
        // Cover is showing — display a dim title, no chapter navigation
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted("The Holy Bible");
        ImGui::PopStyleColor();
    } else {
        // Chapter nav — centred in the toolbar
        const Book* book = find_book(app, app->current_testament_id, app->current_book_id);
        float frame_h  = nav_frame_h;
        float ch_btn_w = nav_ch_btn_w;

        // Book name — clipped to the space left of the nav
        float title_x = ImGui::GetCursorPosX();
        float title_w = nav_x - title_x - 8.0f;
        if (title_w > 0.0f) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushClipRect(p, {p.x + title_w, p.y + ImGui::GetTextLineHeightWithSpacing()}, true);
            bool hovered = ImGui::IsMouseHoveringRect(p, {p.x + title_w, p.y + ImGui::GetTextLineHeightWithSpacing()});
            ImGui::PushStyleColor(ImGuiCol_Text, hovered ? COL_TEXT_BODY : COL_HEADER);
            const char* tb_name = book ? book_display_name(book->testament_id, book->book_id) : nullptr;
            if (book && !tb_name) tb_name = book->short_name;
            ImGui::TextUnformatted(tb_name ? tb_name : "...");
            ImGui::PopStyleColor();
            ImGui::PopClipRect();
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    app->sidebar_visible = !app->sidebar_visible;
            }
        }

        ImGui::SameLine(nav_x);

        // Prev chapter
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) app_go_prev_chapter(app);
        ImGui::SameLine(0, 6);

        // Chapter display — fixed width so the next arrow never shifts
        char ch_label[32];
        snprintf(ch_label, sizeof(ch_label), "Ch %d / %d##chbtn",
                 app->current_chapter, app->num_chapters);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f,0.22f,0.15f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.35f,0.30f,0.18f,1.0f});
        if (ImGui::Button(ch_label, {ch_btn_w, 0}))
            ImGui::OpenPopup("##chpicker");
        ImGui::PopStyleColor(3);

        // Chapter picker popup
        ImGui::SetNextWindowSizeConstraints({180.0f, 0.0f}, {400.0f, 500.0f});
        if (ImGui::BeginPopup("##chpicker")) {
            if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.0f, 4.0f});
            for (int ch = 1; ch <= app->num_chapters; ++ch) {
                if ((ch - 1) % 8 != 0) ImGui::SameLine(0, 4);
                char btn[8];
                snprintf(btn, sizeof(btn), "%d##p%d", ch, ch);
                bool is_cur = (ch == app->current_chapter);
                if (is_cur) ImGui::PushStyleColor(ImGuiCol_Button, COL_SELECTED);
                if (ImGui::Button(btn, {36.0f, 28.0f})) {
                    app_load_chapter(app, app->current_testament_id,
                                     app->current_book_id, ch);
                    ImGui::CloseCurrentPopup();
                }
                if (is_cur) ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();
            if (app->fonts.ui) ImGui::PopFont();
            ImGui::EndPopup();
        }

        ImGui::SameLine(0, 6);
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) app_go_next_chapter(app);
    }

    // Translation dropdown — right of chapter nav, visible in all toolbar states
    if (app->translations.size() > 1) {
        ImGui::SameLine(nav_x + nav_w + 16.0f);
        ImGui::PushItemWidth(150.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4{0.20f, 0.18f, 0.16f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.28f, 0.25f, 0.20f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4{0.25f, 0.22f, 0.18f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_PopupBg,        ImVec4{0.15f, 0.14f, 0.12f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text,           COL_TEXT_BODY);
        const char* cur_name = app->translations[app->current_translation].name;
        if (ImGui::BeginCombo("##trans", cur_name, ImGuiComboFlags_HeightRegular)) {
            for (int i = 0; i < (int)app->translations.size(); ++i) {
                bool sel = (i == app->current_translation);
                ImGui::PushStyleColor(ImGuiCol_Text, sel ? COL_HEADER : COL_TEXT_BODY);
                if (ImGui::Selectable(app->translations[i].name, sel))
                    app_switch_translation(app, i);
                ImGui::PopStyleColor();
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(5);
        ImGui::PopItemWidth();
    }

    // Shared font-size helper — clamps and sets rebuild flag
    auto set_font_size = [&](float size) {
        if (size < 10.0f || size > 32.0f) return;
        app->font_size = size;
        app->request_font_rebuild = true;
    };

    // Text size controls (A- / A+), column toggle, and tray toggle — pinned to right edge
    {
        const ImGuiStyle& st  = ImGui::GetStyle();
        float frame_h  = ImGui::GetFrameHeight();
        float btn_w    = ImGui::CalcTextSize("A+").x + st.FramePadding.x * 2.0f;
        float col_w    = ImGui::CalcTextSize("||").x + st.FramePadding.x * 2.0f;
        float tray_btn_w = frame_h + 12.0f;
        float total_w    = tray_btn_w + col_w + 8.0f + btn_w * 2.0f + 4.0f + 12.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - total_w);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.22f, 0.20f, 0.16f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.30f, 0.27f, 0.20f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);

        // Column toggle: "|" = single col, "||" = multi-col (auto)
        {
            ImVec4 active_tint = {0.85f, 0.75f, 0.55f, 1.0f};
            if (app->single_col_override)  ImGui::PushStyleColor(ImGuiCol_Text, active_tint);
            if (ImGui::SmallButton("|"))   app->single_col_override = true;
            if (app->single_col_override)  ImGui::PopStyleColor();

            ImGui::SameLine(0, 2);

            if (!app->single_col_override) ImGui::PushStyleColor(ImGuiCol_Text, active_tint);
            if (ImGui::SmallButton("||"))  app->single_col_override = false;
            if (!app->single_col_override) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 8);

        if (ImGui::SmallButton("A-")) set_font_size(app->font_size - 2.0f);
        ImGui::SameLine(0, 4);
        if (ImGui::SmallButton("A+")) set_font_size(app->font_size + 2.0f);
        ImGui::SameLine(0, 12);

        // Tray toggle — arrow mirrors the sidebar toggle on the left
        if (ImGui::ArrowButton("##trayToggle",
                app->tray_open ? ImGuiDir_Right : ImGuiDir_Left))
            app->tray_open = !app->tray_open;

        ImGui::PopStyleColor(4);
    }

    // Keyboard shortcuts
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  false)) app_go_prev_chapter(app);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) app_go_next_chapter(app);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp,     false)) app_go_prev_chapter(app);
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown,   false)) app_go_next_chapter(app);
    if (ImGui::IsKeyPressed(ImGuiKey_F2,   false)) app->sidebar_visible = !app->sidebar_visible;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab,  false)) app->tray_open = !app->tray_open;
    if (ImGui::IsKeyPressed(ImGuiKey_F11,  false)) app->request_fullscreen_toggle = true;
    // Ctrl +/- / 0 and Keypad +/- — adjust text size
    {
        const ImGuiIO& kbio = ImGui::GetIO();
        if (kbio.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Minus, false)) set_font_size(app->font_size - 2.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_Equal, false)) set_font_size(app->font_size + 2.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_0,     false)) set_font_size(20.0f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd,      false)) set_font_size(app->font_size + 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) set_font_size(app->font_size - 2.0f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
        app->request_shortcuts_popup = true;

    if (app->fonts.ui) ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}
