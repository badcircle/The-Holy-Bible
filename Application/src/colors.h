#pragma once
#include "imgui.h"

// Shared colour palette — included by all render files.
// (ABGR in ImGui = 0xAABBGGRR)
static constexpr ImVec4 COL_BG_DARK    = {0.10f, 0.09f, 0.08f, 1.0f};
static constexpr ImVec4 COL_SIDEBAR_BG = {0.13f, 0.12f, 0.11f, 1.0f};
static constexpr ImVec4 COL_PANEL_BG   = {0.15f, 0.14f, 0.12f, 1.0f};
static constexpr ImVec4 COL_TEXT_BODY  = {0.90f, 0.87f, 0.80f, 1.0f};
static constexpr ImVec4 COL_TEXT_DIM   = {0.55f, 0.52f, 0.45f, 1.0f};
static constexpr ImVec4 COL_VERSE_NUM  = {0.72f, 0.58f, 0.32f, 1.0f}; // warm gold
static constexpr ImVec4 COL_HEADER     = {0.85f, 0.75f, 0.55f, 1.0f}; // book/chapter titles
static constexpr ImVec4 COL_SELECTED   = {0.30f, 0.25f, 0.15f, 1.0f}; // sidebar selection
