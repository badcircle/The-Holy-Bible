//
// BibleReader — main.cpp
// SDL2 + OpenGL 3.3 Core + ImGui (FreeType) entry point
//

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "imgui_freetype.h"

#include "app.h"

// stb_image — declaration only; implementation is compiled in app.cpp
#include "stb_image.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#  include <dwmapi.h>
#  include <SDL2/SDL_syswm.h>
#  include "resources.h"
#endif

// ---------------------------------------------------------------------------
// Config — override at compile time or at runtime via env vars
// ---------------------------------------------------------------------------
#ifndef BIBLE_DB_PATH
#  define BIBLE_DB_PATH "KJV.db"
#endif

static constexpr int   WINDOW_W           = 1280;
static constexpr int   WINDOW_H           = 800;
static constexpr float FONT_BODY_SIZE     = 20.0f;
static constexpr float FONT_HEADING_SIZE  = 24.0f;
static constexpr float FONT_VERSE_SIZE    = 13.0f;
static constexpr float FONT_UI_SIZE       = 14.0f;

// ---------------------------------------------------------------------------
// Embedded-resource font loader (Windows only)
// ---------------------------------------------------------------------------

#ifdef _WIN32
static ImFont* load_font_resource(ImGuiIO& io, int resource_id, float size,
                                   const ImFontConfig* cfg, const ImWchar* ranges)
{
    HRSRC   hres  = FindResource(nullptr, MAKEINTRESOURCE(resource_id), RT_RCDATA);
    if (!hres)  return nullptr;
    HGLOBAL hglob = LoadResource(nullptr, hres);
    if (!hglob) return nullptr;
    const void* src = LockResource(hglob);
    DWORD       sz  = SizeofResource(nullptr, hres);
    if (!src || !sz) return nullptr;
    // ImGui takes ownership of this buffer and will IM_FREE it on atlas clear.
    void* data = IM_ALLOC(sz);
    memcpy(data, src, sz);
    return io.Fonts->AddFontFromMemoryTTF(data, (int)sz, size, cfg, ranges);
}
#endif

// ---------------------------------------------------------------------------
// Font loading helpers
// ---------------------------------------------------------------------------

// Try to load from rel_path (relative to cwd/exe), then assets/fonts/ prefix,
// then fall back to the embedded Windows resource (if resource_id > 0).
static ImFont* load_font(ImGuiIO& io, const char* rel_path,
                          float size, const ImFontConfig* cfg = nullptr,
                          const ImWchar* ranges = nullptr,
                          int resource_id = 0)
{
    // Relative to exe / cwd
    ImFont* f = io.Fonts->AddFontFromFileTTF(rel_path, size, cfg, ranges);
    if (f) return f;

    // Try assets/fonts/ prefix
    std::string p = std::string("assets/fonts/") + rel_path;
    f = io.Fonts->AddFontFromFileTTF(p.c_str(), size, cfg, ranges);
    if (f) return f;

    // Fall back to embedded resource
#ifdef _WIN32
    if (resource_id > 0)
        return load_font_resource(io, resource_id, size, cfg, ranges);
#endif
    return nullptr;
}

// ---------------------------------------------------------------------------
// Build the font atlas
// ---------------------------------------------------------------------------
static void setup_fonts(AppFonts& fonts, float body_size = FONT_BODY_SIZE) {
    ImGuiIO& io = ImGui::GetIO();

    // Scale all sizes proportionally to the body size
    const float heading_size = body_size * (FONT_HEADING_SIZE / FONT_BODY_SIZE);
    const float verse_size   = body_size * (FONT_VERSE_SIZE   / FONT_BODY_SIZE);
    const float ui_size      = body_size * (FONT_UI_SIZE      / FONT_BODY_SIZE);

    // FreeType quality hints
    ImFontConfig cfg_serif;
    cfg_serif.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint |
                                ImGuiFreeTypeBuilderFlags_LightHinting;

    ImFontConfig cfg_sans;
    cfg_sans.FontLoaderFlags  = ImGuiFreeTypeBuilderFlags_ForceAutoHint;

    // ------------------------------------------------------------------
    // Body text — Gentium Regular
    // ------------------------------------------------------------------
    fonts.body = load_font(io,
        "Gentium-7.000/Gentium-Regular.ttf", body_size, &cfg_serif,
        nullptr, RES_FONT_BODY);
    if (!fonts.body)
        fonts.body = load_font(io,
            "Crimson_Pro/static/CrimsonPro-Regular.ttf", body_size, &cfg_serif,
            nullptr, RES_FONT_VERSE_NUM);

    // ------------------------------------------------------------------
    // Chapter heading — Pirata One (heading size)
    // ------------------------------------------------------------------
    fonts.body_large = load_font(io,
        "Pirata_One/PirataOne-Regular.ttf", heading_size, &cfg_serif,
        nullptr, RES_FONT_HEADING);

    // ------------------------------------------------------------------
    // Book title — Pirata One (2× heading size)
    // ------------------------------------------------------------------
    fonts.title = load_font(io,
        "Pirata_One/PirataOne-Regular.ttf", heading_size * 2.0f, &cfg_serif,
        nullptr, RES_FONT_HEADING);

    // ------------------------------------------------------------------
    // Verse numbers — Crimson Pro (lighter weight, smaller)
    // ------------------------------------------------------------------
    fonts.verse_num = load_font(io,
        "Crimson_Pro/static/CrimsonPro-Regular.ttf", verse_size, &cfg_serif,
        nullptr, RES_FONT_VERSE_NUM);
    if (!fonts.verse_num)
        fonts.verse_num = load_font(io,
            "Gentium-7.000/Gentium-Regular.ttf", verse_size, &cfg_serif,
            nullptr, RES_FONT_BODY);

    // ------------------------------------------------------------------
    // UI chrome — Roboto (fixed size; toolbar stays legible at any zoom)
    // ------------------------------------------------------------------
    fonts.ui = load_font(io,
        "Roboto/static/Roboto-Regular.ttf", ui_size, &cfg_sans,
        nullptr, RES_FONT_UI);

    // ------------------------------------------------------------------
    // Hebrew body text — Noto Serif Hebrew
    // ------------------------------------------------------------------
    fonts.hebrew = load_font(io,
        "Noto_Serif_Hebrew/static/NotoSerifHebrew-Regular.ttf", body_size, &cfg_serif,
        nullptr, RES_FONT_HEBREW);

    // Always ensure a valid default
    io.FontDefault = io.Fonts->AddFontDefault();

    if (!fonts.body)       fonts.body       = io.FontDefault;
    if (!fonts.body_large) fonts.body_large = fonts.body;
    if (!fonts.title)      fonts.title      = fonts.body_large;
    if (!fonts.verse_num)  fonts.verse_num  = io.FontDefault;
    if (!fonts.ui)         fonts.ui         = io.FontDefault;
    if (!fonts.hebrew)     fonts.hebrew     = fonts.body;
}

// ---------------------------------------------------------------------------
// Style
// ---------------------------------------------------------------------------

static void setup_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding      = 4.0f;
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = {8.0f, 6.0f};
    s.ScrollbarSize     = 12.0f;

    ImVec4* c = s.Colors;
    // Dark parchment palette
    c[ImGuiCol_WindowBg]          = {0.12f, 0.11f, 0.10f, 1.00f};
    c[ImGuiCol_ChildBg]           = {0.15f, 0.14f, 0.12f, 1.00f};
    c[ImGuiCol_PopupBg]           = {0.13f, 0.12f, 0.10f, 0.97f};
    c[ImGuiCol_Text]              = {0.90f, 0.87f, 0.80f, 1.00f};
    c[ImGuiCol_TextDisabled]      = {0.50f, 0.47f, 0.40f, 1.00f};
    c[ImGuiCol_Header]            = {0.28f, 0.24f, 0.16f, 1.00f};
    c[ImGuiCol_HeaderHovered]     = {0.36f, 0.30f, 0.18f, 1.00f};
    c[ImGuiCol_HeaderActive]      = {0.45f, 0.38f, 0.22f, 1.00f};
    c[ImGuiCol_Button]            = {0.28f, 0.24f, 0.16f, 1.00f};
    c[ImGuiCol_ButtonHovered]     = {0.40f, 0.34f, 0.20f, 1.00f};
    c[ImGuiCol_ButtonActive]      = {0.50f, 0.43f, 0.25f, 1.00f};
    c[ImGuiCol_FrameBg]           = {0.20f, 0.18f, 0.14f, 1.00f};
    c[ImGuiCol_FrameBgHovered]    = {0.28f, 0.25f, 0.18f, 1.00f};
    c[ImGuiCol_ScrollbarBg]       = {0.10f, 0.09f, 0.08f, 1.00f};
    c[ImGuiCol_ScrollbarGrab]     = {0.38f, 0.32f, 0.20f, 1.00f};
    c[ImGuiCol_ScrollbarGrabHovered]= {0.50f, 0.43f, 0.28f, 1.00f};
    c[ImGuiCol_Separator]         = {0.30f, 0.27f, 0.20f, 1.00f};
    c[ImGuiCol_ResizeGrip]        = {0.35f, 0.30f, 0.18f, 0.40f};
}

// ---------------------------------------------------------------------------
// Window icon
// ---------------------------------------------------------------------------

static void set_window_icon(SDL_Window* window)
{
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = nullptr;

#ifdef _WIN32
    // Load from embedded resource first
    HRSRC   hres  = FindResource(nullptr, MAKEINTRESOURCE(RES_ICON_IMAGE), RT_RCDATA);
    HGLOBAL hglob = hres ? LoadResource(nullptr, hres) : nullptr;
    const void* src = hglob ? LockResource(hglob) : nullptr;
    DWORD       sz  = hres  ? SizeofResource(nullptr, hres) : 0;
    if (src && sz)
        pixels = stbi_load_from_memory((const stbi_uc*)src, (int)sz, &w, &h, &ch, 4);
#endif

    if (!pixels) {
        // Fall back to file on disk
        pixels = stbi_load("assets/images/icon.png", &w, &h, &ch, 4);
    }
    if (!pixels) return;

    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        pixels, w, h, 32, w * 4,
        0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    if (surf) {
        SDL_SetWindowIcon(window, surf);
        SDL_FreeSurface(surf);
    }
    stbi_image_free(pixels);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Optionally override DB path via first argument or env var
    const char* db_path = BIBLE_DB_PATH;
    if (argc > 1) db_path = argv[1];
    const char* env_path = SDL_getenv("BIBLE_DB");
    if (env_path && *env_path) db_path = env_path;

    // ---- SDL2 init --------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    // OpenGL 3.3 Core
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,        0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,          1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,            24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,          8);

    // High-DPI
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

    Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                       SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow(
        "The Holy Bible",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        win_flags
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

#ifdef _WIN32
    // Dark title bar (Windows 10 20H1+ / Windows 11)
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(window, &wm)) {
            BOOL dark = TRUE;
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
            DwmSetWindowAttribute(wm.info.win.window, 20, &dark, sizeof(dark));
        }
    }
#endif

    set_window_icon(window);

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1); // vsync

    // ---- ImGui ------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // NavEnableKeyboard intentionally omitted — we handle all keys manually.
    io.IniFilename = "bible_reader_layout.ini";

    AppFonts fonts;
    setup_fonts(fonts, FONT_BODY_SIZE);
    setup_style();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ---- App init ---------------------------------------------------------
    AppState app;
    app.fonts = fonts;

    if (!app_init(&app, db_path)) {
        fprintf(stderr, "Failed to open Bible database at: %s\n", db_path);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Bible Reader",
            "Could not open KJV.db.\n"
            "Make sure the Bible database files are next to the executable.",
            window);
        return 1;
    }

    // ---- Main loop --------------------------------------------------------
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
            if (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_SLASH &&
                !event.key.repeat)
                app.request_context_popup = true;
        }

        // Rebuild font atlas if size was changed (A-/A+ or Ctrl+/-/0)
        if (app.request_font_rebuild) {
            app.request_font_rebuild = false;
            io.Fonts->Clear();
            setup_fonts(app.fonts, app.font_size);
            // The OpenGL3 backend detects TexReady==false in RenderDrawData
            // and automatically re-uploads the atlas texture.
        }

        // High-DPI scale
        int draw_w, draw_h;
        SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);
        float dpi_scale = (float)draw_w / (float)win_w;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Scale UI for DPI
        ImGui::GetIO().DisplayFramebufferScale = {dpi_scale, dpi_scale};

        app_render(&app);

        // Window management signals from the UI
        if (app.request_close)   running = false;
        if (app.request_minimize) {
            SDL_MinimizeWindow(window);
            app.request_minimize = false;
        }
        if (app.request_fullscreen_toggle) {
            app.request_fullscreen_toggle = false;
            bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        }

        ImGui::Render();
        glViewport(0, 0, draw_w, draw_h);
        glClearColor(0.10f, 0.09f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // ---- Cleanup ----------------------------------------------------------
    app_shutdown(&app);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
