#include "bible_db.h"
#include <sqlite3.h>
#include <cstdio>
#include <cstring>

#if defined(_WIN32) && (defined(BR_EMBED_BIBLE) || defined(BR_SHIP))
#  include <windows.h>
#  include "resources.h"
#endif

struct BibleDB {
    sqlite3* db;
};

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// Parse a single Passage1 string into individual Verse entries.
//
// The DB stores verses in paragraph groups: one row may contain multiple verses
// concatenated with embedded references, e.g.:
//   "1:1 Paul, an apostle... 1:2 Grace be to you..."
//
// chapter   : the chapter number being read (used to identify verse refs)
// passage   : raw Passage1 text
// out / max : output buffer
// para_break: if true, the *first* verse produced is flagged paragraph_start
//
// Returns number of Verse structs emitted (>= 1 on success, 0 on parse error).
static int parse_passage(int chapter, const char* passage,
                         Verse* out, int max, bool para_break)
{
    if (!passage || !*passage || max <= 0) return 0;

    // Prefix used to identify verse refs in this chapter, e.g. "1:"
    char ch_prefix[16];
    int  ch_prefix_len = snprintf(ch_prefix, sizeof(ch_prefix), "%d:", chapter);

    // Pattern used to find the NEXT verse ref inside the text: " 1:"
    char next_pat[20];
    int  next_pat_len = snprintf(next_pat, sizeof(next_pat), " %d:", chapter);

    const char* p     = passage;
    int         count = 0;
    bool        first = true;

    while (*p && count < max) {
        // Expect "chapter:verse " at the current position
        if (strncmp(p, ch_prefix, ch_prefix_len) != 0) break;
        p += ch_prefix_len;

        // Read the verse number
        char* endp;
        int verse_num = (int)strtol(p, &endp, 10);
        if (endp == p || *endp != ' ') break;
        p = endp + 1; // step past the space

        // Find where this verse's text ends.
        // Check for the next verse in the same chapter (" N:V ") or a cross-chapter
        // spill into chapter+1 — both handled in one pass.
        char next_ch_pat[20];
        int  next_ch_pat_len = snprintf(next_ch_pat, sizeof(next_ch_pat), " %d:", chapter + 1);

        const char* text_start = p;
        const char* text_end   = nullptr;

        for (const char* s = p; *s; ++s) {
            const char* pat = nullptr;
            int         plen = 0;
            if      (strncmp(s, next_pat,    next_pat_len)    == 0) { pat = next_pat;    plen = next_pat_len;    }
            else if (strncmp(s, next_ch_pat, next_ch_pat_len) == 0) { pat = next_ch_pat; plen = next_ch_pat_len; }
            if (pat) {
                char* ne; strtol(s + plen, &ne, 10);
                if (ne > s + plen && *ne == ' ') { text_end = s; break; }
            }
        }

        if (!text_end) text_end = p + strlen(p); // end of string

        // Emit verse
        Verse& v          = out[count++];
        v.verse_id        = verse_num;
        v.paragraph_start = first ? para_break : false;
        first             = false;

        int len = (int)(text_end - text_start);
        if (len < 0)                        len = 0;
        if (len >= (int)sizeof(v.text))     len = (int)sizeof(v.text) - 1;
        memcpy(v.text, text_start, len);
        v.text[len] = '\0';
        // Normalise: replace any \r or \n with a space so ImGui doesn't
        // treat them as hard line-breaks inside a verse
        for (int i = 0; i < len; ++i)
            if (v.text[i] == '\r' || v.text[i] == '\n') v.text[i] = ' ';
        // Trim trailing whitespace (spaces, \r, \n, \t)
        while (len > 0 && (unsigned char)v.text[len - 1] <= ' ') v.text[--len] = '\0';
        // Trim leading whitespace
        int lead = 0;
        while (lead < len && (unsigned char)v.text[lead] <= ' ') ++lead;
        if (lead > 0) { memmove(v.text, v.text + lead, len - lead + 1); len -= lead; }
        // Collapse runs of multiple spaces into one
        int w = 0;
        for (int i = 0; i < len; ++i) {
            if (v.text[i] == ' ' && w > 0 && v.text[w - 1] == ' ') continue;
            v.text[w++] = v.text[i];
        }
        v.text[w] = '\0'; len = w;

        if (!*text_end) break;      // end of string — done
        p = text_end + 1;           // skip the space that precedes the next ref
    }

    return count;
}

// ---------------------------------------------------------------------------
// open / close
// ---------------------------------------------------------------------------

// Shared helper: deserialize a raw SQLite image into a new in-memory connection.
static BibleDB* open_from_blob(const void* src, size_t sz) {
    sqlite3* raw = nullptr;
    if (sqlite3_open(":memory:", &raw) != SQLITE_OK) {
        sqlite3_close(raw);
        return nullptr;
    }
    unsigned char* data = (unsigned char*)sqlite3_malloc64((sqlite3_int64)sz);
    if (!data) { sqlite3_close(raw); return nullptr; }
    memcpy(data, src, sz);
    int rc = sqlite3_deserialize(raw, "main", data,
                                 (sqlite3_int64)sz, (sqlite3_int64)sz,
                                 SQLITE_DESERIALIZE_FREEONCLOSE |
                                 SQLITE_DESERIALIZE_RESIZEABLE);
    if (rc != SQLITE_OK) {
        sqlite3_free(data);
        sqlite3_close(raw);
        return nullptr;
    }
    sqlite3_exec(raw, "PRAGMA cache_size = 8000;",  nullptr, nullptr, nullptr);
    sqlite3_exec(raw, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
    auto* db = new BibleDB;
    db->db = raw;
    return db;
}

BibleDB* bible_db_open_from_resource(int resource_id) {
#if defined(_WIN32) && (defined(BR_EMBED_BIBLE) || defined(BR_SHIP))
    HRSRC   hres  = FindResource(nullptr, MAKEINTRESOURCE(resource_id), RT_RCDATA);
    HGLOBAL hglob = hres  ? LoadResource(nullptr, hres)  : nullptr;
    const void* src = hglob ? LockResource(hglob) : nullptr;
    DWORD sz = hres ? SizeofResource(nullptr, hres) : 0;
    if (src && sz) return open_from_blob(src, (size_t)sz);
#else
    (void)resource_id;
#endif
    return nullptr;
}

BibleDB* bible_db_open(const char* path) {
    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(path, &raw, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        sqlite3_exec(raw, "PRAGMA cache_size = 8000;", nullptr, nullptr, nullptr);
        sqlite3_exec(raw, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
        auto* db = new BibleDB;
        db->db   = raw;
        return db;
    }
    sqlite3_close(raw);
    raw = nullptr;

#if defined(_WIN32) && defined(BR_EMBED_BIBLE)
    // File not found — try the legacy single-DB embedded resource
    { BibleDB* e = bible_db_open_from_resource(RES_BIBLE_DB); if (e) return e; }
#endif

    fprintf(stderr, "bible_db: cannot open '%s'\n", path);
    return nullptr;
}

void bible_db_close(BibleDB* db) {
    if (!db) return;
    sqlite3_close(db->db);
    delete db;
}

// ---------------------------------------------------------------------------
// books
// ---------------------------------------------------------------------------

int bible_db_get_books(BibleDB* db, Book* out, int max_books) {
    // Join Books with BookStats so we get num_chapters in one pass
    const char* sql =
        "SELECT b.TestamentID, b.BookID, b.ShortName, b.LongName, COALESCE(s.NumChapters,0) "
        "FROM Books b "
        "LEFT JOIN BookStats s ON s.TestamentID = b.TestamentID AND s.BookID = b.BookID "
        "ORDER BY b.TestamentID, b.BookID;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "bible_db_get_books: %s\n", sqlite3_errmsg(db->db));
        return 0;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_books) {
        Book& bk         = out[count++];
        bk.testament_id  = sqlite3_column_int(stmt, 0);
        bk.book_id       = sqlite3_column_int(stmt, 1);
        const char* sn   = (const char*)sqlite3_column_text(stmt, 2);
        const char* ln   = (const char*)sqlite3_column_text(stmt, 3);
        bk.num_chapters  = sqlite3_column_int(stmt, 4);
        snprintf(bk.short_name, sizeof(bk.short_name), "%s", sn ? sn : "");
        snprintf(bk.long_name,  sizeof(bk.long_name),  "%s", ln ? ln : "");
    }

    sqlite3_finalize(stmt);
    return count;
}

// ---------------------------------------------------------------------------
// chapter
// ---------------------------------------------------------------------------

int bible_db_get_chapter(BibleDB* db, int testament_id, int book_id,
                          int chapter_id, Verse* out, int max_verses)
{
    int count = 0;

    // ------------------------------------------------------------------
    // Look-back: the last passage row of the previous chapter may contain
    // verses that belong to this chapter (DB paragraph groups can span
    // chapter boundaries). Extract any such verses first.
    // ------------------------------------------------------------------
    if (chapter_id > 1 && count < max_verses) {
        const char* prev_sql =
            "SELECT Passage1 FROM Bible "
            "WHERE TestamentID = ? AND BookID = ? AND ChapterID = ? "
            "ORDER BY VerseID DESC LIMIT 1;";
        sqlite3_stmt* prev_stmt = nullptr;
        if (sqlite3_prepare_v2(db->db, prev_sql, -1, &prev_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(prev_stmt, 1, testament_id);
            sqlite3_bind_int(prev_stmt, 2, book_id);
            sqlite3_bind_int(prev_stmt, 3, chapter_id - 1);
            if (sqlite3_step(prev_stmt) == SQLITE_ROW) {
                const char* passage = (const char*)sqlite3_column_text(prev_stmt, 0);
                if (passage) {
                    // Search for " {chapter_id}:" inside the previous passage
                    char search[20];
                    snprintf(search, sizeof(search), " %d:", chapter_id);
                    const char* found = strstr(passage, search);
                    if (found) {
                        // Parse from here (skip the leading space)
                        count = parse_passage(chapter_id, found + 1,
                                              out, max_verses, false);
                    }
                }
            }
            sqlite3_finalize(prev_stmt);
        }
    }

    // ------------------------------------------------------------------
    // Main query: rows tagged ChapterID = chapter_id
    // ------------------------------------------------------------------
    const char* sql =
        "SELECT VerseID, Passage1, NumParagraphs "
        "FROM Bible "
        "WHERE TestamentID = ? AND BookID = ? AND ChapterID = ? "
        "ORDER BY VerseID;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "bible_db_get_chapter: %s\n", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, testament_id);
    sqlite3_bind_int(stmt, 2, book_id);
    sqlite3_bind_int(stmt, 3, chapter_id);

    bool first_row = (count == 0); // no para-break before overflow verses
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_verses) {
        const char* passage = (const char*)sqlite3_column_text(stmt, 1);
        bool para_break     = !first_row;
        first_row           = false;

        int n = parse_passage(chapter_id, passage,
                              out + count, max_verses - count, para_break);
        count += n;
    }

    sqlite3_finalize(stmt);
    return count;
}

// ---------------------------------------------------------------------------
// stats helpers
// ---------------------------------------------------------------------------

int bible_db_get_num_chapters(BibleDB* db, int testament_id, int book_id) {
    const char* sql =
        "SELECT NumChapters FROM BookStats WHERE TestamentID = ? AND BookID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, testament_id);
    sqlite3_bind_int(stmt, 2, book_id);
    int result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

int bible_db_get_first_chapter(BibleDB* db, int testament_id, int book_id) {
    const char* sql =
        "SELECT MIN(ChapterID) FROM Bible WHERE TestamentID = ? AND BookID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 1;
    sqlite3_bind_int(stmt, 1, testament_id);
    sqlite3_bind_int(stmt, 2, book_id);
    int result = 1;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

int bible_db_get_num_verses(BibleDB* db, int testament_id, int book_id, int chapter_id) {
    const char* sql =
        "SELECT NumVerses FROM ChapterStats "
        "WHERE TestamentID = ? AND BookID = ? AND ChapterID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, testament_id);
    sqlite3_bind_int(stmt, 2, book_id);
    sqlite3_bind_int(stmt, 3, chapter_id);
    int result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}
