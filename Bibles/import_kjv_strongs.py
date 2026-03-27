#!/usr/bin/env python3
"""
Import KJV text with Strong's numbers into a word-tag database.

Input:  kjv-strongsnumbers_ot.txt  (OT)
        Format: Book_CC_VV [¶] text with <NNNNN translit> tags
        Numbers are 5-digit zero-padded Hebrew.

        kjv-strongsnumbers_nt.txt  (NT)
        Format: Book_CC_VV [¶] text with <NNNN> tags (no transliteration)
        Numbers are 1-4 digit Greek.

Output: kjv_strongs.db
Schema:
    WordTags(ShortName, Chapter, Verse, Pos, KJVWord, StrongsNum)
    VerseHTML(ShortName, Chapter, Verse, HTML)
    ShortName matches Books.ShortName in KJV.db (e.g. "1 Chr", "Dan").
    StrongsNum is "H<n>" for OT, "G<n>" for NT — matches strongs.db.
"""

import html as _html
import re
import sqlite3
from pathlib import Path

BIBLES_DIR = Path(__file__).parent
OT_FILE    = BIBLES_DIR / "kjv-strongsnumbers_ot.txt"
NT_FILE    = BIBLES_DIR / "kjv-strongsnumbers_nt.txt"
OUT_DB     = BIBLES_DIR / "kjv_strongs.db"

# ---------------------------------------------------------------------------
# Book code mapping: file abbreviation -> (ShortName in KJV.db, testament)
# testament: "H" = OT/Hebrew, "G" = NT/Greek
# ---------------------------------------------------------------------------
BOOK_MAP = {
    # Old Testament
    "Gen": ("Gen",    "H"), "Exo": ("Exo",   "H"), "Lev": ("Lev",   "H"),
    "Num": ("Num",    "H"), "Deu": ("Deu",   "H"), "Jos": ("Jos",   "H"),
    "Jdg": ("Jdg",   "H"), "Jud": ("Jdg",   "H"), "Rut": ("Rut",   "H"),
    "1Sa": ("1 Sam", "H"), "2Sa": ("2 Sam", "H"), "1Ki": ("1 Kgs", "H"),
    "2Ki": ("2 Kgs", "H"), "1Ch": ("1 Chr", "H"), "2Ch": ("2 Chr", "H"),
    "Ezr": ("Ezr",   "H"), "Neh": ("Neh",   "H"), "Est": ("Est",   "H"),
    "Job": ("Job",   "H"), "Psa": ("Psa",   "H"), "Pro": ("Pro",   "H"),
    "Ecc": ("Ecc",   "H"), "Sol": ("Sol",   "H"), "SoS": ("Sol",   "H"),
    "Son": ("Sol",   "H"), "Isa": ("Isa",   "H"), "Jer": ("Jer",   "H"),
    "Lam": ("Lam",   "H"), "Eze": ("Eze",   "H"), "Dan": ("Dan",   "H"),
    "Hos": ("Hos",   "H"), "Joe": ("Joe",   "H"), "Amo": ("Amo",   "H"),
    "Oba": ("Oba",   "H"), "Jon": ("Jon",   "H"), "Mic": ("Mic",   "H"),
    "Nah": ("Nah",   "H"), "Hab": ("Hab",   "H"), "Zep": ("Zep",   "H"),
    "Zph": ("Zep",   "H"), "Hag": ("Hag",   "H"), "Zac": ("Zac",   "H"),
    "Zec": ("Zac",   "H"), "Mal": ("Mal",   "H"),
    # New Testament
    "Mat": ("Mat",   "G"), "Mtt": ("Mat",   "G"), "Mar": ("Mar",   "G"),
    "Mrk": ("Mar",   "G"), "Luk": ("Luk",   "G"), "Lke": ("Luk",   "G"),
    "Joh": ("Joh",   "G"), "Jhn": ("Joh",   "G"), "Act": ("Act",   "G"),
    "Rom": ("Rom",   "G"), "1Co": ("1 Cor", "G"), "2Co": ("2 Cor", "G"),
    "Gal": ("Gal",   "G"), "Eph": ("Eph",   "G"), "Phi": ("Phi",   "G"),
    "Php": ("Phi",   "G"), "Col": ("Col",   "G"), "1Th": ("1 The", "G"),
    "2Th": ("2 The", "G"), "1Ti": ("1 Tim", "G"), "2Ti": ("2 Tim", "G"),
    "Tit": ("Tit",   "G"), "Phm": ("Plm",   "G"), "Heb": ("Heb",   "G"),
    "Jam": ("Jam",   "G"), "1Pe": ("1 Pet", "G"), "2Pe": ("2 Pet", "G"),
    "1Jo": ("1 Joh", "G"), "2Jo": ("2 Joh", "G"), "3Jo": ("3 Joh", "G"),
    "Jde": ("Jde",   "G"), "Rev": ("Rev",   "G"),
}

# ---------------------------------------------------------------------------
# Regexes
# ---------------------------------------------------------------------------

# OT: <NNNNN[.n] content> — closing > is first > NOT followed by a
# letter/symbol (aleph character mid-transliteration).
OT_TAG_RE = re.compile(r'<(\d{5}(?:\.\d+)?)(?:\s(?:[^>]|>(?=[a-zA-Z<>@_]))*)?>')

# OT null/short refs like <00> that aren't real entries (fewer than 5 digits)
OT_SHORT_RE = re.compile(r'<\d{1,4}(?:\.\d+)?>')

# NT: <NNNN> — simple digit-only tags, no transliteration
NT_TAG_RE = re.compile(r'<(\d+)>')

# Strips verb form codes like (8804) / (5719) that appear after tags
VERB_CODE_RE  = re.compile(r'\(\d{4}\)')
# Collapses multiple spaces left behind after removals
MULTI_SP_RE   = re.compile(r' {2,}')
# Strips leading punctuation/whitespace from a segment
LEAD_STRIP_RE = re.compile(r'^[\s,;:()\u00b6\u00a0*]+')
# Removes space immediately before closing punctuation in finished HTML
PUNCT_SP_RE   = re.compile(r'\s+([,;:.!?)\u2018\u2019\'"])')


# ---------------------------------------------------------------------------
# Parsing helpers
# ---------------------------------------------------------------------------

def _preprocess(raw_text: str, lang: str) -> str:
    """Strip null refs and verb codes; normalise whitespace."""
    if lang == 'H':
        text = OT_SHORT_RE.sub("", raw_text)
    else:
        # NT: strip <0> null refs only (num == 0)
        text = NT_TAG_RE.sub(lambda m: "" if m.group(1) == "0" else m.group(0), raw_text)
    text = VERB_CODE_RE.sub("", text)
    text = text.replace("¶", "").replace("\xa0", " ")
    return MULTI_SP_RE.sub(" ", text).strip()


def _tag_re(lang: str):
    return OT_TAG_RE if lang == 'H' else NT_TAG_RE


def parse_verse_text(text: str, lang: str) -> list[tuple[str, str]]:
    """
    Parse a verse text and return [(strongs_num, kjv_word), ...].
    kjv_word is the word immediately preceding each tag.
    """
    text = _preprocess(text, lang)
    results = []
    last_end = 0

    for m in _tag_re(lang).finditer(text):
        num = int(m.group(1).split('.')[0])
        strongs_num = f"{lang}{num}"
        segment = text[last_end:m.start()]
        last_end = m.end()

        word = LEAD_STRIP_RE.sub("", segment).rstrip()
        word = word.replace("¶", "").strip()
        results.append((strongs_num, word))

    return results


def build_verse_html(raw_text: str, lang: str) -> str:
    """
    Build annotated HTML for a verse.
    Each tagged word becomes <span data-s="H430">word</span>.
    Only the last word of a segment is wrapped.
    """
    text = _preprocess(raw_text, lang)

    parts = []
    last_end = 0

    for m in _tag_re(lang).finditer(text):
        num = int(m.group(1).split('.')[0])
        strongs_num = f"{lang}{num}"
        segment = text[last_end:m.start()]
        last_end = m.end()

        # Separate leading punctuation/whitespace from content
        stripped = LEAD_STRIP_RE.sub("", segment)
        leading  = segment[:len(segment) - len(stripped)]
        content  = stripped.rstrip()
        trailing = stripped[len(content):]

        parts.append(_html.escape(leading))

        if content:
            # Wrap only the last space-separated token in the span
            space = content.rfind(' ')
            if space >= 0:
                parts.append(_html.escape(content[:space + 1]))
                word = content[space + 1:]
            else:
                word = content

            parts.append(f'<span data-s="{strongs_num}">{_html.escape(word)}</span>')

        parts.append(_html.escape(trailing))

    # Any text after the last tag (closing punctuation, etc.)
    parts.append(_html.escape(text[last_end:]))

    result = ''.join(parts)
    result = PUNCT_SP_RE.sub(r'\1', result)
    result = MULTI_SP_RE.sub(' ', result)
    return result


# ---------------------------------------------------------------------------
# DB schema
# ---------------------------------------------------------------------------

def create_schema(conn: sqlite3.Connection) -> None:
    conn.executescript("""
        DROP TABLE IF EXISTS WordTags;
        DROP TABLE IF EXISTS VerseHTML;

        CREATE TABLE WordTags (
            ShortName  TEXT    NOT NULL,
            Chapter    INTEGER NOT NULL,
            Verse      INTEGER NOT NULL,
            Pos        INTEGER NOT NULL,
            KJVWord    TEXT,
            StrongsNum TEXT    NOT NULL,
            PRIMARY KEY (ShortName, Chapter, Verse, Pos)
        );

        CREATE TABLE VerseHTML (
            ShortName  TEXT    NOT NULL,
            Chapter    INTEGER NOT NULL,
            Verse      INTEGER NOT NULL,
            HTML       TEXT    NOT NULL,
            PRIMARY KEY (ShortName, Chapter, Verse)
        );

        CREATE INDEX idx_wt_verse   ON WordTags(ShortName, Chapter, Verse);
        CREATE INDEX idx_wt_strongs ON WordTags(StrongsNum);
        CREATE INDEX idx_vh_chapter ON VerseHTML(ShortName, Chapter);
    """)


# ---------------------------------------------------------------------------
# File processing
# ---------------------------------------------------------------------------

def process_file(path: Path, conn: sqlite3.Connection,
                 tag_rows: list, html_rows: list,
                 skipped_books: set) -> tuple[int, int]:
    """Read one source file and append rows; flush batches to DB. Returns (verses, tags)."""
    verse_count = 0
    tag_count   = 0

    with open(path, encoding="cp1252") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue

            # Format: Book_CC_VV text...
            space = line.index(" ")
            ref   = line[:space]
            text  = line[space + 1:]

            parts = ref.split("_")
            if len(parts) != 3:
                continue

            book_code, ch_str, vs_str = parts
            try:
                chapter = int(ch_str)
                verse   = int(vs_str)
            except ValueError:
                continue

            if book_code not in BOOK_MAP:
                skipped_books.add(book_code)
                continue

            short_name, lang = BOOK_MAP[book_code]
            tags  = parse_verse_text(text, lang)
            vhtml = build_verse_html(text, lang)

            verse_count += 1
            for pos, (strongs_num, word) in enumerate(tags, start=1):
                tag_rows.append((short_name, chapter, verse, pos, word, strongs_num))
                tag_count += 1

            html_rows.append((short_name, chapter, verse, vhtml))

            if len(tag_rows) >= 10_000:
                conn.executemany("INSERT OR IGNORE INTO WordTags VALUES (?,?,?,?,?,?)", tag_rows)
                tag_rows.clear()
            if len(html_rows) >= 5_000:
                conn.executemany("INSERT OR IGNORE INTO VerseHTML VALUES (?,?,?,?)", html_rows)
                html_rows.clear()

    return verse_count, tag_count


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    print("KJV Strong's Numbers Importer")
    print("=" * 40)

    missing = [p for p in (OT_FILE, NT_FILE) if not p.exists()]
    if missing:
        for p in missing:
            print(f"ERROR: {p} not found.")
        return

    if OUT_DB.exists():
        OUT_DB.unlink()

    conn = sqlite3.connect(OUT_DB)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    create_schema(conn)

    tag_rows      = []
    html_rows     = []
    skipped_books = set()
    total_verses  = 0
    total_tags    = 0

    for path, label in [(OT_FILE, "OT"), (NT_FILE, "NT")]:
        v, t = process_file(path, conn, tag_rows, html_rows, skipped_books)
        print(f"  {label}: {v:,} verses, {t:,} tags")
        total_verses += v
        total_tags   += t

    if tag_rows:
        conn.executemany("INSERT OR IGNORE INTO WordTags VALUES (?,?,?,?,?,?)", tag_rows)
    if html_rows:
        conn.executemany("INSERT OR IGNORE INTO VerseHTML VALUES (?,?,?,?)", html_rows)

    conn.commit()
    conn.close()

    if skipped_books:
        print(f"  Warning: unrecognised book codes skipped: {sorted(skipped_books)}")

    size_kb = OUT_DB.stat().st_size // 1024
    print(f"  Total: {total_verses:,} verses, {total_tags:,} tags")
    print(f"\nDone. {OUT_DB} ({size_kb:,} KB)")


if __name__ == "__main__":
    main()
