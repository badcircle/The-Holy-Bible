//
// render_reader.cpp
// Main reading view: chapter heading, verse columns with dropcap, and the
// "Bible at a Glance" context popup (called only from this file).
//
#include "app_internal.h"
#include "imgui.h"
#include "book_names.h"
#include "colors.h"
#include "rtl.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Bible-context popup  ("Bible at a Glance" overlay)
// Only opened/rendered from within render_reader.
// ---------------------------------------------------------------------------

static void render_context_popup(AppState* app) {
    struct Sec { int tid; const char* label; int b0; int b1; };
    static constexpr Sec SECS[] = {
        {1, "THE LAW",                  1,  5},
        {1, "HISTORY",                  6, 17},
        {1, "WISDOM LITERATURE",       18, 22},
        {1, "MAJOR PROPHETS",          23, 27},
        {1, "MINOR PROPHETS",          28, 39},
        {2, "GOSPELS",                  1,  4},
        {2, "CHURCH HISTORY",           5,  5},
        {2, "LETTERS",                  6, 26},
        {2, "PROPHECY (APOCALYPTIC)",  27, 27},
    };

    static constexpr ImVec4 C_TSMT = {0.92f, 0.87f, 0.65f, 1.0f};
    static constexpr ImVec4 C_SEC  = {0.80f, 0.67f, 0.38f, 1.0f};
    static constexpr ImVec4 C_BK   = {0.72f, 0.69f, 0.62f, 1.0f};
    static constexpr ImVec4 C_HLBG = {0.28f, 0.50f, 0.20f, 0.65f};
    static constexpr ImVec4 C_HLBK = {0.97f, 0.95f, 0.82f, 1.0f};

    ImGuiIO& io = ImGui::GetIO();
    float pw = 780.0f;
    if (pw > io.DisplaySize.x - 40.0f) pw = io.DisplaySize.x - 40.0f;

    ImGui::SetNextWindowPos(
        {io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
        ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({pw, 80.0f}, {pw, io.DisplaySize.y * 0.90f});

    // Suppress the border — the red outline was ImGui's layout-warning indicator
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    bool open = ImGui::BeginPopupModal("##bctx", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!open) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || app->close_context_popup) {
        app->close_context_popup = false;
        ImGui::CloseCurrentPopup();
    }

    if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);

    // Title + close button
    ImGui::PushStyleColor(ImGuiCol_Text, C_TSMT);
    ImGui::TextUnformatted("Bible at a Glance");
    ImGui::PopStyleColor();
    {
        float cbw     = ImGui::CalcTextSize("close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        float close_x = ImGui::GetWindowWidth() - cbw - ImGui::GetStyle().WindowPadding.x;
        ImGui::SameLine(close_x);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f, 0.25f, 0.18f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.38f, 0.33f, 0.22f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        if (ImGui::SmallButton("close")) ImGui::CloseCurrentPopup();
        ImGui::PopStyleColor(4);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Layout: two equal columns separated by an inner vertical border
    const float pad   = ImGui::GetStyle().WindowPadding.x;
    const float col_w = (pw - pad * 2.0f - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    // Per-section book grid: number of columns and slot width
    int   n_bk = ((int)(col_w / 82.0f) > 0) ? (int)(col_w / 82.0f) : 1;
    if (n_bk > 3) n_bk = 3;
    const float slot = col_w / (float)n_bk;

    // Lambda: render one section using a proper table (no manual cursor jumping)
    auto render_sec = [&](const Sec& sec, int idx) {
        std::vector<const Book*> bks;
        for (auto& b : app->books)
            if (b.testament_id == sec.tid && b.book_id >= sec.b0 && b.book_id <= sec.b1)
                bks.push_back(&b);
        if (bks.empty()) return;

        // Section label
        ImGui::PushStyleColor(ImGuiCol_Text, C_SEC);
        ImGui::TextUnformatted(sec.label);
        ImGui::PopStyleColor();

        // Book grid via BeginTable — avoids all SetCursorPos cursor-boundary warnings
        char tbl_id[16]; snprintf(tbl_id, sizeof(tbl_id), "##bk%d", idx);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4.0f, 2.0f});
        if (ImGui::BeginTable(tbl_id, n_bk, ImGuiTableFlags_None)) {
            for (int c = 0; c < n_bk; ++c)
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, slot);
            for (auto* bk : bks) {
                ImGui::TableNextColumn();
                bool is_cur = (bk->testament_id == app->current_testament_id &&
                               bk->book_id      == app->current_book_id);
                const char* bk_dn = book_display_name(bk->testament_id, bk->book_id);
                if (!bk_dn) bk_dn = bk->short_name;
                if (is_cur) {
                    ImVec2 p  = ImGui::GetCursorScreenPos();
                    float  tw = ImGui::CalcTextSize(bk_dn).x;
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        {p.x - 3.0f, p.y - 1.0f},
                        {p.x + tw + 3.0f, p.y + ImGui::GetTextLineHeight() + 1.0f},
                        ImGui::ColorConvertFloat4ToU32(C_HLBG), 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, C_HLBK);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, C_BK);
                }
                ImGui::TextUnformatted(bk_dn);
                ImGui::PopStyleColor();
                // Click navigates to chapter 1 of this book
                if (ImGui::IsItemClicked()) {
                    app->show_cover = false;
                    app_open_book(app, bk->testament_id, bk->book_id);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::Spacing();
    };

    // Outer two-column layout — also a table
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, {0.35f, 0.30f, 0.18f, 0.8f});
    if (ImGui::BeginTable("##layout", 2,
            ImGuiTableFlags_BordersInnerV,
            {pw - pad * 2.0f, 0.0f})) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col_w);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col_w);

        // --- OLD TESTAMENT ---
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, C_TSMT);
        ImGui::TextUnformatted("OLD TESTAMENT");
        ImGui::PopStyleColor();
        ImGui::Separator();
        for (int i = 0; i < 5; ++i) render_sec(SECS[i], i);

        // --- NEW TESTAMENT ---
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, C_TSMT);
        ImGui::TextUnformatted("NEW TESTAMENT");
        ImGui::PopStyleColor();
        ImGui::Separator();
        for (int i = 5; i < 9; ++i) render_sec(SECS[i], i);

        ImGui::EndTable();
    }
    ImGui::PopStyleColor(); // TableBorderLight

    if (app->fonts.ui) ImGui::PopFont();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Main reading view
// ---------------------------------------------------------------------------

void render_reader(AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL_BG);
    ImGui::BeginChild("##reader", ImVec2(0, 0), false, 0);

    if (app->scroll_to_top) {
        ImGui::SetScrollHereY(0.0f);
        app->scroll_to_top = false;
    }

    // Up/down arrows scroll the body; key-repeat gives smooth feel
    const float line = ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,   true)) ImGui::SetScrollY(ImGui::GetScrollY() - line * 3.0f);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) ImGui::SetScrollY(ImGui::GetScrollY() + line * 3.0f);

    // --- Layout constants -------------------------------------------------
    static constexpr float MAX_COL_W  = 460.0f;  // max text width for multi-column
    static constexpr float MAX_1COL_W = 896.0f;  // max text width for single column
    static constexpr float MIN_COL_W  = 320.0f;  // min text width before reducing columns
    static constexpr float GUTTER     = 48.0f;   // gap between columns
    static constexpr float INNER_PAD  = 32.0f;   // padding inside each column (each side)
    static constexpr float VERSE_GAP  = 0.0f;
    static constexpr float PARA_GAP   = 1.0f;
    static constexpr float BTN_W      = 160.0f;
    static constexpr float BTN_H      = 30.0f;
    static constexpr int   MAX_COLS   = 4;

    float avail_w   = ImGui::GetContentRegionAvail().x;
    float avail_h   = ImGui::GetWindowHeight();
    float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    int   total     = (int)app->verses.size();

    // Font pointers for direct measurement (no push/pop needed)
    ImFont* bfont = app->fonts.body      ? app->fonts.body      : ImGui::GetFont();
    ImFont* vfont = app->fonts.verse_num ? app->fonts.verse_num : ImGui::GetFont();
    float num_col_w = vfont->CalcTextSizeA(vfont->LegacySize, FLT_MAX, 0.0f, "999").x + 2.0f;

    // --- Estimate chrome heights ------------------------------------------
    float hdr_title_h = app->fonts.title      ? app->fonts.title->LegacySize      : 48.0f;
    float hdr_large_h = app->fonts.body_large ? app->fonts.body_large->LegacySize : 24.0f;
    const Book* book  = find_book(app, app->current_testament_id, app->current_book_id);
    // Estimate title height: may wrap to 2 lines if the name is long
    float title_text_w = (app->fonts.title && book)
        ? app->fonts.title->CalcTextSizeA(app->fonts.title->LegacySize, FLT_MAX, 0.0f,
                                          book->long_name).x
        : 0.0f;
    int title_lines = (title_text_w > avail_w * 0.8f) ? 2 : 1;
    // 2 Spacing + book title (possibly 2 lines) + chapter heading + Spacing + separator + 2 Spacing
    float header_h_est = spacing_y * 2.0f
                       + (hdr_title_h * title_lines + spacing_y) + (hdr_large_h + spacing_y)
                       + spacing_y + 1.0f + spacing_y * 2.0f;
    // 2 Spacing + separator + 2 Spacing + button row + Spacing
    float nav_h_est = spacing_y * 4.0f + 1.0f + BTN_H + spacing_y;
    float content_h = avail_h - header_h_est - nav_h_est - spacing_y * 4.0f;
    if (content_h < 80.0f) content_h = 80.0f;

    // --- Choose column count ----------------------------------------------
    // RTL translations always use a single column
    int n_max = (app->single_col_override || app->rtl_layout) ? 1
              : (int)((avail_w + GUTTER) / (MIN_COL_W + INNER_PAD * 2.0f + GUTTER));
    if (n_max < 1)        n_max = 1;
    if (n_max > MAX_COLS) n_max = MAX_COLS;

    // Start at 1 column; add columns until all content fits on-screen height.
    int   N         = n_max;
    float col_txt_w = 0.0f;
    float wrap_w    = 0.0f;
    float total_h   = 0.0f;

    for (int n = 1; n <= n_max; ++n) {
        float cw = (avail_w - GUTTER * (float)(n - 1)) / (float)n - INNER_PAD * 2.0f;
        float max_cw = (n == 1) ? MAX_1COL_W : MAX_COL_W;
        if (cw > max_cw) cw = max_cw;
        if (cw < 80.0f)  break;
        float tw = cw - num_col_w - 8.0f;
        if (tw < 50.0f)  break;

        // Measure total verse height with this wrap width
        float tvh = 0.0f;
        for (int i = 0; i < total; ++i) {
            if (app->verses[i].paragraph_start) tvh += PARA_GAP + spacing_y;
            tvh += bfont->CalcTextSizeA(bfont->LegacySize, FLT_MAX, tw,
                                        app->verses[i].text).y + spacing_y;
            tvh += VERSE_GAP + spacing_y;
        }

        col_txt_w = cw;
        wrap_w    = tw;
        total_h   = tvh;
        N         = n;

        if (tvh / (float)n <= content_h) break; // fits — use this column count
    }

    // --- Column geometry --------------------------------------------------
    float block_w  = (float)N * (col_txt_w + INNER_PAD * 2.0f) + GUTTER * (float)(N - 1);
    float margin_l = (avail_w - block_w) * 0.5f;
    if (margin_l < 0.0f) margin_l = 0.0f;

    float col_x[MAX_COLS], col_wrap[MAX_COLS];
    for (int i = 0; i < N; ++i) {
        col_x[i]    = margin_l + (float)i * (col_txt_w + INNER_PAD * 2.0f + GUTTER) + INNER_PAD;
        col_wrap[i] = col_x[i] + col_txt_w;
    }

    float header_x = col_x[0];
    float header_w = col_wrap[N - 1] - col_x[0];

    // --- Header -----------------------------------------------------------
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, COL_HEADER);
    if (app->fonts.title) ImGui::PushFont(app->fonts.title);
    if (book) {
        float tw = ImGui::CalcTextSize(book->long_name).x;
        if (tw <= header_w) {
            float cx = app->rtl_layout
                ? header_x + header_w - tw          // right-align
                : header_x + (header_w - tw) * 0.5f; // centre
            ImGui::SetCursorPosX(cx);
            ImGui::Text("%s", book->long_name);
        } else {
            ImGui::SetCursorPosX(header_x);
            ImGui::PushTextWrapPos(header_x + header_w);
            ImGui::TextWrapped("%s", book->long_name);
            ImGui::PopTextWrapPos();
        }
    }
    if (app->fonts.title) ImGui::PopFont();
    if (app->fonts.body_large) ImGui::PushFont(app->fonts.body_large);
    {
        char ch_label[32];
        snprintf(ch_label, sizeof(ch_label), "Chapter %d", app->current_chapter);
        float cw = ImGui::CalcTextSize(ch_label).x;
        float cx = app->rtl_layout
            ? header_x + header_w - cw
            : header_x + (header_w - cw) * 0.5f;
        ImGui::SetCursorPosX(cx);
        ImGui::Text("%s", ch_label);
    }
    if (app->fonts.body_large) ImGui::PopFont();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    {
        ImVec2 p   = ImGui::GetCursorScreenPos();
        p.x        = ImGui::GetWindowPos().x + header_x;
        ImU32 scol = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Separator));
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + header_w, p.y + 1.0f}, scol);
        ImGui::Dummy({0.0f, 1.0f});
    }
    ImGui::Spacing(); ImGui::Spacing();

    float content_start_y = ImGui::GetCursorPosY();

    // --- Compute verse heights for column splitting -----------------------
    std::vector<float> vhts((size_t)total);
    for (int i = 0; i < total; ++i) {
        float h = 0.0f;
        if (app->verses[i].paragraph_start) h += PARA_GAP + spacing_y;
        h += bfont->CalcTextSizeA(bfont->LegacySize, FLT_MAX, wrap_w,
                                  app->verses[i].text).y + spacing_y;
        h += VERSE_GAP + spacing_y;
        vhts[i] = h;
    }

    // splits[c] = first verse index in column c; splits[N] = total
    int splits[MAX_COLS + 1];
    splits[0] = 0;
    for (int c = 1; c <= N; ++c) splits[c] = total;

    if (N > 1 && total > 1) {
        float target = total_h / (float)N;
        int   prev   = 0;
        for (int c = 1; c < N; ++c) {
            float acc = 0.0f;
            int   sp  = prev;
            for (int i = prev; i < total; ++i) {
                acc += vhts[i];
                if (acc >= target) {
                    sp = i + 1;
                    // Prefer to break at the next nearby paragraph boundary
                    for (int j = sp; j < total && j < sp + 5; ++j) {
                        if (app->verses[j].paragraph_start) { sp = j; break; }
                    }
                    break;
                }
            }
            if (sp <= prev) sp = prev + 1;
            if (sp > total) sp = total;
            splits[c] = sp;
            prev = sp;
        }
    }

    // --- Verse rendering lambda -------------------------------------------
    auto render_col_range = [&](int from, int to, float cx, float wx) {
        const bool rtl = app->rtl_layout;

        // LTR: [num_col | 8px gap | text .............]
        // RTL: [text ............. | 8px gap | num_col]
        float text_x    = rtl ? cx                     : cx + num_col_w + 8.0f;
        float text_wrap = rtl ? wx - num_col_w - 8.0f : wx;
        float num_x_fn  = 0.0f; // computed per-verse below

        ImFont* body_font = (rtl && app->fonts.hebrew) ? app->fonts.hebrew : app->fonts.body;
        if (body_font) ImGui::PushFont(body_font);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_BODY);

        for (int i = from; i < to; ++i) {
            const Verse& v = app->verses[i];
            if (v.paragraph_start) ImGui::Dummy({0.0f, PARA_GAP});

            float verse_start_y = ImGui::GetCursorPosY();

            // --- Dropcap for verse 1 (LTR only, first column only) -------
            bool did_dropcap = false;
            if (!rtl && from == 0 && i == from && v.verse_id == 1 &&
                app->fonts.dropcap && v.text[0] != '\0')
            {
                // Count bytes in first UTF-8 codepoint
                auto utf8_first_len = [](const char* s) -> int {
                    unsigned char c = (unsigned char)*s;
                    if (c < 0x80)           return 1;
                    if ((c & 0xE0) == 0xC0) return 2;
                    if ((c & 0xF0) == 0xE0) return 3;
                    if ((c & 0xF8) == 0xF0) return 4;
                    return 1;
                };
                int byte_len = utf8_first_len(v.text);
                const char* rest_p = v.text + byte_len;
                while (*rest_p == ' ') rest_p++;

                ImFont* dc_font = app->fonts.dropcap;
                ImVec2 dc_glyph_sz = dc_font->CalcTextSizeA(
                    dc_font->LegacySize, FLT_MAX, 0.0f, v.text, v.text + byte_len);

                // Frame padding and dimensions
                const float fp   = 7.0f;
                float dc_w       = dc_glyph_sz.x + fp * 2.0f;
                float dc_h       = dc_glyph_sz.y + fp * 2.0f;
                float dc_frame_x = text_x;
                float dc_frame_y = verse_start_y;

                // Draw decorative frame via draw list
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos();
                float  sy = ImGui::GetScrollY();
                ImVec2 fr0 = {wp.x + dc_frame_x, wp.y + dc_frame_y - sy};
                ImVec2 fr1 = {fr0.x + dc_w,       fr0.y + dc_h};

                // Outer offset for second border ring
                const float outer_off = 4.0f;
                ImVec2 fo0 = {fr0.x - outer_off, fr0.y - outer_off};
                ImVec2 fo1 = {fr1.x + outer_off, fr1.y + outer_off};

                ImU32 col_bg     = ImGui::ColorConvertFloat4ToU32({0.13f, 0.11f, 0.08f, 1.0f});
                ImU32 col_border = ImGui::ColorConvertFloat4ToU32({0.72f, 0.58f, 0.32f, 0.85f});
                ImU32 col_outer  = ImGui::ColorConvertFloat4ToU32({0.72f, 0.58f, 0.32f, 0.28f});
                ImU32 col_letter = ImGui::ColorConvertFloat4ToU32({0.90f, 0.74f, 0.40f, 1.0f});

                // Background fill + borders
                dl->AddRectFilled(fr0, fr1, col_bg, 3.0f);
                dl->AddRect(fr0, fr1, col_border, 3.0f, 0, 1.5f);
                dl->AddRect(fo0, fo1, col_outer,  3.0f, 0, 1.0f);

                // Corner diamonds at the outer border corners
                const float dm = 3.5f;
                auto diamond = [&](float cx2, float cy2) {
                    dl->AddQuadFilled(
                        {cx2,      cy2 - dm},
                        {cx2 + dm, cy2},
                        {cx2,      cy2 + dm},
                        {cx2 - dm, cy2},
                        col_border);
                };
                diamond(fo0.x, fo0.y);
                diamond(fo1.x, fo0.y);
                diamond(fo1.x, fo1.y);
                diamond(fo0.x, fo1.y);

                // Render dropcap letter
                ImGui::PushFont(dc_font);
                ImGui::SetCursorPos({dc_frame_x + fp, dc_frame_y + fp});
                ImGui::PushStyleColor(ImGuiCol_Text, col_letter);
                ImGui::TextUnformatted(v.text, v.text + byte_len);
                ImGui::PopStyleColor();
                ImGui::PopFont();

                // Wrap remaining verse text around the dropcap frame
                const float gap   = 10.0f;
                float right_x     = dc_frame_x + dc_w + gap;
                float narrow_wrap = text_wrap - right_x;
                float full_wrap   = text_wrap - text_x;

                // How many body-font lines span the dropcap frame height?
                float body_line_h = body_font->LegacySize + spacing_y;
                int   dc_lines    = (int)ceilf((dc_h + outer_off) / body_line_h);
                if (dc_lines < 1) dc_lines = 1;

                const char* p  = rest_p;
                int  line_num  = 0;
                bool first_ln  = true;

                // Suppress ItemSpacing.y between lines so spacing matches TextWrapped
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                    {ImGui::GetStyle().ItemSpacing.x, 0.0f});

                while (*p) {
                    bool  narrow = (line_num < dc_lines);
                    float lx     = narrow ? right_x : text_x;
                    float lw     = narrow ? narrow_wrap : full_wrap;
                    if (lw < 20.0f) { p += strlen(p); break; }

                    const char* line_end = body_font->CalcWordWrapPositionA(
                        1.0f, p, p + strlen(p), lw);
                    if (line_end == p) line_end++; // don't stall on a very long word

                    if (first_ln) {
                        ImGui::SetCursorPos({lx, dc_frame_y});
                        first_ln = false;
                    } else {
                        ImGui::SetCursorPosX(lx);
                    }
                    ImGui::TextUnformatted(p, line_end);

                    p = line_end;
                    while (*p == ' ') p++;
                    line_num++;
                }

                ImGui::PopStyleVar(); // ItemSpacing

                // Ensure cursor clears the full dropcap frame (including outer border)
                float below_dc = dc_frame_y + dc_h + outer_off + spacing_y;
                if (ImGui::GetCursorPosY() < below_dc)
                    ImGui::SetCursorPosY(below_dc);

                did_dropcap = true;
            }

            if (!did_dropcap) {
                // Verse number — no wrap pos active so it never wraps
                if (app->fonts.verse_num) ImGui::PushFont(app->fonts.verse_num);
                char nbuf[8]; snprintf(nbuf, sizeof(nbuf), "%d", v.verse_id);
                float nw = ImGui::CalcTextSize(nbuf).x;
                num_x_fn = rtl ? wx - nw : cx + num_col_w - nw;
                float row_y = ImGui::GetCursorPosY();
                ImGui::SetCursorPosX(num_x_fn);
                ImGui::PushStyleColor(ImGuiCol_Text, COL_VERSE_NUM);
                ImGui::TextUnformatted(nbuf);
                ImGui::PopStyleColor();
                if (app->fonts.verse_num) ImGui::PopFont();

                // Verse text
                if (rtl) {
                    // Hebrew word-wrap for LTR renderers (ImGui has no BiDi support):
                    //   1. Split original logical-order text into words.
                    //   2. Word-wrap in logical order so line breaks fall naturally.
                    //   3. For each line, reverse the word order AND reverse chars within
                    //      each word — this gives correct visual (right-to-left) output.
                    float wrap_width = text_wrap - text_x;
                    float space_w    = ImGui::CalcTextSize(" ").x;

                    // Step 1 – collect words in logical order
                    std::vector<std::string> words;
                    for (const char* p = v.text; *p; ) {
                        while (*p == ' ') p++;
                        if (!*p) break;
                        const char* ws = p;
                        while (*p && *p != ' ') p++;
                        words.emplace_back(ws, p);
                    }

                    // Step 2 – find line break indices
                    std::vector<int> line_start;
                    line_start.push_back(0);
                    float cur_w = 0.0f;
                    for (int wi = 0; wi < (int)words.size(); ++wi) {
                        float ww = ImGui::CalcTextSize(words[wi].c_str()).x;
                        if (wi == line_start.back()) {
                            cur_w = ww;
                        } else if (cur_w + space_w + ww > wrap_width) {
                            line_start.push_back(wi);
                            cur_w = ww;
                        } else {
                            cur_w += space_w + ww;
                        }
                    }

                    // Step 3 – render each line reversed, right-aligned
                    bool first_line = true;
                    for (int li = 0; li < (int)line_start.size(); ++li) {
                        int from_w = line_start[li];
                        int to_w   = (li + 1 < (int)line_start.size())
                                     ? line_start[li + 1] : (int)words.size();

                        // Build display string: words reversed, chars reversed per word
                        std::string line_text;
                        for (int wi = to_w - 1; wi >= from_w; --wi) {
                            if (!line_text.empty()) line_text += ' ';
                            line_text += rtl_reverse(words[wi].c_str());
                        }

                        float lw = ImGui::CalcTextSize(line_text.c_str()).x;
                        float lx = text_wrap - lw;
                        if (lx < text_x) lx = text_x;
                        if (first_line) {
                            ImGui::SetCursorPos({lx, row_y});
                            first_line = false;
                        } else {
                            ImGui::SetCursorPosX(lx);
                        }
                        ImGui::TextUnformatted(line_text.c_str());
                    }
                } else {
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(text_x);
                    ImGui::PushTextWrapPos(text_wrap);
                    ImGui::TextWrapped("%s", v.text);
                    ImGui::PopTextWrapPos();
                }
            } // end if (!did_dropcap)

            float verse_end_y = ImGui::GetCursorPosY();

            // Click silently loads the verse into the tray (no highlight, no auto-open)
            {
                ImVec2 wpos = ImGui::GetWindowPos();
                float  sy   = ImGui::GetScrollY();
                ImVec2 rmin = {wpos.x + cx, wpos.y + verse_start_y - sy};
                ImVec2 rmax = {wpos.x + wx, wpos.y + verse_end_y   - sy};
                bool hovered = ImGui::IsMouseHoveringRect(rmin, rmax);
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
                    tray_load_verse(app, app->current_testament_id,
                                    app->current_book_id, app->current_chapter,
                                    v.verse_id);
                }
            }
        }

        ImGui::PopStyleColor();
        if (body_font) ImGui::PopFont();
    };

    // --- Render all columns -----------------------------------------------
    float max_end_y = content_start_y;
    for (int i = 0; i < N; ++i) {
        ImGui::SetCursorPos({col_x[i], content_start_y});
        render_col_range(splits[i], splits[i + 1], col_x[i], col_wrap[i]);
        float ey = ImGui::GetCursorPosY();
        if (ey > max_end_y) max_end_y = ey;
    }
    ImGui::SetCursorPosY(max_end_y);

    // --- Bottom chapter navigation ----------------------------------------
    int  book_idx = current_book_index(app);
    bool can_prev = true;
    bool can_next = (book_idx < (int)app->books.size() - 1 ||
                     app->current_chapter < app->num_chapters);

    float nav_left  = col_x[0];
    float nav_right = col_wrap[N - 1];

    if (can_prev || can_next) {
        ImGui::Spacing(); ImGui::Spacing();
        {
            ImVec2 p   = ImGui::GetCursorScreenPos();
            p.x        = ImGui::GetWindowPos().x + nav_left;
            ImU32 ncol = ImGui::ColorConvertFloat4ToU32({0.28f, 0.25f, 0.18f, 0.6f});
            ImGui::GetWindowDrawList()->AddRectFilled(
                p, {p.x + (nav_right - nav_left), p.y + 1.0f}, ncol);
            ImGui::Dummy({0.0f, 1.0f});
        }
        ImGui::Spacing(); ImGui::Spacing();

        if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);
        float nav_w = nav_right - nav_left;
        float ctx_w = ImGui::CalcTextSize("Context").x
                    + ImGui::GetStyle().FramePadding.x * 2.0f;
        float ctx_x = nav_left + (nav_w - ctx_w) * 0.5f;

        // Previous chapter (left)
        if (can_prev) {
            ImGui::SetCursorPosX(nav_left);
            if (ImGui::Button("< Previous Chapter", {BTN_W, BTN_H})) app_go_prev_chapter(app);
            ImGui::SameLine(ctx_x);
        } else {
            ImGui::SetCursorPosX(ctx_x);
        }

        // Context button (centre) — ghost style
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.22f, 0.16f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.35f, 0.30f, 0.20f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        if (ImGui::Button("Context", {ctx_w, BTN_H})) ImGui::OpenPopup("##bctx");
        ImGui::PopStyleColor(4);

        // Next chapter (right)
        if (can_next) {
            ImGui::SameLine(nav_left + nav_w - BTN_W);
            if (ImGui::Button("Next Chapter >", {BTN_W, BTN_H})) app_go_next_chapter(app);
        }

        if (app->fonts.ui) ImGui::PopFont();
        ImGui::Spacing();
    }

    if (app->request_context_popup) {
        app->request_context_popup = false;
        if (!ImGui::IsPopupOpen("##bctx"))
            ImGui::OpenPopup("##bctx");
        else
            app->close_context_popup = true;
    }
    render_context_popup(app);

    // Scroll padding: minimal when content fits on screen, comfortable when scrolling
    bool fits = (N > 0 && total > 0 && total_h / (float)N <= content_h);
    ImGui::Dummy({0.0f, fits ? spacing_y * 4.0f : avail_h * 0.75f});

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}
