#pragma once

// Display name lookup for every book by (testament_id, book_id).
// Testament IDs: 1 = OT, 2 = NT, 3 = Deuterocanonical / Apocrypha.
// Falls back to nullptr when a book_id is out of range — callers should
// then fall back to the short_name stored in the database.

static inline const char* book_display_name(int testament_id, int book_id) {

    // -----------------------------------------------------------------
    // OLD TESTAMENT  (testament_id == 1, book_id 1-39)
    // -----------------------------------------------------------------
    static const char* const OT[] = {
        nullptr,           //  0  (unused)
        "Genesis",         //  1
        "Exodus",          //  2
        "Leviticus",       //  3
        "Numbers",         //  4
        "Deuteronomy",     //  5
        "Joshua",          //  6
        "Judges",          //  7
        "Ruth",            //  8
        "1 Samuel",        //  9
        "2 Samuel",        // 10
        "1 Kings",         // 11
        "2 Kings",         // 12
        "1 Chronicles",    // 13
        "2 Chronicles",    // 14
        "Ezra",            // 15
        "Nehemiah",        // 16
        "Esther",          // 17
        "Job",             // 18
        "Psalms",          // 19
        "Proverbs",        // 20
        "Ecclesiastes",    // 21
        "Song of Solomon", // 22
        "Isaiah",          // 23
        "Jeremiah",        // 24
        "Lamentations",    // 25
        "Ezekiel",         // 26
        "Daniel",          // 27
        "Hosea",           // 28
        "Joel",            // 29
        "Amos",            // 30
        "Obadiah",         // 31
        "Jonah",           // 32
        "Micah",           // 33
        "Nahum",           // 34
        "Habakkuk",        // 35
        "Zephaniah",       // 36
        "Haggai",          // 37
        "Zechariah",       // 38
        "Malachi",         // 39
    };

    // -----------------------------------------------------------------
    // NEW TESTAMENT  (testament_id == 2, book_id 1-27)
    // -----------------------------------------------------------------
    static const char* const NT[] = {
        nullptr,            //  0  (unused)
        "Matthew",          //  1
        "Mark",             //  2
        "Luke",             //  3
        "John",             //  4
        "Acts",             //  5
        "Romans",           //  6
        "1 Corinthians",    //  7
        "2 Corinthians",    //  8
        "Galatians",        //  9
        "Ephesians",        // 10
        "Philippians",      // 11
        "Colossians",       // 12
        "1 Thessalonians",  // 13
        "2 Thessalonians",  // 14
        "1 Timothy",        // 15
        "2 Timothy",        // 16
        "Titus",            // 17
        "Philemon",         // 18
        "Hebrews",          // 19
        "James",            // 20
        "1 Peter",          // 21
        "2 Peter",          // 22
        "1 John",           // 23
        "2 John",           // 24
        "3 John",           // 25
        "Jude",             // 26
        "Revelation",       // 27
    };

    // -----------------------------------------------------------------
    // DEUTEROCANONICAL / APOCRYPHA  (testament_id == 3, book_id 1-22)
    // Book IDs match the convert.py BOOK_MAP assignment.
    // -----------------------------------------------------------------
    static const char* const DC[] = {
        nullptr,                    //  0  (unused)
        "Tobit",                    //  1
        "Judith",                   //  2
        "Additions to Esther",      //  3
        "Wisdom",                   //  4
        "Sirach",                   //  5
        "Baruch",                   //  6
        "Epistle of Jeremiah",      //  7
        "Susanna",                  //  8
        "Bel and the Dragon",       //  9
        "1 Maccabees",              // 10
        "2 Maccabees",              // 11
        "3 Maccabees",              // 12
        "4 Maccabees",              // 13
        "1 Esdras",                 // 14
        "2 Esdras",                 // 15
        "Prayer of Manasseh",       // 16
        "Prayer of Azariah",        // 17
        "Odes",                     // 18
        "Psalms of Solomon",        // 19
        "Joshua (B Text)",          // 20
        "Judges (B Text)",          // 21
        "Laodiceans",               // 22
    };

    if (testament_id == 1 && book_id >= 1 && book_id <= 39) return OT[book_id];
    if (testament_id == 2 && book_id >= 1 && book_id <= 27) return NT[book_id];
    if (testament_id == 3 && book_id >= 1 && book_id <= 22) return DC[book_id];
    return nullptr;
}
