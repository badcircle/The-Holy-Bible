#pragma once
#include "app.h"
#include "bible_db.h"

// ---------------------------------------------------------------------------
// Shared constants
// ---------------------------------------------------------------------------

static constexpr float TOOLBAR_H = 44.0f;

// ---------------------------------------------------------------------------
// Internal helpers — defined in app_init.cpp / render_tray.cpp
// ---------------------------------------------------------------------------

const Book*  find_book(const AppState* app, int testament_id, int book_id);
int          current_book_index(const AppState* app);
bool         name_is_rtl(const char* name);
BibleDB*     open_translation_db(const Translation& t);
void         tray_load_verse(AppState* app, int testament_id, int book_id,
                             int chapter, int verse_id);

// ---------------------------------------------------------------------------
// Render functions — defined in their respective render_*.cpp files,
// all called from app_render.cpp (and render_reader for context popup).
// ---------------------------------------------------------------------------

void render_toolbar(AppState* app);
void render_sidebar(AppState* app);
void render_cover(AppState* app);
void render_reader(AppState* app);
void render_tray(AppState* app);
void render_shortcuts_popup(AppState* app);
