#pragma once
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// RTL text reversal (Hebrew / Tanakh)
//
// ImGui has no BiDi support — it always renders glyphs left-to-right.
// Hebrew Unicode text is stored in logical order (reading order = right-to-left),
// so the first codepoint ends up on the LEFT, which is visually backwards.
//
// Solution: reverse the string at the grapheme-cluster level so that the first
// reading character ends up last (rightmost) after LTR rendering.  Combining
// marks (niqqud, cantillation) stay attached to their base character.
// ---------------------------------------------------------------------------

static uint32_t rtl_decode_utf8(const char*& p) {
    auto b = [](const char* q, int i) { return (unsigned char)q[i]; };
    unsigned char c = b(p, 0);
    uint32_t cp; int n;
    if      (c < 0x80) { cp = c;        n = 1; }
    else if (c < 0xC0) { cp = 0xFFFD;   n = 1; }   // stray continuation
    else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
    else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
    else               { cp = c & 0x07; n = 4; }
    for (int i = 1; i < n && p[i]; ++i) cp = (cp << 6) | (b(p, i) & 0x3F);
    p += n;
    return cp;
}

static void rtl_encode_utf8(std::string& out, uint32_t cp) {
    if      (cp < 0x80)    { out += (char)cp; }
    else if (cp < 0x800)   { out += (char)(0xC0|(cp>>6));  out += (char)(0x80|(cp&0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
    else                   { out += (char)(0xF0|(cp>>18)); out += (char)(0x80|((cp>>12)&0x3F)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
}

static bool rtl_is_combining(uint32_t cp) {
    // Hebrew cantillation (0591-05AF) and niqqud/vowel points (05B0-05C7),
    // excluding standalone punctuation: maqaf(05BE), paseq(05C0), sof-pasuq(05C3), nun-hafukha(05C6)
    if (cp >= 0x0591 && cp <= 0x05C7)
        return cp != 0x05BE && cp != 0x05C0 && cp != 0x05C3 && cp != 0x05C6;
    return false;
}

// Reverse a UTF-8 Hebrew string for correct visual display in a LTR renderer.
static std::string rtl_reverse(const char* text) {
    // Decode into clusters: [base_cp, combining_cp, combining_cp, ...]
    struct Cluster { uint32_t cps[8]; int n = 0; };
    std::vector<Cluster> clusters;
    const char* p = text;
    while (*p) {
        uint32_t cp = rtl_decode_utf8(p);
        if (rtl_is_combining(cp) && !clusters.empty()) {
            auto& cl = clusters.back();
            if (cl.n < 8) cl.cps[cl.n++] = cp;
        } else {
            Cluster cl; cl.cps[0] = cp; cl.n = 1;
            clusters.push_back(cl);
        }
    }
    std::reverse(clusters.begin(), clusters.end());
    std::string out;
    out.reserve(strlen(text));
    for (auto& cl : clusters)
        for (int i = 0; i < cl.n; ++i)
            rtl_encode_utf8(out, cl.cps[i]);
    return out;
}
