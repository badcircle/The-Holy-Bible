cmake_minimum_required(VERSION 3.20)
project(BibleReader CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------
# Optional: fully-static single-file distribution (no DLLs).
# Requires rebuilding vcpkg packages with the x64-windows-static triplet:
#   vcpkg install --triplet x64-windows-static sdl2 sqlite3 freetype
# Then configure with: cmake -DBR_STATIC_DIST=ON ...
# -----------------------------------------------------------------------
option(BR_STATIC_DIST "Link everything statically for single-file distribution" OFF)
if(BR_STATIC_DIST)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# -----------------------------------------------------------------------
# Paths
# -----------------------------------------------------------------------
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/../Libraries/imgui-1.92.6")

# KJV.db is the default; other translations (Septuagint.db, Vulgate.db, etc.)
# must also be present next to the executable at runtime.
# Override on the cmake command line: -DBIBLE_DB_PATH="C:/my/path/KJV.db"
if(NOT BIBLE_DB_PATH)
    set(BIBLE_DB_PATH "${CMAKE_SOURCE_DIR}/../Bibles/KJV.db")
endif()
cmake_path(CONVERT "${BIBLE_DB_PATH}" TO_CMAKE_PATH_LIST BIBLE_DB_PATH_CMAKE)

# -----------------------------------------------------------------------
# Font embedding — generate the Windows resource script from the template
# Fonts are stored in assets/fonts/ relative to the project source.
# -----------------------------------------------------------------------
set(_FONTS_DIR "${CMAKE_SOURCE_DIR}/assets/fonts")
set(FONT_BODY      "${_FONTS_DIR}/Gentium-7.000/Gentium-Regular.ttf")
set(FONT_HEADING   "${_FONTS_DIR}/Pirata_One/PirataOne-Regular.ttf")
set(FONT_VERSE_NUM "${_FONTS_DIR}/Crimson_Pro/static/CrimsonPro-Regular.ttf")
set(FONT_UI        "${_FONTS_DIR}/Roboto/static/Roboto-Regular.ttf")
set(FONT_HEBREW    "${_FONTS_DIR}/Noto_Serif_Hebrew/static/NotoSerifHebrew-Regular.ttf")
set(COVER_IMAGE    "${CMAKE_SOURCE_DIR}/assets/images/Cover.jpg")
set(ICON_IMAGE     "${CMAKE_SOURCE_DIR}/assets/images/icon.png")

# -----------------------------------------------------------------------
# Ship build — embed all six translation databases into the exe so that
# no external .db files are needed at runtime.
#
#   cmake -DBR_SHIP=ON ..
#
# For a fully self-contained single .exe (no SDL2.dll either), also pass:
#   -DBR_STATIC_DIST=ON  and  -DVCPKG_TARGET_TRIPLET=x64-windows-static
# (requires rebuilding vcpkg deps with the static triplet first)
# -----------------------------------------------------------------------
option(BR_SHIP "Embed all six translation databases into the exe for distribution" OFF)

# -----------------------------------------------------------------------
# Legacy option: embed only KJV (Bible.db) — kept for backward compat.
# Prefer BR_SHIP for new builds.
# -----------------------------------------------------------------------
option(BR_EMBED_BIBLE "Embed a single Bible.db into the exe (legacy)" OFF)

set(_BIBLES_DIR "${CMAKE_SOURCE_DIR}/../Bibles")

if(BR_SHIP)
    set(DB_KJV        "${_BIBLES_DIR}/KJV.db")
    set(DB_SEPTUAGINT "${_BIBLES_DIR}/Septuagint.db")
    set(DB_VULGATE    "${_BIBLES_DIR}/Vulgate.db")
    set(DB_UGNT       "${_BIBLES_DIR}/UGNT.db")
    set(DB_TANAKH     "${_BIBLES_DIR}/Tanakh.db")
    set(DB_APOCRYPHA  "${_BIBLES_DIR}/Apocrypha.db")
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/resources_ship.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
        @ONLY
    )
elseif(BR_EMBED_BIBLE)
    set(BIBLE_DB_EMBED_PATH "${BIBLE_DB_PATH}")
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/resources_full.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
        @ONLY
    )
else()
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/resources.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
        @ONLY
    )
endif()

# -----------------------------------------------------------------------
# Dependencies (install via vcpkg — see NEXT_STEPS.md)
# -----------------------------------------------------------------------
find_package(SDL2   CONFIG REQUIRED)
find_package(OpenGL        REQUIRED)
find_package(SQLite3       REQUIRED)
find_package(Freetype      REQUIRED)
find_package(Stb            REQUIRED)

# -----------------------------------------------------------------------
# ImGui — core + SDL2/OpenGL3 backends + FreeType
# -----------------------------------------------------------------------
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/freetype
)
target_compile_definitions(imgui PUBLIC
    IMGUI_ENABLE_FREETYPE
    IMGUI_USE_WCHAR32   # full Unicode range
)
target_link_libraries(imgui PUBLIC
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    OpenGL::GL
    Freetype::Freetype
)

# -----------------------------------------------------------------------
# BibleReader executable
# -----------------------------------------------------------------------
add_executable(BibleReader WIN32
    src/main.cpp
    src/bible_db.cpp
    src/app_init.cpp
    src/app_render.cpp
    src/render_sidebar.cpp
    src/render_cover.cpp
    src/render_tray.cpp
    src/render_reader.cpp
    src/render_toolbar.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc"
)

target_include_directories(BibleReader PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/freetype
    src
    ${Stb_INCLUDE_DIR}
)

target_compile_definitions(BibleReader PRIVATE
    IMGUI_ENABLE_FREETYPE
    IMGUI_USE_WCHAR32
    BIBLE_DB_PATH="${BIBLE_DB_PATH_CMAKE}"
    $<$<BOOL:${BR_EMBED_BIBLE}>:BR_EMBED_BIBLE>
    $<$<BOOL:${BR_SHIP}>:BR_SHIP>
)

target_link_libraries(BibleReader PRIVATE
    imgui
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
    SDL2::SDL2main
    OpenGL::GL
    SQLite::SQLite3
    Freetype::Freetype
    $<$<PLATFORM_ID:Windows>:dwmapi>
)

# Copy SDL2 DLL next to the .exe (skipped when using the static triplet)
if(WIN32 AND NOT BR_STATIC_DIST)
    add_custom_command(TARGET BibleReader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:BibleReader>
        COMMENT "Copying SDL2.dll"
    )
endif()
