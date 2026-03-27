#!/usr/bin/env python3
"""
Convert Bible pipe-delimited text files to BibleReader-compatible SQLite databases.

Input format:  BookCode|Chapter|Verse|Text
Output schema: Books, Bible, BookStats, ChapterStats  (same as Bible.db)

Each verse is stored as one row; Passage1 = "chapter:verse text"
which the BibleReader parser handles natively.
"""

import re
import sqlite3
import html
from pathlib import Path
from collections import defaultdict

BIBLES_DIR = Path(__file__).parent

# ---------------------------------------------------------------------------
# Testament IDs
# ---------------------------------------------------------------------------
OT = 1   # Old Testament
NT = 2   # New Testament
DC = 3   # Deuterocanonical / Apocrypha

# ---------------------------------------------------------------------------
# Book catalogue  (code -> testament_id, book_id, short_name, long_name)
# ---------------------------------------------------------------------------
BOOK_MAP = {
    # ── OLD TESTAMENT (39) ────────────────────────────────────────────────
    "Gen":  (OT,  1,  "Gen",    "Genesis"),
    "gen":  (OT,  1,  "Gen",    "Genesis"),
    "Exo":  (OT,  2,  "Exo",   "Exodus"),
    "exo":  (OT,  2,  "Exo",   "Exodus"),
    "Lev":  (OT,  3,  "Lev",   "Leviticus"),
    "lev":  (OT,  3,  "Lev",   "Leviticus"),
    "Num":  (OT,  4,  "Num",   "Numbers"),
    "num":  (OT,  4,  "Num",   "Numbers"),
    "Deu":  (OT,  5,  "Deu",   "Deuteronomy"),
    "deu":  (OT,  5,  "Deu",   "Deuteronomy"),
    "Jos":  (OT,  6,  "Jos",   "Joshua"),
    "jos":  (OT,  6,  "Jos",   "Joshua"),
    "JsA":  (DC, 20,  "Jos A", "Joshua (A Text)"),   # LXX A text (partial variant)
    "Jdg":  (OT,  7,  "Jdg",   "Judges"),
    "jdg":  (OT,  7,  "Jdg",   "Judges"),
    "JdA":  (OT,  7,  "Jdg",   "Judges"),           # LXX A text (shorter)
    "Rut":  (OT,  8,  "Rut",   "Ruth"),
    "rut":  (OT,  8,  "Rut",   "Ruth"),
    "Sa1":  (OT,  9,  "1 Sam", "1 Samuel"),
    "sa1":  (OT,  9,  "1 Sam", "1 Samuel"),
    "Sa2":  (OT, 10,  "2 Sam", "2 Samuel"),
    "sa2":  (OT, 10,  "2 Sam", "2 Samuel"),
    "Kg1":  (OT, 11,  "1 Kgs", "1 Kings"),
    "kg1":  (OT, 11,  "1 Kgs", "1 Kings"),
    "Kg2":  (OT, 12,  "2 Kgs", "2 Kings"),
    "kg2":  (OT, 12,  "2 Kgs", "2 Kings"),
    "Ch1":  (OT, 13,  "1 Chr", "1 Chronicles"),
    "ch1":  (OT, 13,  "1 Chr", "1 Chronicles"),
    "Ch2":  (OT, 14,  "2 Chr", "2 Chronicles"),
    "ch2":  (OT, 14,  "2 Chr", "2 Chronicles"),
    "Ezr":  (OT, 15,  "Ezr",   "Ezra"),
    "ezr":  (OT, 15,  "Ezr",   "Ezra"),
    "Neh":  (OT, 16,  "Neh",   "Nehemiah"),
    "neh":  (OT, 16,  "Neh",   "Nehemiah"),
    "Est":  (OT, 17,  "Est",   "Esther"),
    "est":  (OT, 17,  "Est",   "Esther"),
    "Job":  (OT, 18,  "Job",   "Job"),
    "job":  (OT, 18,  "Job",   "Job"),
    "Psa":  (OT, 19,  "Psa",   "Psalms"),
    "psa":  (OT, 19,  "Psa",   "Psalms"),
    "Pro":  (OT, 20,  "Pro",   "Proverbs"),
    "pro":  (OT, 20,  "Pro",   "Proverbs"),
    "Ecc":  (OT, 21,  "Ecc",   "Ecclesiastes"),
    "ecc":  (OT, 21,  "Ecc",   "Ecclesiastes"),
    "Sol":  (OT, 22,  "Sol",   "Song of Solomon"),
    "sol":  (OT, 22,  "Sol",   "Song of Solomon"),
    "Isa":  (OT, 23,  "Isa",   "Isaiah"),
    "isa":  (OT, 23,  "Isa",   "Isaiah"),
    "Jer":  (OT, 24,  "Jer",   "Jeremiah"),
    "jer":  (OT, 24,  "Jer",   "Jeremiah"),
    "Lam":  (OT, 25,  "Lam",   "Lamentations"),
    "lam":  (OT, 25,  "Lam",   "Lamentations"),
    "Eze":  (OT, 26,  "Eze",   "Ezekiel"),
    "eze":  (OT, 26,  "Eze",   "Ezekiel"),
    "Dan":  (OT, 27,  "Dan",   "Daniel"),
    "dan":  (OT, 27,  "Dan",   "Daniel"),
    "DaT":  (OT, 27,  "Dan",   "Daniel"),           # Theodotion Daniel (standard LXX)
    "Hos":  (OT, 28,  "Hos",   "Hosea"),
    "hos":  (OT, 28,  "Hos",   "Hosea"),
    "Joe":  (OT, 29,  "Joe",   "Joel"),
    "joe":  (OT, 29,  "Joe",   "Joel"),
    "Amo":  (OT, 30,  "Amo",   "Amos"),
    "amo":  (OT, 30,  "Amo",   "Amos"),
    "Oba":  (OT, 31,  "Oba",   "Obadiah"),
    "oba":  (OT, 31,  "Oba",   "Obadiah"),
    "Jon":  (OT, 32,  "Jon",   "Jonah"),
    "jon":  (OT, 32,  "Jon",   "Jonah"),
    "Mic":  (OT, 33,  "Mic",   "Micah"),
    "mic":  (OT, 33,  "Mic",   "Micah"),
    "Nah":  (OT, 34,  "Nah",   "Nahum"),
    "nah":  (OT, 34,  "Nah",   "Nahum"),
    "Hab":  (OT, 35,  "Hab",   "Habakkuk"),
    "hab":  (OT, 35,  "Hab",   "Habakkuk"),
    "Zep":  (OT, 36,  "Zep",   "Zephaniah"),
    "zep":  (OT, 36,  "Zep",   "Zephaniah"),
    "Hag":  (OT, 37,  "Hag",   "Haggai"),
    "hag":  (OT, 37,  "Hag",   "Haggai"),
    "Zac":  (OT, 38,  "Zac",   "Zechariah"),
    "zac":  (OT, 38,  "Zac",   "Zechariah"),
    "Mal":  (OT, 39,  "Mal",   "Malachi"),
    "mal":  (OT, 39,  "Mal",   "Malachi"),

    # ── NEW TESTAMENT (27) ────────────────────────────────────────────────
    "Mat":  (NT,  1,  "Mat",   "The Gospel According to Matthew"),
    "Mar":  (NT,  2,  "Mar",   "The Gospel According to Mark"),
    "Luk":  (NT,  3,  "Luk",   "The Gospel According to Luke"),
    "Joh":  (NT,  4,  "Joh",   "The Gospel According to John"),
    "Act":  (NT,  5,  "Act",   "The Acts of the Apostles"),
    "Rom":  (NT,  6,  "Rom",   "Romans"),
    "Co1":  (NT,  7,  "1 Cor", "1 Corinthians"),
    "Co2":  (NT,  8,  "2 Cor", "2 Corinthians"),
    "Gal":  (NT,  9,  "Gal",   "Galatians"),
    "Eph":  (NT, 10,  "Eph",   "Ephesians"),
    "Phi":  (NT, 11,  "Phi",   "Philippians"),
    "Col":  (NT, 12,  "Col",   "Colossians"),
    "Th1":  (NT, 13,  "1 The", "1 Thessalonians"),
    "Th2":  (NT, 14,  "2 The", "2 Thessalonians"),
    "Ti1":  (NT, 15,  "1 Tim", "1 Timothy"),
    "Ti2":  (NT, 16,  "2 Tim", "2 Timothy"),
    "Tit":  (NT, 17,  "Tit",   "Titus"),
    "Plm":  (NT, 18,  "Plm",   "Philemon"),
    "Heb":  (NT, 19,  "Heb",   "Hebrews"),
    "Jam":  (NT, 20,  "Jam",   "James"),
    "Pe1":  (NT, 21,  "1 Pet", "1 Peter"),
    "Pe2":  (NT, 22,  "2 Pet", "2 Peter"),
    "Jo1":  (NT, 23,  "1 Joh", "1 John"),
    "Jo2":  (NT, 24,  "2 Joh", "2 John"),
    "Jo3":  (NT, 25,  "3 Joh", "3 John"),
    "Jde":  (NT, 26,  "Jde",   "Jude"),
    "Rev":  (NT, 27,  "Rev",   "Revelation"),

    # ── DEUTEROCANONICAL / APOCRYPHA ──────────────────────────────────────
    "Tob":  (DC,  1,  "Tob",   "Tobit"),
    "ToA":  (DC,  1,  "Tob",   "Tobit"),            # Short recension
    "ToS":  (DC,  1,  "Tob",   "Tobit"),            # Long recension
    "Jdt":  (DC,  2,  "Jdt",   "Judith"),
    "Aes":  (DC,  3,  "Aes",   "Additions to Esther"),
    "Wis":  (DC,  4,  "Wis",   "Wisdom of Solomon"),
    "Sir":  (DC,  5,  "Sir",   "Sirach"),
    "Bar":  (DC,  6,  "Bar",   "Baruch"),
    "Epj":  (DC,  7,  "Epj",   "Epistle of Jeremiah"),
    "Sus":  (DC,  8,  "Sus",   "Susanna"),
    "SuT":  (DC,  8,  "Sus",   "Susanna"),          # Theodotion text
    "Bel":  (DC,  9,  "Bel",   "Bel and the Dragon"),
    "BeT":  (DC,  9,  "Bel",   "Bel and the Dragon"),  # Theodotion text
    "Ma1":  (DC, 10,  "1 Mac", "1 Maccabees"),
    "Ma2":  (DC, 11,  "2 Mac", "2 Maccabees"),
    "Ma3":  (DC, 12,  "3 Mac", "3 Maccabees"),
    "Ma4":  (DC, 13,  "4 Mac", "4 Maccabees"),
    "Es1":  (DC, 14,  "1 Esd", "1 Esdras"),
    "Es2":  (DC, 15,  "2 Esd", "2 Esdras"),
    "Man":  (DC, 16,  "Man",   "Prayer of Manasseh"),
    "Aza":  (DC, 17,  "Aza",   "Prayer of Azariah"),
    "Ode":  (DC, 18,  "Ode",   "Odes"),
    "Pss":  (DC, 19,  "Pss",   "Psalms of Solomon"),
    "JsB":  (OT,  6,  "Jos",   "Joshua"),            # LXX B text (full canonical)
    "JdB":  (DC, 21,  "Jdg B", "Judges (B Text)"),  # LXX B text
    "Lao":  (DC, 22,  "Lao",   "Epistle to the Laodiceans"),
}

# Canonical info for each (testament_id, book_id) — used when inserting Books table
BOOK_INFO = {}
for code, (tid, bid, short, long) in BOOK_MAP.items():
    key = (tid, bid)
    if key not in BOOK_INFO:
        BOOK_INFO[key] = (short, long)

# KJV-specific long names (traditional 1611 canonical titles).
# Keyed by (testament_id, book_id); only applied when building KJV.db.
KJV_LONG_NAMES = {
    (OT,  1): "The First Book of Moses, called Genesis",
    (OT,  2): "The Second Book of Moses, called Exodus",
    (OT,  3): "The Third Book of Moses, called Leviticus",
    (OT,  4): "The Fourth Book of Moses, called Numbers",
    (OT,  5): "The Fifth Book of Moses, called Deuteronomy",
    (OT,  6): "The Book of Joshua",
    (OT,  7): "The Book of Judges",
    (OT,  8): "The Book of Ruth",
    (OT,  9): "The First Book of Samuel, otherwise called the First Book of the Kings",
    (OT, 10): "The Second Book of Samuel, otherwise called the Second Book of the Kings",
    (OT, 11): "The First Book of the Kings, commonly called the Third Book of the Kings",
    (OT, 12): "The Second Book of the Kings, commonly called the Fourth Book of the Kings",
    (OT, 13): "The First Book of the Chronicles",
    (OT, 14): "The Second Book of the Chronicles",
    (OT, 16): "The Book of Nehemiah",
    (OT, 17): "The Book of Esther",
    (OT, 18): "The Book of Job",
    (OT, 19): "The Book of Psalms",
    (OT, 20): "The Proverbs",
    (OT, 21): "Ecclesiastes, or, The Preacher",
    (OT, 22): "The Song of Solomon",
    (OT, 23): "The Book of the Prophet Isaiah",
    (OT, 24): "The Book of the Prophet Jeremiah",
    (OT, 25): "The Lamentations of Jeremiah",
    (OT, 26): "The Book of the Prophet Ezekiel",
    (OT, 27): "The Book of Daniel",
    (NT,  1): "The Gospel According to Saint Matthew",
    (NT,  2): "The Gospel According to Saint Mark",
    (NT,  3): "The Gospel According to Saint Luke",
    (NT,  4): "The Gospel According to Saint John",
    (NT,  6): "The Epistle of Paul the Apostle to the Romans",
    (NT,  7): "The First Epistle of Paul the Apostle to the Corinthians",
    (NT,  8): "The Second Epistle of Paul the Apostle to the Corinthians",
    (NT,  9): "The Epistle of Paul the Apostle to the Galatians",
    (NT, 10): "The Epistle of Paul the Apostle to the Ephesians",
    (NT, 11): "The Epistle of Paul the Apostle to the Philippians",
    (NT, 12): "The Epistle of Paul the Apostle to the Colossians",
    (NT, 13): "The First Epistle of Paul the Apostle to the Thessalonians",
    (NT, 14): "The Second Epistle of Paul the Apostle to the Thessalonians",
    (NT, 15): "The First Epistle of Paul the Apostle to Timothy",
    (NT, 16): "The Second Epistle of Paul the Apostle to Timothy",
    (NT, 17): "The Epistle of Paul the Apostle to Titus",
    (NT, 18): "The Epistle of Paul the Apostle to Philemon",
    (NT, 19): "The Epistle of Paul the Apostle to the Hebrews",
    (NT, 20): "The General Epistle of James",
    (NT, 21): "The First Epistle General of Peter",
    (NT, 22): "The Second General Epistle of Peter",
    (NT, 23): "The First Epistle General of John",
    (NT, 24): "The Second Epistle General of John",
    (NT, 25): "The Third Epistle General of John",
    (NT, 26): "The General Epistle of Jude",
    (NT, 27): "The Revelation of Saint John the Divine",
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def clean_text(text: str, decode_html: bool, strip_tags: bool = False) -> str:
    text = text.strip()
    if text.endswith('~'):
        text = text[:-1].rstrip()
    if decode_html:
        # Replace poetic line-break tags with a space before decoding entities
        # (<BR> marks cola/stichoi in Vulgate poetry; source file preserves originals)
        text = text.replace('<BR>', ' ').replace('<br>', ' ')
        # Strip all other XML-style markup tags (acrostic letters, speaker labels,
        # prologue markers, etc.) — keep only the text content between them.
        # Examples: <Prologus>, <Aleph>, <Sponsa>, <Chorus Fratrum>
        text = re.sub(r'<(?!BR\b|br\b)[^>]*>', '', text)
        # Strip stanza-wrapper brackets used in Vulgate psalms/poetry
        # ([ opens a psalm, ] closes it; not editorial additions)
        text = text.replace('[', '').replace(']', '')
        text = html.unescape(text)
    elif strip_tags:
        # Strip HTML/XML markup (e.g. <font color>, <sup>) but leave text content intact
        text = re.sub(r'<[^>]*>', '', text)
        text = html.unescape(text)
    # Collapse any runs of whitespace introduced by the replacements above
    text = re.sub(r'  +', ' ', text).strip()
    return text


def create_schema(cur):
    cur.executescript("""
        CREATE TABLE Books(
            TestamentID INT,
            BookID      INT,
            ShortName   TEXT,
            LongName    TEXT
        );
        CREATE TABLE Bible(
            TestamentID  INT,
            BookID       INT,
            ChapterID    INT,
            VerseID      INT,
            Index_       TEXT,
            IndexTypeID  INT,
            NumParagraphs INT,
            Passage1     TEXT,
            Passage2     TEXT
        );
        CREATE TABLE BookStats(
            TestamentID INT,
            BookID      INT,
            NumChapters INT
        );
        CREATE TABLE ChapterStats(
            TestamentID INT,
            BookID      INT,
            ChapterID   INT,
            NumVerses   INT
        );
    """)


def create_indexes(cur):
    cur.executescript("""
        CREATE INDEX idx_bible   ON Bible(TestamentID, BookID, ChapterID, VerseID);
        CREATE INDEX idx_bstats  ON BookStats(TestamentID, BookID);
        CREATE INDEX idx_cstats  ON ChapterStats(TestamentID, BookID, ChapterID);
    """)


# ---------------------------------------------------------------------------
# Core converter
# ---------------------------------------------------------------------------

def build_db(src: Path, dst: Path, decode_html: bool = False, strip_tags: bool = False,
             long_name_overrides: dict = None):
    print(f"  {src.name}")

    verses = []          # (tid, bid, ch, v, text)
    unknown = set()

    with open(src, encoding='utf-8', errors='replace') as f:
        for line in f:
            parts = line.split('|', 3)
            if len(parts) < 4:
                continue
            code, ch_s, v_s, text = parts[0].strip(), parts[1].strip(), parts[2].strip(), parts[3]
            if not code or not ch_s.isdigit() or not v_s.isdigit():
                continue
            if code not in BOOK_MAP:
                unknown.add(code)
                continue
            tid, bid, _, _ = BOOK_MAP[code]
            text = clean_text(text, decode_html, strip_tags)
            if text:
                verses.append((tid, bid, int(ch_s), int(v_s), text))

    if unknown:
        print(f"    Skipped unknown codes: {sorted(unknown)}")
    if not verses:
        print(f"    No verses found, skipping.")
        return

    # A chapter=0 / verse=0 entry is a book prologue heading; fold it into
    # chapter 1 so the normalization step doesn't create an extra chapter offset.
    # (Sirach's ch=0 prologue has real verses 1-36 and is intentionally left as
    # its own chapter by the normalization below.)
    verses = [(tid, bid, 1 if (ch == 0 and v == 0) else ch, v, text)
              for tid, bid, ch, v, text in verses]

    # Normalize chapter numbers to be sequential starting from 1 for each book.
    # Some source files use non-1-based chapter numbering (e.g. Additions to
    # Esther chapters 10-16, Epistle of Jeremiah chapter 6 only).
    book_orig_chs: dict = defaultdict(set)
    for tid, bid, ch, v, text in verses:
        book_orig_chs[(tid, bid)].add(ch)
    ch_remap: dict = {}  # (tid, bid, orig_ch) -> new_ch
    for (tid, bid), orig_set in book_orig_chs.items():
        for new_ch, orig_ch in enumerate(sorted(orig_set), 1):
            ch_remap[(tid, bid, orig_ch)] = new_ch
    if any(k[2] != v for k, v in ch_remap.items()):
        verses = [(tid, bid, ch_remap[(tid, bid, ch)], v, text)
                  for tid, bid, ch, v, text in verses]

    # Compute stats
    chapter_verses: dict = defaultdict(set)   # (tid,bid,ch) -> {verse_ids}
    for tid, bid, ch, v, _ in verses:
        chapter_verses[(tid, bid, ch)].add(v)

    book_chapters: dict = defaultdict(set)    # (tid,bid) -> {chapter_ids}
    for (tid, bid, ch) in chapter_verses:
        book_chapters[(tid, bid)].add(ch)

    used_books = set(book_chapters.keys())

    # Write database
    if dst.exists():
        dst.unlink()

    db = sqlite3.connect(str(dst))
    cur = db.cursor()
    create_schema(cur)

    # Books
    for (tid, bid) in sorted(used_books):
        short, long = BOOK_INFO.get((tid, bid), ("???", "Unknown"))
        if long_name_overrides:
            long = long_name_overrides.get((tid, bid), long)
        cur.execute("INSERT INTO Books VALUES (?,?,?,?)", (tid, bid, short, long))

    # Bible — one verse per row, Passage1 = "chapter:verse text"
    cur.executemany(
        "INSERT INTO Bible VALUES (?,?,?,?,NULL,NULL,0,?,NULL)",
        [(tid, bid, ch, v, f"{ch}:{v} {text}") for tid, bid, ch, v, text in sorted(verses)]
    )

    # BookStats
    cur.executemany(
        "INSERT INTO BookStats VALUES (?,?,?)",
        [(tid, bid, max(chs)) for (tid, bid), chs in sorted(book_chapters.items())]
    )

    # ChapterStats
    cur.executemany(
        "INSERT INTO ChapterStats VALUES (?,?,?,?)",
        [(tid, bid, ch, len(vset)) for (tid, bid, ch), vset in sorted(chapter_verses.items())]
    )

    create_indexes(cur)
    db.commit()
    db.close()

    total_chapters = sum(len(chs) for chs in book_chapters.values())
    print(f"    -> {dst.name}  ({len(used_books)} books, {total_chapters} chapters, {len(verses)} verses)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

CONVERSIONS = [
    # (source file,        output db,          decode_html, strip_tags,  long_name_overrides)
    ("kjvdat.txt",         "KJV.db",            False,       False,       KJV_LONG_NAMES),
    ("sept.txt",           "Septuagint.db",     False,       False,       None),
    ("vuldat.txt",         "Vulgate.db",        True,        False,       None),
    ("ugntdat.txt",        "UGNT.db",           False,       False,       None),
    ("tanakh_utf.dat",     "Tanakh.db",         False,       True,        None),
    ("apodat.txt",         "Apocrypha.db",      False,       False,       None),
]

if __name__ == "__main__":
    print(f"Converting Bible texts in {BIBLES_DIR}\n")
    for src_name, dst_name, decode_html, strip_tags, long_names in CONVERSIONS:
        src = BIBLES_DIR / src_name
        dst = BIBLES_DIR / dst_name
        if not src.exists():
            print(f"  Skipping {src_name} (not found)")
            continue
        build_db(src, dst, decode_html, strip_tags, long_names)
    print("\nDone.")
