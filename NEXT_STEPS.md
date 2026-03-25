# Bible Reader — Next Steps & Setup Guide

## Tech Stack

| Layer | Library | Why |
|---|---|---|
| Windowing / input | **SDL2** | Battle-tested, minimal, great on Windows |
| GPU / rendering | **OpenGL 3.3 Core** | GPU-accelerated, widely supported |
| UI | **ImGui 1.92.6** *(already have)* | Fast immediate-mode GUI |
| Font rendering | **FreeType** (via ImGui misc/freetype) | Subpixel-quality text |
| Database | **SQLite3** | Embedded, zero-config |
| Build system | **CMake + vcpkg** | Industry standard |

---

## 1. Install Prerequisites

### A. Visual Studio 2022 (Community is fine)
Download from: https://visualstudio.microsoft.com/downloads/
- Workload: **Desktop development with C++**
- This gives you MSVC, CMake support, and the Windows SDK.

### B. CMake (if not included with VS)
https://cmake.org/download/ — add to PATH during install.

### C. vcpkg (dependency manager)
Run these commands in a terminal (PowerShell or cmd):
```bat
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg integrate install
```

### D. Install dependencies via vcpkg
```bat
C:\vcpkg\vcpkg install sdl2:x64-windows
C:\vcpkg\vcpkg install sqlite3:x64-windows
C:\vcpkg\vcpkg install freetype:x64-windows
```

---

## 2. Build the Project

Open **Developer Command Prompt for VS 2022** (or VS's terminal), then:

```bat
cd C:\Users\wood\Projects\Experiments\Bible\Application

cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake

cmake --build build --config Release
```

The executable lands at:
```
Application\build\Release\BibleReader.exe
```

Bible.db is resolved at build time relative to `Application/`. If you move the exe, pass the DB path as the first argument:
```bat
BibleReader.exe "C:\path\to\Bible.db"
```
Or set the `BIBLE_DB` environment variable.

---

## 3. Fonts — Add for Best Results

The app renders beautifully with proper serif fonts. Drop `.ttf` files into:
```
Application\assets\fonts\
```

**Recommended (free):**

| Font | Use | Download |
|---|---|---|
| **EB Garamond** (`EBGaramond-Regular.ttf`, `EBGaramond-SemiBold.ttf`) | Body & headings | https://fonts.google.com/specimen/EB+Garamond |
| **Gentium Plus** | Excellent for biblical text, wide Unicode | https://software.sil.org/gentium/ |
| **Crimson Pro** | Clean, highly readable long-form | https://fonts.google.com/specimen/Crimson+Pro |
| **Roboto** (`Roboto-Regular.ttf`) | UI controls | https://fonts.google.com/specimen/Roboto |

Without these files, ImGui falls back to its built-in bitmap font (readable but not polished).

---

## 4. Keyboard Shortcuts (implemented)

| Key | Action |
|---|---|
| `←` / `→` | Previous / next chapter |
| `Page Up` / `Page Down` | Previous / next chapter |

---

## 5. Roadmap / What to Build Next

### High Priority
- [ ] **Chapter number input / jump-to** — type a chapter number directly
- [ ] **In-chapter verse jump** — click a verse number in the sidebar
- [ ] **Text search** — full-text search across all verses (SQLite FTS5)
- [ ] **Copy verse** — Ctrl+click copies "Book Ch:V text" to clipboard
- [ ] **Font size control** — `Ctrl++` / `Ctrl+-` to adjust reading size

### Rendering Quality
- [ ] **FreeType subpixel LCD hints** — enable `ImGuiFreeTypeBuilderFlags_LcdFilter` for
  screens that support it (see `imgui_freetype.h`)
- [ ] **Ligature / OpenType shaping** — HarfBuzz integration for proper glyph shaping
  (makes EB Garamond look its best with `fi`, `fl`, `st` ligatures)
- [ ] **Line spacing control** — expose `ImGuiStyle::ItemSpacing.y` as a setting
- [ ] **Paragraph flow mode** — render multiple verses as flowing paragraphs instead of
  one-per-line (use the `NumParagraphs` column already in the DB)

### Features
- [ ] **Bookmarks** — save/restore book+chapter+verse positions (write to SQLite)
- [ ] **Reading plan** — daily chapter schedule stored in DB
- [ ] **Notes margin** — side panel for user annotations per verse
- [ ] **Cross-references** — a separate `cross_refs` table pointing between verses
- [ ] **Multiple translations** — add a `version` column to Bible table; store ESV, NIV etc.
- [ ] **Red-letter** — color Jesus' words; requires a `red_letter` flag per verse in DB
- [ ] **Strong's numbers** — inline Hebrew/Greek word links (requires separate lexicon DB)

### Platform / Polish
- [ ] **System tray** — minimize to tray, quick-access hotkey
- [ ] **Window title** shows current reference ("Genesis 1 — Bible Reader")
- [ ] **Remember last position** — persist current book/chapter across sessions
- [ ] **Dark/light theme toggle**
- [ ] **Print / export to PDF** — render chapter to PDF via a simple layout pass

---

## 6. Project Structure

```
Bible/
├── Bible.db                        ← SQLite KJV database
├── NEXT_STEPS.md                   ← this file
├── Libraries/
│   └── imgui-1.92.6/               ← ImGui (already present)
└── Application/
    ├── CMakeLists.txt
    ├── assets/
    │   └── fonts/                  ← drop TTF files here
    └── src/
        ├── main.cpp                ← SDL2 + OpenGL3 + ImGui setup
        ├── bible_db.h / .cpp       ← SQLite query layer
        ├── app.h / .cpp            ← UI layout & navigation
        └── (future: search.cpp, bookmarks.cpp, ...)
```

---

## 7. Bible.db Schema Reference

```sql
-- Testament 1 = OT, 2 = NT
Books      (TestamentID, BookID, ShortName, LongName)
Bible      (TestamentID, BookID, ChapterID, VerseID, Index_,
            IndexTypeID, NumParagraphs, Passage1, Passage2)
BookStats  (TestamentID, BookID, NumChapters)
ChapterStats (TestamentID, BookID, ChapterID, NumVerses)
```

`Passage1` contains `"chapter:verse text"` — the code strips the reference prefix
automatically.

---

## 8. Adding FTS5 Search (optional quick win)

```sql
-- Run this once against Bible.db to create a search index
CREATE VIRTUAL TABLE Bible_fts USING fts5(
    Passage1,
    content=Bible,
    content_rowid=rowid
);
INSERT INTO Bible_fts(Bible_fts) VALUES('rebuild');
```

Then query:
```sql
SELECT b.BookID, b.ChapterID, b.VerseID, b.Passage1
FROM Bible b
JOIN Bible_fts f ON f.rowid = b.rowid
WHERE Bible_fts MATCH 'love one another'
ORDER BY rank;
```
