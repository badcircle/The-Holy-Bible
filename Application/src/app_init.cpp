//
// app_init.cpp
// App initialisation, shutdown, translation management, and navigation.
//
#include "app_internal.h"
#include "imgui.h"
#include "book_names.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// stb_image — single-header image loader (implementation compiled here only)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#  include <windows.h>
#  include <SDL2/SDL_opengl.h>
#  include "resources.h"
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

const Book* find_book(const AppState* app, int testament_id, int book_id) {
    for (auto& b : app->books)
        if (b.testament_id == testament_id && b.book_id == book_id) return &b;
    return nullptr;
}

bool name_is_rtl(const char* name) {
    return strcmp(name, "Tanakh") == 0;
}

BibleDB* open_translation_db(const Translation& t) {
    if (t.resource_id > 0) {
        BibleDB* db = bible_db_open_from_resource(t.resource_id);
        if (db) return db;
    }
    return bible_db_open(t.path);
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
// Translation discovery
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
    const char* ls  = strrchr(active_db_path, '/');
    const char* lb  = strrchr(active_db_path, '\\');
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

static bool translation_has_cover(const AppState* app) {
    if (app->current_translation < 0 ||
        app->current_translation >= (int)app->translations.size()) return false;
    return strcmp(app->translations[app->current_translation].name, "KJV") == 0;
}

static bool translation_is_rtl(const AppState* app) {
    if (app->current_translation < 0 ||
        app->current_translation >= (int)app->translations.size()) return false;
    return name_is_rtl(app->translations[app->current_translation].name);
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

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

    // Remember position so we can return to the same book/chapter if it exists
    int  saved_testament = app->current_testament_id;
    int  saved_book      = app->current_book_id;
    int  saved_chapter   = app->current_chapter;
    bool was_cover       = app->show_cover;

    BibleDB* next = open_translation_db(app->translations[idx]);
    if (!next) return; // leave current db intact if open fails

    bible_db_close(app->db);
    app->db = next;
    app->current_translation = idx;

    app->books.resize(200);
    int n = bible_db_get_books(app->db, app->books.data(), 200);
    app->books.resize(n);

    app->verses.clear();

    // Try to stay at the same book/chapter if we weren't on the cover
    bool found = false;
    if (!was_cover && !app->books.empty()) {
        for (auto& b : app->books) {
            if (b.testament_id == saved_testament && b.book_id == saved_book) {
                int first = bible_db_get_first_chapter(app->db, saved_testament, saved_book);
                int num   = bible_db_get_num_chapters(app->db, saved_testament, saved_book);
                int ch    = saved_chapter;
                if (ch < first) ch = first;
                if (num > 0 && ch > num) ch = num;
                app->show_cover = false;
                app_load_chapter(app, saved_testament, saved_book, ch);
                found = true;
                break;
            }
        }
    }

    if (!found) {
        if (translation_has_cover(app)) {
            app->show_cover = true;
        } else if (!app->books.empty()) {
            app->show_cover = false;
            app_open_book(app, app->books[0].testament_id, app->books[0].book_id);
        }
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

int current_book_index(const AppState* app) {
    for (int i = 0; i < (int)app->books.size(); ++i) {
        const Book& b = app->books[i];
        if (b.testament_id == app->current_testament_id &&
            b.book_id      == app->current_book_id)
            return i;
    }
    return 0;
}

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
