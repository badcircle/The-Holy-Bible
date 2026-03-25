#pragma once
// Windows resource IDs for embedded fonts.
// Must match the IDs in resources.rc.in / the generated resources.rc.
#define RES_FONT_BODY       101
#define RES_FONT_HEADING    102
#define RES_FONT_VERSE_NUM  103
#define RES_FONT_UI         104

// Embedded cover image
#define RES_COVER_IMAGE     105

// Embedded window/taskbar icon
#define RES_ICON_IMAGE      106

// Embedded Hebrew font (Noto Serif Hebrew)
#define RES_FONT_HEBREW     107

// Embedded Bible database (only present when built with -DBR_EMBED_BIBLE=ON)
#define RES_BIBLE_DB        200

// Embedded translation databases (only present when built with -DBR_SHIP=ON)
#define RES_DB_KJV          201
#define RES_DB_SEPTUAGINT   202
#define RES_DB_VULGATE      203
#define RES_DB_UGNT         204
#define RES_DB_TANAKH       205
#define RES_DB_APOCRYPHA    206
