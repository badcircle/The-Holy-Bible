#pragma once
#include <vector>
#include <string>

// -----------------------------------------------------------------------
// Mirrors the schema in Bible.db
// -----------------------------------------------------------------------

struct Book {
    int  testament_id;   // 1 = OT, 2 = NT
    int  book_id;        // 1–66
    char short_name[32]; // "Genesis"
    char long_name[128]; // "The First Book of Moses, called Genesis"
    int  num_chapters;
};

struct Verse {
    int  verse_id;
    char text[4096];     // Passage1 with the "ch:v " prefix stripped
    bool paragraph_start; // NumParagraphs > 0 in the row above (small gap)
};

// Opaque handle
struct BibleDB;

BibleDB*    bible_db_open(const char* path);
BibleDB*    bible_db_open_from_resource(int resource_id); // Windows RCDATA; no-op on other platforms
void        bible_db_close(BibleDB* db);

// Returns number of books loaded (up to max_books)
int         bible_db_get_books(BibleDB* db, Book* out, int max_books);

// Fills 'out' with all verses for the given testament+book+chapter.
// Returns verse count, or -1 on error.
// testament_id: 1=OT, 2=NT  (BookID restarts at 1 for each testament)
int         bible_db_get_chapter(BibleDB* db, int testament_id, int book_id,
                                 int chapter_id, Verse* out, int max_verses);

int         bible_db_get_num_chapters(BibleDB* db, int testament_id, int book_id);
int         bible_db_get_first_chapter(BibleDB* db, int testament_id, int book_id);
int         bible_db_get_num_verses(BibleDB* db, int testament_id, int book_id,
                                    int chapter_id);
