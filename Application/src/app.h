#pragma once
#include <vector>
#include "imgui.h"
#include "bible_db.h"
#include <SDL2/SDL_opengl.h>

// A discovered / available Bible translation
struct Translation {
    char name[48];
    char path[260];
    int  resource_id = 0; // >0: Windows RCDATA resource ID (BR_SHIP build); 0: load from path
};

// One entry in the parallel-verse tray (one translation's text for the selected verse)
struct TrayEntry {
    char translation[48];
    char text[4096];
};

// Font handles (set up in main.cpp after ImGui context is created)
struct AppFonts {
    ImFont* body        = nullptr;  // Main reading font (serif)
    ImFont* body_large  = nullptr;  // Chapter heading (Pirata One, ~heading size)
    ImFont* title       = nullptr;  // Book title (Pirata One, 2× heading size)
    ImFont* verse_num   = nullptr;  // Small numbered superscript-style
    ImFont* ui          = nullptr;  // Sans-serif for buttons/labels
    ImFont* hebrew      = nullptr;  // Noto Serif Hebrew (RTL translations)
};

struct AppState {
    BibleDB* db = nullptr;

    // Available translations + active selection
    std::vector<Translation> translations;
    int  current_translation  = 0;

    // Loaded data
    std::vector<Book>  books;
    std::vector<Verse> verses;      // Current chapter verses

    // Selection (BookID restarts at 1 per testament, so both fields are needed)
    int  current_testament_id = 1;
    int  current_book_id      = 1;
    int  current_chapter      = 1;
    int  first_chapter        = 1;  // First available chapter in current book
    int  num_chapters         = 0;  // Last available chapter in current book

    // UI state
    float  sidebar_width   = 220.0f;
    bool   sidebar_visible = true;
    int    highlight_verse = 0;     // 0 = none
    bool   scroll_to_top   = false;

    // Window management (written by app_render, consumed by main loop)
    ImVec2 window_drag_delta          = {0.0f, 0.0f};
    bool   request_close              = false;
    bool   request_minimize           = false;
    bool   request_fullscreen_toggle  = false;

    // Font size (A-/A+ buttons or Ctrl+/-/0; consumed by main loop)
    float  font_size            = 20.0f;
    bool   request_font_rebuild = false;

    // Column layout
    bool   single_col_override  = false; // when true, force single column regardless of width
    bool   rtl_layout           = false; // when true, right-to-left text (Hebrew etc.)

    // Popup requests (set anywhere, consumed by render_reader / render_context_popup)
    bool   request_context_popup  = false;
    bool   close_context_popup    = false;
    bool   request_shortcuts_popup = false;

    // Parallel-verse tray (slides in from the right)
    bool   tray_open      = false;
    float  tray_anim      = 0.0f;  // 0 = fully hidden, 1 = fully open
    int    tray_verse_id  = 0;     // verse number of the selected verse (0 = none)
    int    tray_testament = 0;
    int    tray_book_id   = 0;
    int    tray_chapter   = 0;
    std::vector<TrayEntry> tray_entries;

    // Cover image (shown until first book is selected)
    bool   show_cover    = true;
    GLuint cover_tex     = 0;    // OpenGL texture ID (0 = not loaded)
    int    cover_tex_w   = 0;
    int    cover_tex_h   = 0;

    // Fonts (populated by main.cpp)
    AppFonts fonts;
};

// Called once after ImGui context + fonts are ready
bool app_init(AppState* app, const char* db_path);
void app_shutdown(AppState* app);

// Call each frame inside the ImGui frame
void app_render(AppState* app);

// Navigation helpers
void app_load_chapter(AppState* app, int testament_id, int book_id, int chapter);
void app_open_book(AppState* app, int testament_id, int book_id); // opens at first available chapter
void app_go_prev_chapter(AppState* app);
void app_go_next_chapter(AppState* app);

// Translation switching
void app_switch_translation(AppState* app, int index);
