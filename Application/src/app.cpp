#include "app.h"
#include "imgui.h"
#include "book_names.h"
#include <cstdio>

// stb_image — single-header image loader
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#  include <windows.h>
#  include "resources.h"
#endif

// ---------------------------------------------------------------------------
// Colour palette  (ABGR in ImGui = 0xAABBGGRR)
// ---------------------------------------------------------------------------
static constexpr ImVec4 COL_BG_DARK      = {0.10f, 0.09f, 0.08f, 1.0f};
static constexpr ImVec4 COL_SIDEBAR_BG   = {0.13f, 0.12f, 0.11f, 1.0f};
static constexpr ImVec4 COL_PANEL_BG     = {0.15f, 0.14f, 0.12f, 1.0f};
static constexpr ImVec4 COL_TEXT_BODY    = {0.90f, 0.87f, 0.80f, 1.0f};
static constexpr ImVec4 COL_TEXT_DIM     = {0.55f, 0.52f, 0.45f, 1.0f};
static constexpr ImVec4 COL_VERSE_NUM    = {0.72f, 0.58f, 0.32f, 1.0f}; // warm gold
static constexpr ImVec4 COL_HEADER      = {0.85f, 0.75f, 0.55f, 1.0f}; // book/chapter title
static constexpr ImVec4 COL_SELECTED    = {0.30f, 0.25f, 0.15f, 1.0f}; // sidebar selection

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static const Book* find_book(const AppState* app, int testament_id, int book_id) {
    for (auto& b : app->books)
        if (b.testament_id == testament_id && b.book_id == book_id) return &b;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Cover image loading
// ---------------------------------------------------------------------------

static void load_cover_texture(AppState* app) {
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = nullptr;

    // Try loading from disk first (assets/images/Cover.jpg relative to cwd/exe)
    const char* paths[] = {
        "assets/images/Cover.jpg",
        "../assets/images/Cover.jpg",
    };
    for (const char* p : paths) {
        pixels = stbi_load(p, &w, &h, &ch, 4);
        if (pixels) break;
    }

#if defined(_WIN32)
    // Fall back to embedded resource
    if (!pixels) {
        HRSRC   hres  = FindResource(nullptr, MAKEINTRESOURCE(RES_COVER_IMAGE), RT_RCDATA);
        HGLOBAL hglob = hres ? LoadResource(nullptr, hres) : nullptr;
        const void* src = hglob ? LockResource(hglob) : nullptr;
        DWORD       sz  = hres  ? SizeofResource(nullptr, hres) : 0;
        if (src && sz)
            pixels = stbi_load_from_memory(
                (const unsigned char*)src, (int)sz, &w, &h, &ch, 4);
    }
#endif

    if (!pixels) return;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    app->cover_tex   = tex;
    app->cover_tex_w = w;
    app->cover_tex_h = h;
}

// ---------------------------------------------------------------------------
// Translation discovery / init / shutdown
// ---------------------------------------------------------------------------

static void init_translations(AppState* app, const char* active_db_path) {
    static const struct { const char* name; const char* file; int res_id; } ALL[] = {
        {"KJV",                 "KJV.db",         RES_DB_KJV        },
        {"Septuagint",          "Septuagint.db",   RES_DB_SEPTUAGINT },
        {"Vulgate",             "Vulgate.db",      RES_DB_VULGATE    },
        {"Greek New Testament", "UGNT.db",         RES_DB_UGNT       },
        {"Tanakh",              "Tanakh.db",       RES_DB_TANAKH     },
        {"Apocrypha",           "Apocrypha.db",    RES_DB_APOCRYPHA  },
    };

    // Extract directory from the active db path so siblings resolve correctly
    char dir[512] = "";
    const char* ls = strrchr(active_db_path, '/');
    const char* lb = strrchr(active_db_path, '\\');
    const char* sep = (ls > lb) ? ls : lb;
    if (sep) {
        size_t dlen = (size_t)(sep - active_db_path) + 1;
        if (dlen < sizeof(dir)) { memcpy(dir, active_db_path, dlen); dir[dlen] = '\0'; }
    }

    app->translations.clear();
    app->current_translation = 0;
    for (auto& k : ALL) {
        Translation t;
        snprintf(t.name, sizeof(t.name), "%s", k.name);
        snprintf(t.path, sizeof(t.path), "%s%s", dir, k.file);
#ifdef BR_SHIP
        t.resource_id = k.res_id;
#else
        t.resource_id = 0;
#endif
        app->translations.push_back(t);
    }
}

// Open a translation DB — uses embedded resource in ship builds, file path otherwise.
static BibleDB* open_translation_db(const Translation& t) {
    if (t.resource_id > 0) {
        BibleDB* db = bible_db_open_from_resource(t.resource_id);
        if (db) return db;
    }
    return bible_db_open(t.path);
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

static bool translation_has_cover(const AppState* app) {
    if (app->current_translation < 0 ||
        app->current_translation >= (int)app->translations.size()) return false;
    return strcmp(app->translations[app->current_translation].name, "KJV") == 0;
}

static bool translation_is_rtl(const AppState* app) {
    if (app->current_translation < 0 ||
        app->current_translation >= (int)app->translations.size()) return false;
    return strcmp(app->translations[app->current_translation].name, "Tanakh") == 0;
}

static bool name_is_rtl(const char* name) {
    return strcmp(name, "Tanakh") == 0;
}

bool app_init(AppState* app, const char* db_path) {
    init_translations(app, db_path);

    // Open the primary (KJV) database
    if (!app->translations.empty())
        app->db = open_translation_db(app->translations[0]);
    else
        app->db = bible_db_open(db_path);
    if (!app->db) return false;

    app->books.resize(200);
    int n = bible_db_get_books(app->db, app->books.data(), 200);
    app->books.resize(n);

    load_cover_texture(app);

    if (!translation_has_cover(app) && !app->books.empty()) {
        app->show_cover = false;
        app_open_book(app, app->books[0].testament_id, app->books[0].book_id);
    }
    // else: cover is shown until the user picks a book (KJV default)
    return true;
}


void app_shutdown(AppState* app) {
    if (app->cover_tex) { glDeleteTextures(1, &app->cover_tex); app->cover_tex = 0; }
    bible_db_close(app->db);
    app->db = nullptr;
}

void app_switch_translation(AppState* app, int idx) {
    if (idx < 0 || idx >= (int)app->translations.size()) return;
    if (idx == app->current_translation) return;

    BibleDB* next = open_translation_db(app->translations[idx]);
    if (!next) return; // leave current db intact if open fails

    bible_db_close(app->db);
    app->db = next;
    app->current_translation = idx;

    app->books.resize(200);
    int n = bible_db_get_books(app->db, app->books.data(), 200);
    app->books.resize(n);

    app->verses.clear();
    if (translation_has_cover(app)) {
        app->show_cover = true;
    } else {
        app->show_cover = false;
        if (!app->books.empty())
            app_open_book(app, app->books[0].testament_id, app->books[0].book_id);
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void app_load_chapter(AppState* app, int testament_id, int book_id, int chapter) {
    app->current_testament_id = testament_id;
    app->current_book_id      = book_id;
    app->current_chapter      = chapter;
    app->first_chapter  = bible_db_get_first_chapter(app->db, testament_id, book_id);
    app->num_chapters   = bible_db_get_num_chapters(app->db, testament_id, book_id);
    app->rtl_layout     = translation_is_rtl(app);

    app->verses.resize(200);
    int n = bible_db_get_chapter(app->db, testament_id, book_id, chapter,
                                  app->verses.data(), 200);
    app->verses.resize(n > 0 ? n : 0);
    app->scroll_to_top = true;
}

void app_open_book(AppState* app, int testament_id, int book_id) {
    int first = bible_db_get_first_chapter(app->db, testament_id, book_id);
    app_load_chapter(app, testament_id, book_id, first > 0 ? first : 1);
}

// Find the index of the current book in app->books
static int current_book_index(const AppState* app) {
    for (int i = 0; i < (int)app->books.size(); ++i) {
        const Book& b = app->books[i];
        if (b.testament_id == app->current_testament_id &&
            b.book_id      == app->current_book_id)
            return i;
    }
    return 0;
}

void app_go_prev_chapter(AppState* app) {
    if (app->show_cover) return; // already at the beginning
    if (app->current_chapter > app->first_chapter) {
        app_load_chapter(app, app->current_testament_id,
                         app->current_book_id, app->current_chapter - 1);
    } else {
        int idx = current_book_index(app);
        if (idx <= 0) {
            // At the first chapter of the first book — go back to cover
            app->show_cover = true;
            return;
        }
        const Book& prev = app->books[idx - 1];
        int prev_last = bible_db_get_num_chapters(app->db,
                            prev.testament_id, prev.book_id);
        if (prev_last > 0)
            app_load_chapter(app, prev.testament_id, prev.book_id, prev_last);
    }
}

void app_go_next_chapter(AppState* app) {
    if (app->show_cover) {
        app->show_cover = false;
        if (!app->books.empty())
            app_open_book(app, app->books[0].testament_id, app->books[0].book_id);
        return;
    }
    if (app->current_chapter < app->num_chapters) {
        app_load_chapter(app, app->current_testament_id,
                         app->current_book_id, app->current_chapter + 1);
    } else {
        int idx = current_book_index(app);
        if (idx + 1 >= (int)app->books.size()) return;
        const Book& next = app->books[idx + 1];
        app_open_book(app, next.testament_id, next.book_id);
    }
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------

static void push_body_style(const AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_BODY);
    if (app->fonts.body) ImGui::PushFont(app->fonts.body);
}

static void pop_body_style(const AppState* app) {
    if (app->fonts.body) ImGui::PopFont();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Sidebar: book list
// ---------------------------------------------------------------------------

static void render_sidebar(AppState* app) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_SIDEBAR_BG);
    ImGui::BeginChild("##sidebar", ImVec2(app->sidebar_width, 0), false, 0);

    ImGui::Spacing();
    ImGui::Spacing();

    if (app->fonts.ui) ImGui::PushFont(app->fonts.ui);

    // More breathing room between items
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {8.0f, 5.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  {6.0f, 4.0f});

    // Translation selector
    if (app->translations.size() > 1) {
        ImGui::SetCursorPosX(8.0f);
        ImGui::PushItemWidth(app->sidebar_width - 16.0f);
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
        ImGui::Spacing();
        ImGui::SetCursorPosX(8.0f);
        ImGui::Separator();
        ImGui::Spacing();
    }

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

// ---------------------------------------------------------------------------
// Cover splash
// ---------------------------------------------------------------------------

static void render_cover(AppState* app) {
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

// ---------------------------------------------------------------------------
// Bible-context popup  ("Bible at a Glance" overlay)
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
        float cbw   = ImGui::CalcTextSize("close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
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
    int   n_bk  = ((int)(col_w / 82.0f) > 0) ? (int)(col_w / 82.0f) : 1;
    if (n_bk > 3) n_bk = 3;
    const float slot  = col_w / (float)n_bk;

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

    // Outer two-column layout — also a table, so no cursor jumping at this level either
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

static constexpr float TOOLBAR_H = 44.0f;

// ---------------------------------------------------------------------------
// Parallel-verse tray helpers
// ---------------------------------------------------------------------------

static void tray_load_verse(AppState* app,
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

static constexpr float TRAY_W = 340.0f;

// render_tray is called INSIDE the root Begin/End so the child always draws on top
// of the reader child (later submission = higher draw order within the same parent).
static void render_tray(AppState* app) {
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

// ---------------------------------------------------------------------------
// Reader: chapter heading + verses
// ---------------------------------------------------------------------------

static void render_reader(AppState* app) {
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
    float hdr_title_h  = app->fonts.title      ? app->fonts.title->LegacySize      : 48.0f;
    float hdr_large_h  = app->fonts.body_large ? app->fonts.body_large->LegacySize : 24.0f;
    const Book* book   = find_book(app, app->current_testament_id, app->current_book_id);
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
    float nav_h_est  = spacing_y * 4.0f + 1.0f + BTN_H + spacing_y;
    float content_h  = avail_h - header_h_est - nav_h_est - spacing_y * 4.0f;
    if (content_h < 80.0f) content_h = 80.0f;

    // --- Choose column count ----------------------------------------------
    // Maximum columns that fit given the minimum column width
    // RTL translations always use a single column
    int n_max = (app->single_col_override || app->rtl_layout) ? 1
              : (int)((avail_w + GUTTER) / (MIN_COL_W + INNER_PAD * 2.0f + GUTTER));
    if (n_max < 1)        n_max = 1;
    if (n_max > MAX_COLS) n_max = MAX_COLS;

    // Start at 1 column; add columns until all content fits on-screen height.
    // If nothing fits even at n_max, stay at n_max for minimal scrolling.
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

    // --- Compute verse splits ---------------------------------------------
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
    // Default: all verses in column 0, remaining columns empty.
    // This handles total==0 and total==1 without leaving splits uninitialized.
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
        float text_x    = rtl ? cx                       : cx + num_col_w + 8.0f;
        float text_wrap = rtl ? wx - num_col_w - 8.0f   : wx;
        float num_x_fn  = 0.0f; // computed per-verse below

        ImFont* body_font = (rtl && app->fonts.hebrew) ? app->fonts.hebrew : app->fonts.body;
        if (body_font) ImGui::PushFont(body_font);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_BODY);

        for (int i = from; i < to; ++i) {
            const Verse& v = app->verses[i];
            if (v.paragraph_start) ImGui::Dummy({0.0f, PARA_GAP});

            float verse_start_y = ImGui::GetCursorPosY();

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
                // Manual word-wrap with per-line right-alignment (ImGui has no BiDi support)
                float wrap_width = text_wrap - text_x;
                const char* p = v.text;
                std::string cur_line;
                float cur_w = 0.0f;
                bool first_line = true;

                auto flush_rtl_line = [&](const std::string& line) {
                    float lw = ImGui::CalcTextSize(line.c_str()).x;
                    float lx = text_wrap - lw;
                    if (lx < text_x) lx = text_x;
                    if (first_line) {
                        ImGui::SetCursorPos({lx, row_y});
                        first_line = false;
                    } else {
                        ImGui::SetCursorPosX(lx);
                    }
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
                    if (!cur_line.empty() && cur_w + sw + ww > wrap_width) {
                        flush_rtl_line(cur_line);
                        cur_line = word;
                        cur_w = ww;
                    } else {
                        if (!cur_line.empty()) { cur_line += ' '; cur_w += sw; }
                        cur_line += word;
                        cur_w += ww;
                    }
                }
                if (!cur_line.empty()) flush_rtl_line(cur_line);
            } else {
                ImGui::SameLine();
                ImGui::SetCursorPosX(text_x);
                ImGui::PushTextWrapPos(text_wrap);
                ImGui::TextWrapped("%s", v.text);
                ImGui::PopTextWrapPos();
            }

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
        float nav_w   = nav_right - nav_left;
        float ctx_w   = ImGui::CalcTextSize("Context").x
                      + ImGui::GetStyle().FramePadding.x * 2.0f;
        float ctx_x   = nav_left + (nav_w - ctx_w) * 0.5f;

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

// ---------------------------------------------------------------------------
// Title bar / toolbar  (borderless drag zone + navigation + window controls)
// ---------------------------------------------------------------------------

static void render_toolbar(AppState* app) {
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

    if (app->show_cover) {
        // Cover is showing — display a dim title, no chapter navigation
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_DIM);
        ImGui::TextUnformatted("The Holy Bible");
        ImGui::PopStyleColor();
    } else {
        // Chapter nav — centred in the toolbar
        const Book* book = find_book(app, app->current_testament_id, app->current_book_id);
        float frame_h  = ImGui::GetFrameHeight();
        float ch_btn_w = ImGui::CalcTextSize("Ch 999 / 999").x
                       + ImGui::GetStyle().FramePadding.x * 2.0f;
        float nav_w    = frame_h + 6.0f + ch_btn_w + 6.0f + frame_h;
        float nav_x    = (ImGui::GetWindowWidth() - nav_w) * 0.5f;

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
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left))  app_go_prev_chapter(app);
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

    // Shared font-size helper — clamps and sets rebuild flag
    auto set_font_size = [&](float size) {
        if (size < 10.0f || size > 32.0f) return;
        app->font_size = size;
        app->request_font_rebuild = true;
    };

    // Text size controls (A- / A+), column toggle, and tray toggle — pinned to right edge
    {
        const ImGuiStyle& st   = ImGui::GetStyle();
        float frame_h  = ImGui::GetFrameHeight();
        float btn_w    = ImGui::CalcTextSize("A+").x + st.FramePadding.x * 2.0f;
        float col_w    = ImGui::CalcTextSize("||").x + st.FramePadding.x * 2.0f;
        // tray arrow button width + gap before it
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
    if (ImGui::IsKeyPressed(ImGuiKey_Tab,        false)) app->sidebar_visible = !app->sidebar_visible;
    if (ImGui::IsKeyPressed(ImGuiKey_Backslash,  false)) app->tray_open = !app->tray_open;
    if (ImGui::IsKeyPressed(ImGuiKey_F11,        false)) app->request_fullscreen_toggle = true;
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
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            !ImGui::IsPopupOpen("##shortcuts") &&
            !ImGui::IsPopupOpen("##bctx") &&
            !ImGui::IsPopupOpen("##chpicker"))
        ImGui::OpenPopup("##shortcuts");

    if (app->fonts.ui) ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg
}

// ---------------------------------------------------------------------------
// Keyboard shortcuts popup
// ---------------------------------------------------------------------------

static void render_shortcuts_popup(AppState* app) {
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
    row("Left / Right",      "Previous / next chapter");
    row("Page Up / Page Down","Previous / next chapter");
    row("Up / Down",         "Scroll");

    section("VIEW");
    row("Tab",               "Toggle book sidebar");
    row("\\",                "Toggle parallel-verse tray");
    row("F11",               "Toggle fullscreen");

    section("TEXT SIZE");
    row("Ctrl  +  /  -",     "Increase / decrease font size");
    row("Ctrl  0",           "Reset font size");
    row("Numpad  +  /  -",   "Increase / decrease font size");
    row("A+  /  A-  (toolbar)","Increase / decrease font size");

    section("OTHER");
    row("Esc",               "Show this help / close popups");

    ImGui::Spacing();
    if (app->fonts.ui) ImGui::PopFont();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Main render entry point
// ---------------------------------------------------------------------------

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

    // Below toolbar: [sidebar |] reader
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

    render_shortcuts_popup(app);

    ImGui::End();
}
