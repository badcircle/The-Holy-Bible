#!/usr/bin/env python3
"""
Import Strong's Hebrew and Greek dictionaries into a SQLite database.

Source: https://github.com/openscriptures/strongs
License: CC-BY-SA (Open Scriptures)

Output: Bibles/strongs.db
Schema:
    Strongs(StrongsNum, Lang, Num, Lemma, Translit, Pronunciation, Derivation, StrongsDef, KJVDef)
"""

import json
import re
import sqlite3
import urllib.request
from pathlib import Path

BIBLES_DIR = Path(__file__).parent
OUT_DB = BIBLES_DIR / "strongs.db"

HEBREW_URL = "https://raw.githubusercontent.com/openscriptures/strongs/master/hebrew/strongs-hebrew-dictionary.js"
GREEK_URL  = "https://raw.githubusercontent.com/openscriptures/strongs/master/greek/strongs-greek-dictionary.js"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def fetch_strongs_js(url: str) -> dict:
    """Download a strongs-*-dictionary.js file and return the parsed dict."""
    print(f"  Fetching {url} ...")
    with urllib.request.urlopen(url) as resp:
        text = resp.read().decode("utf-8")

    # Strip the JS variable wrapper: `var strongsXxxDictionary = { ... };`
    # Everything from the first '{' to the last '}'
    start = text.index("{")
    end   = text.rindex("}") + 1
    return json.loads(text[start:end])


def create_schema(conn: sqlite3.Connection) -> None:
    conn.executescript("""
        DROP TABLE IF EXISTS Strongs;

        CREATE TABLE Strongs (
            StrongsNum    TEXT    PRIMARY KEY,
            Lang          TEXT    NOT NULL,
            Num           INTEGER NOT NULL,
            Lemma         TEXT,
            Translit      TEXT,
            Pronunciation TEXT,
            Derivation    TEXT,
            StrongsDef    TEXT,
            KJVDef        TEXT
        );

        CREATE INDEX idx_strongs ON Strongs(Lang, Num);
    """)


def insert_entries(conn: sqlite3.Connection, data: dict, lang: str) -> int:
    rows = []
    for key, entry in data.items():
        # key is e.g. "H1" or "G1615"
        num_str = key[1:]
        try:
            num = int(num_str)
        except ValueError:
            print(f"  Warning: unexpected key {key!r}, skipping")
            continue

        # Hebrew uses xlit + pron; Greek uses translit
        translit = entry.get("xlit") or entry.get("translit") or ""
        pron     = entry.get("pron", "")

        rows.append((
            key,
            lang,
            num,
            entry.get("lemma", ""),
            translit,
            pron,
            entry.get("derivation", ""),
            entry.get("strongs_def", "").strip(),
            entry.get("kjv_def", "").strip(),
        ))

    conn.executemany(
        "INSERT INTO Strongs VALUES (?,?,?,?,?,?,?,?,?)",
        rows,
    )
    return len(rows)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    print("Strong's Concordance Importer")
    print("=" * 40)

    print("\nDownloading dictionaries...")
    hebrew = fetch_strongs_js(HEBREW_URL)
    greek  = fetch_strongs_js(GREEK_URL)

    if OUT_DB.exists():
        OUT_DB.unlink()

    print(f"\nBuilding {OUT_DB.name} ...")
    conn = sqlite3.connect(OUT_DB)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")

    create_schema(conn)

    n_heb = insert_entries(conn, hebrew, "H")
    print(f"  Hebrew entries: {n_heb:,}")

    n_grk = insert_entries(conn, greek, "G")
    print(f"  Greek entries:  {n_grk:,}")

    conn.commit()
    conn.close()

    size_kb = OUT_DB.stat().st_size // 1024
    print(f"\nDone. {OUT_DB} ({size_kb:,} KB, {n_heb + n_grk:,} total entries)")


if __name__ == "__main__":
    main()
