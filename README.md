# Bible Reader

A fast, clean desktop Bible reader for Windows built with C++, ImGui, and SQLite.

## Download

Grab the latest single-file `.exe` from the [Releases](https://github.com/badcircle/The-Holy-Bible/releases) page — no installer, no dependencies.

## Features

- Six translations: KJV, Septuagint (LXX), Vulgate, UGNT, Tanakh, Apocrypha
- Parallel tray — click any verse to compare it across all translations side by side
- Hebrew/RTL rendering for Tanakh (Noto Serif Hebrew, right-aligned)
- Multi-column layout that adapts to window width
- Font size controls (`A-` / `A+`, or `Ctrl +/-/0`)
- Keyboard navigation (`←` / `→` or `Page Up` / `Page Down` for chapters, `\` to toggle tray)
- Serif body font (Gentium), decorative headings (Pirata One)
- Custom frameless window with drag-to-move and minimize/maximize/close controls
- Single self-contained `.exe` — all databases, fonts, and images embedded

## Building from Source

### Prerequisites

- **Visual Studio 2022** (Desktop C++ workload)
- **CMake** (included with VS, or from cmake.org)
- **vcpkg** at `C:\vcpkg`:
  ```
  git clone https://github.com/microsoft/vcpkg C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
  ```

### Install dependencies

```bat
C:\vcpkg\vcpkg install sdl2:x64-windows sqlite3:x64-windows freetype:x64-windows stb:x64-windows
```

### Generate Bible databases

The `.db` files are not included in the repo. You need the raw source data files placed in `Bibles/`:

| File | Translation |
|---|---|
| `kjvdat.txt` | King James Version |
| `sept.txt` | Septuagint |
| `vuldat.txt` | Vulgate |
| `ugntdat.txt` | UGNT |
| `tanakh_utf.dat` | Tanakh |
| `apodat.txt` | Apocrypha |

Then run:
```
cd Bibles
python convert.py
```

This writes `KJV.db`, `Septuagint.db`, etc. into `Bibles/`. Copy (or symlink) them into `Application/`.

### Development build (loads .db files from disk)

```bat
cd Application
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

The exe lands at `Application\build\Release\BibleReader.exe` and expects `KJV.db` (and others) in the same directory.

### Ship build (fully self-contained single exe)

Requires the x64-windows-static vcpkg triplet:
```bat
C:\vcpkg\vcpkg install sdl2:x64-windows-static sqlite3:x64-windows-static freetype:x64-windows-static stb:x64-windows-static
```

Then:
```bat
cd Application
cmake -B build_ship -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DBR_SHIP=ON ^
  -DBR_STATIC_DIST=ON
cmake --build build_ship --config Release
```

Output: `Application\build_ship\Release\BibleReader.exe` (~36 MB, no external files needed).

## Project Structure

```
Bible/
├── Application/
│   ├── CMakeLists.txt
│   ├── assets/
│   │   ├── fonts/          OFL/Apache-licensed fonts (Gentium, Pirata One, etc.)
│   │   └── images/         Cover art, window icon
│   └── src/
│       ├── main.cpp        SDL2 + OpenGL3 + ImGui setup, font loading
│       ├── app.h / .cpp    UI layout, navigation, rendering
│       ├── bible_db.h/.cpp SQLite query layer
│       └── resources*.rc.in  Windows resource scripts
├── Bibles/
│   └── convert.py          Converts raw text files to SQLite databases
├── Libraries/
│   └── imgui-1.92.6/       Dear ImGui (FreeType backend)
└── README.md
```

## Tech Stack

| | |
|---|---|
| Windowing | SDL2 |
| Rendering | OpenGL 3.3 + Dear ImGui 1.92.6 |
| Font rendering | FreeType (via ImGui misc/freetype) |
| Database | SQLite3 (`sqlite3_deserialize` for embedded DBs) |
| Build | CMake + vcpkg |

## License

Source code: MIT

Fonts are distributed under their respective open licenses (SIL OFL / Apache 2.0). See `Application/assets/fonts/` for individual license files.

Bible translations are in the public domain (KJV, Septuagint, Vulgate) or open-licensed (UGNT: CC BY 4.0).
