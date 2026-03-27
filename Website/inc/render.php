<?php
// ============================================================
// The rendering helpers — fashioning HTML from the verse data
// ============================================================

// ---------------------------------------------------------------------------
// book_display_name() — canonical book names, ported from book_names.h.
// For unknown IDs, it falleth back to the DB's LongName.
// ---------------------------------------------------------------------------
function book_display_name(int $tid, int $book_id): ?string {
    static $OT = [
         1 => 'Genesis',          2 => 'Exodus',            3 => 'Leviticus',         4 => 'Numbers',
         5 => 'Deuteronomy',      6 => 'Joshua',            7 => 'Judges',            8 => 'Ruth',
         9 => '1 Samuel',        10 => '2 Samuel',         11 => '1 Kings',          12 => '2 Kings',
        13 => '1 Chronicles',    14 => '2 Chronicles',     15 => 'Ezra',             16 => 'Nehemiah',
        17 => 'Esther',          18 => 'Job',              19 => 'Psalms',           20 => 'Proverbs',
        21 => 'Ecclesiastes',    22 => 'Song of Solomon',  23 => 'Isaiah',           24 => 'Jeremiah',
        25 => 'Lamentations',    26 => 'Ezekiel',          27 => 'Daniel',           28 => 'Hosea',
        29 => 'Joel',            30 => 'Amos',             31 => 'Obadiah',          32 => 'Jonah',
        33 => 'Micah',           34 => 'Nahum',            35 => 'Habakkuk',         36 => 'Zephaniah',
        37 => 'Haggai',          38 => 'Zechariah',        39 => 'Malachi',
    ];
    static $NT = [
         1 => 'Matthew',          2 => 'Mark',              3 => 'Luke',              4 => 'John',
         5 => 'Acts',             6 => 'Romans',            7 => '1 Corinthians',     8 => '2 Corinthians',
         9 => 'Galatians',       10 => 'Ephesians',        11 => 'Philippians',      12 => 'Colossians',
        13 => '1 Thessalonians', 14 => '2 Thessalonians',  15 => '1 Timothy',        16 => '2 Timothy',
        17 => 'Titus',           18 => 'Philemon',         19 => 'Hebrews',          20 => 'James',
        21 => '1 Peter',         22 => '2 Peter',          23 => '1 John',           24 => '2 John',
        25 => '3 John',          26 => 'Jude',             27 => 'Revelation',
    ];
    static $DC = [
         1 => 'Tobit',            2 => 'Judith',            3 => 'Additions to Esther', 4 => 'Wisdom',
         5 => 'Sirach',           6 => 'Baruch',            7 => 'Epistle of Jeremiah',  8 => 'Susanna',
         9 => 'Bel and the Dragon',10 => '1 Maccabees',    11 => '2 Maccabees',        12 => '3 Maccabees',
        13 => '4 Maccabees',     14 => '1 Esdras',         15 => '2 Esdras',           16 => 'Prayer of Manasseh',
        17 => 'Prayer of Azariah',18 => 'Odes',            19 => 'Psalms of Solomon',  20 => 'Joshua (A Text)',
        21 => 'Judges (B Text)', 22 => 'Laodiceans',
    ];
    if ($tid === 1) return $OT[$book_id] ?? null;
    if ($tid === 2) return $NT[$book_id] ?? null;
    if ($tid === 3) return $DC[$book_id] ?? null;
    return null;
}

// ---------------------------------------------------------------------------
// nav_url() — buildeth a seemly URL for the given chapter
// ---------------------------------------------------------------------------
function nav_url(string $trans, int $tid, int $book, int $ch): string {
    return BASE_PATH . strtolower($trans) . '/' . $tid . '/' . $book . '/' . $ch;
}

// ---------------------------------------------------------------------------
// render_orig_col() — rendereth the original-language text for the second column.
// Verse numbers bear only data-v; the onclick is delegated from #reader-orig.
// ---------------------------------------------------------------------------
function render_orig_col(array $verses): string {
    if (!$verses) return '<p class="orig-empty">—</p>';

    $out     = '';
    $in_para = false;

    foreach ($verses as $v) {
        $vid  = (int)$v['verse_id'];
        $text = $v['text'];

        if ($v['paragraph_start']) {
            if ($in_para) $out .= "</p>\n";
            $out    .= '<p class="verse-para">';
            $in_para = true;
        } elseif (!$in_para) {
            $out    .= '<p class="verse-para">';
            $in_para = true;
        }

        if ($vid === 0) {
            if ($in_para) { $out .= "</p>\n"; $in_para = false; }
            $out .= '<h2 class="verse-heading">' . htmlspecialchars($text) . "</h2>\n";
            continue;
        }

        $out .= '<sup class="vnum" data-v="' . $vid . '">' . $vid . '</sup>'
              . '<span class="vtext">' . htmlspecialchars($text) . '</span> ';
    }

    if ($in_para) $out .= "</p>\n";
    return $out;
}

// ---------------------------------------------------------------------------
// render_interlinear_html() — the interlinear view: KJV with Strong's
// mouseovers atop the Greek, atop the Hebrew, verse by verse.
// Called alike for full-page load and for AJAX chapter navigation.
// ---------------------------------------------------------------------------
function render_interlinear_html(
    bool $db_ok, array $verses, string $book_title, int $chapter,
    int $tid, int $book_id,
    ?string $prev_url, ?string $next_url,
    array $verse_html = [], array $grk_verses = [], array $heb_verses = []
): string {
    $out = '';

    if (!$db_ok) {
        $out .= '<p class="error-msg">Could not open the database.</p>';
    } elseif (empty($verses)) {
        $out .= '<p class="error-msg">No verses found for this chapter.</p>';
    } else {
        // Index the parallel verses by verse_id, that they may be found swiftly
        $grk = [];
        foreach ($grk_verses as $v) {
            if ($v['verse_id'] > 0) $grk[(int)$v['verse_id']] = $v['text'];
        }
        $heb = [];
        foreach ($heb_verses as $v) {
            if ($v['verse_id'] > 0) $heb[(int)$v['verse_id']] = $v['text'];
        }

        $out .= '<h1 class="chapter-heading">' . $book_title
              . '<span class="chapter-label">Chapter ' . $chapter . "</span></h1>\n";

        foreach ($verses as $v) {
            $vid  = (int)$v['verse_id'];
            $text = $v['text'];

            if ($vid === 0) {
                $out .= '<h2 class="verse-heading">' . htmlspecialchars($text) . "</h2>\n";
                continue;
            }

            $onclick = 'onclick="loadParallel(' . $tid . ',' . $book_id . ',' . $chapter . ',' . $vid . ','
                     . htmlspecialchars(json_encode(book_display_name($tid, $book_id) ?? '')) . ')"';
            $eng = isset($verse_html[$vid]) ? $verse_html[$vid] : htmlspecialchars($text);

            $out .= '<div class="interlinear-verse">' . "\n"
                  . '  <span class="vnum interlinear-vnum" data-v="' . $vid . '" ' . $onclick . '>' . $vid . '</span>' . "\n"
                  . '  <div class="interlinear-body">' . "\n"
                  . '    <div class="interlinear-eng">' . $eng . '</div>' . "\n";

            if (isset($grk[$vid])) {
                $out .= '    <div class="interlinear-grk">' . htmlspecialchars($grk[$vid]) . '</div>' . "\n";
            }
            if (isset($heb[$vid])) {
                $out .= '    <div class="interlinear-heb" dir="rtl">' . htmlspecialchars($heb[$vid]) . '</div>' . "\n";
            }

            $out .= "  </div>\n"
                  . "</div>\n";
        }
    }

    if ($prev_url || $next_url) {
        $prev = $prev_url
            ? '<a href="' . $prev_url . '" class="nav-btn-lg nav-ajax">&lsaquo; Previous</a>'
            : '<span class="nav-btn-lg disabled"></span>';
        $next = $next_url
            ? '<a href="' . $next_url . '" class="nav-btn-lg nav-ajax">Next &rsaquo;</a>'
            : '<span class="nav-btn-lg disabled"></span>';
        $out .= '<div class="bottom-nav">' . $prev . $next . "</div>\n";
    }

    return $out;
}

// ---------------------------------------------------------------------------
// render_reader_html() — the inner content of <article.reader>.
// Called alike for full-page load and for AJAX chapter navigation.
// ---------------------------------------------------------------------------
function render_reader_html(
    bool $db_ok, array $verses, string $book_title, int $chapter,
    bool $is_kjv, bool $is_rtl, int $tid, int $book_id,
    ?string $prev_url, ?string $next_url, array $trans,
    bool $is_strongs = false, array $verse_html = []
): string {
    $out = '';

    if (!$db_ok) {
        $ext  = extension_loaded('pdo_sqlite') ? 'loaded' : 'NOT loaded';
        $out .= '<p class="error-msg">Could not open <strong>' . htmlspecialchars($trans['file']) . '</strong>.'
              . '<br>PDO SQLite: <code>' . $ext . '</code></p>';
    } elseif (empty($verses)) {
        $out .= '<p class="error-msg">No verses found for this chapter.</p>';
    } else {
        $out .= '<h1 class="chapter-heading">' . $book_title
              . '<span class="chapter-label">Chapter ' . $chapter . "</span></h1>\n";

        $in_para     = false;
        $first_verse = true;

        foreach ($verses as $v) {
            $vid  = $v['verse_id'];
            $text = $v['text'];

            if ($v['paragraph_start'] || $first_verse) {
                if ($in_para) $out .= "</p>\n";
                $out    .= '<p class="verse-para">';
                $in_para = true;
            }

            if ($vid === 0) {
                if ($in_para) { $out .= "</p>\n"; $in_para = false; }
                $out        .= '<h2 class="verse-heading">' . htmlspecialchars($text) . "</h2>\n";
                $first_verse = false;
                continue;
            }

            $onclick = 'onclick="loadParallel(' . $tid . ',' . $book_id . ',' . $chapter . ',' . $vid . ','
                     . htmlspecialchars(json_encode(book_display_name($tid, $book_id) ?? '')) . ')"';

            if ($is_strongs && isset($verse_html[$vid])) {
                $out .= '<sup class="vnum" data-v="' . $vid . '" ' . $onclick . '>' . $vid . '</sup>'
                      . '<span class="vtext">' . $verse_html[$vid] . '</span> ';
            } elseif ($is_kjv && $first_verse && $vid === 1 && $text !== '') {
                preg_match('/^./us', $text, $m);
                $dc_char = $m[0] ?? '';
                $out .= '<span class="dropcap-wrap" data-v="1">'
                      . '<span class="dropcap" title="Compare verse 1 across translations" ' . $onclick . '>'
                      . htmlspecialchars($dc_char) . '</span>'
                      . htmlspecialchars(substr($text, strlen($dc_char)))
                      . '</span>';
            } else {
                $out .= '<sup class="vnum" data-v="' . $vid . '" ' . $onclick . '>' . $vid . '</sup>'
                      . '<span class="vtext">' . htmlspecialchars($text) . '</span> ';
            }

            $first_verse = false;
        }

        if ($in_para) $out .= "</p>\n";
    }

    if ($prev_url || $next_url) {
        $prev = $prev_url
            ? '<a href="' . $prev_url . '" class="nav-btn-lg nav-ajax">&lsaquo; Previous</a>'
            : '<span class="nav-btn-lg disabled"></span>';
        $next = $next_url
            ? '<a href="' . $next_url . '" class="nav-btn-lg nav-ajax">Next &rsaquo;</a>'
            : '<span class="nav-btn-lg disabled"></span>';
        $out .= '<div class="bottom-nav">' . $prev . $next . "</div>\n";
    }

    return $out;
}
