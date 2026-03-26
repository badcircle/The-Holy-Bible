//
// render_tray.cpp
// Parallel-verse tray — slides in from the right, showing a selected verse
// side-by-side across all loaded translations.
//
#include "app_internal.h"
#include "imgui.h"
#include "book_names.h"
#include "colors.h"
#include <cstdio>
#include <string>
#include <vector>

static constexpr float TRAY_W = 340.0f;

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

void tray_load_verse(AppState* app,
                     int testament_id, int book_id, int chapter, int verse_id)
{
    app->tray_testament = testament_id;
    app->tray_book_id   = book_id;
    app->tray_chapter   = chapter;
    app->tray_verse_id  = verse_id;
    app->tray_entries.clear();

    std::vector<Verse> buf(500);
    for (auto& t : app->translations) {
        BibleDB* db = open_translation_db(t);
        if (!db) continue;
        int n = bible_db_get_chapter(db, testament_id, book_id, chapter,
                                     buf.data(), (int)buf.size());
        bible_db_close(db);
        if (n <= 0) continue;
        for (int i = 0; i < n; ++i) {
            if (buf[i].verse_id == verse_id) {
                TrayEntry e;
                snprintf(e.translation, sizeof(e.translation), "%s", t.name);
                snprintf(e.text, sizeof(e.text), "%s", buf[i].text);
                app->tray_entries.push_back(e);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// render_tray is called INSIDE the root Begin/End so the child always draws on
// top of the reader child (later submission = higher draw order in same parent).
void render_tray(AppState* app) {
    // Animate open/close
    float target = app->tray_open ? 1.0f : 0.0f;
    app->tray_anim += (target - app->tray_anim) * ImGui::GetIO().DeltaTime * 12.0f;
    if (app->tray_anim < 0.002f) app->tray_anim = 0.0f;
    if (app->tray_anim > 0.998f) app->tray_anim = 1.0f;
    if (app->tray_anim <= 0.0f) return;

    ImGuiIO& io = ImGui::GetIO();
    float tray_w = TRAY_W < io.DisplaySize.x * 0.45f ? TRAY_W : io.DisplaySize.x * 0.45f;
    float tray_h = io.DisplaySize.y - TOOLBAR_H;
    // Slide in from the right: when anim=1 the tray sits flush at the right edge
    float tray_x = io.DisplaySize.x - tray_w * app->tray_anim;

    // Position within the root window (zero-padding root, so local == screen coords)
    ImGui::SetCursorPos({tray_x, TOOLBAR_H});

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    {14.0f, 12.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,    0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,  1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.11f, 0.10f, 0.09f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_Border,  {0.28f, 0.25f, 0.18f, 1.0f});

    bool visible = ImGui::BeginChild("##tray", {tray_w, tray_h}, true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    if (!visible) { ImGui::EndChild(); return; }

    if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);

    // Header: verse reference + close button
    if (app->tray_verse_id > 0) {
        const char* bname = book_display_name(app->tray_testament, app->tray_book_id);
        if (!bname) {
            for (auto& b : app->books)
                if (b.testament_id == app->tray_testament && b.book_id == app->tray_book_id)
                    { bname = b.short_name; break; }
        }
        char ref[80];
        snprintf(ref, sizeof(ref), "%s %d:%d",
                 bname ? bname : "?", app->tray_chapter, app->tray_verse_id);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_HEADER);
        ImGui::TextUnformatted(ref);
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted("Parallel Verses");
        ImGui::PopStyleColor();
    }

    // Close button — right-aligned in the header row
    {
        float cbw = ImGui::CalcTextSize("close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth()
                        - cbw - ImGui::GetStyle().WindowPadding.x);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f, 0.25f, 0.18f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.38f, 0.33f, 0.22f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        if (ImGui::SmallButton("close")) app->tray_open = false;
        ImGui::PopStyleColor(4);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Scrollable verse list
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.0f, 0.0f, 0.0f, 0.0f});
    ImGui::BeginChild("##tray_body", {0.0f, 0.0f}, false, 0);
    ImGui::PopStyleColor();

    if (app->tray_verse_id == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("Click any verse to see it across all translations.");
        ImGui::PopStyleColor();
    } else if (app->tray_entries.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextWrapped("This verse is not present in any other loaded translation.");
        ImGui::PopStyleColor();
    } else {
        float avail_w = ImGui::GetContentRegionAvail().x;

        for (int ei = 0; ei < (int)app->tray_entries.size(); ++ei) {
            auto& e = app->tray_entries[ei];
            bool rtl = name_is_rtl(e.translation);

            // Translation label
            ImGui::PushStyleColor(ImGuiCol_Text, COL_VERSE_NUM);
            if (rtl) {
                float lw = ImGui::CalcTextSize(e.translation).x;
                ImGui::SetCursorPosX(avail_w - lw);
            }
            ImGui::TextUnformatted(e.translation);
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Verse text
            ImFont* text_font = (rtl && app->fonts.hebrew) ? app->fonts.hebrew : app->fonts.body;
            if (text_font) ImGui::PushFont(text_font);
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_BODY);

            if (rtl) {
                // Per-line right-alignment (same approach as the reader)
                const char* p = e.text;
                std::string cur_line;
                float cur_w = 0.0f;
                bool first = true;

                auto flush = [&](const std::string& line) {
                    float lw = ImGui::CalcTextSize(line.c_str()).x;
                    float lx = avail_w - lw;
                    if (lx < 0.0f) lx = 0.0f;
                    if (!first) ImGui::SetCursorPosX(lx);
                    else { ImGui::SetCursorPosX(lx); first = false; }
                    ImGui::TextUnformatted(line.c_str());
                };

                while (*p) {
                    while (*p == ' ') p++;
                    if (!*p) break;
                    const char* ws = p;
                    while (*p && *p != ' ') p++;
                    std::string word(ws, p);
                    float ww = ImGui::CalcTextSize(word.c_str()).x;
                    float sw = cur_line.empty() ? 0.0f : ImGui::CalcTextSize(" ").x;
                    if (!cur_line.empty() && cur_w + sw + ww > avail_w) {
                        flush(cur_line);
                        cur_line = word; cur_w = ww;
                    } else {
                        if (!cur_line.empty()) { cur_line += ' '; cur_w += sw; }
                        cur_line += word; cur_w += ww;
                    }
                }
                if (!cur_line.empty()) flush(cur_line);
            } else {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(e.text);
                ImGui::PopTextWrapPos();
            }

            ImGui::PopStyleColor();
            if (text_font) ImGui::PopFont();

            if (ei < (int)app->tray_entries.size() - 1) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Separator, {0.35f, 0.30f, 0.20f, 0.4f});
                ImGui::Separator();
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
        }
    }

    ImGui::EndChild(); // ##tray_body
    if (app->fonts.ui) ImGui::PopFont();
    ImGui::EndChild(); // ##tray
}
