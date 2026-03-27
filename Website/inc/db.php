<?php
// ============================================================
// The database helpers — for opening the scriptures and querying therein
// ============================================================

function open_db(string $path): ?PDO {
    if (!file_exists($path)) return null;
    try {
        $pdo = new PDO("sqlite:$path");
        $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $pdo->exec('PRAGMA cache_size = 4000;');
        $pdo->exec('PRAGMA temp_store = MEMORY;');
        return $pdo;
    } catch (Exception $e) {
        return null;
    }
}

// ---------------------------------------------------------------------------
// parse_passage() — a faithful rendering of the C++ version in bible_db.cpp.
//
// The DB layeth up verses as a single Passage1 string:
//   "1:1 In the beginning... 1:2 And the earth was..."
// One DB row may hold many consecutive verses, gathered as a paragraph.
// para_break=true marketh the first verse as the opener of a new paragraph.
//
// Returneth an array of ['verse_id', 'text', 'paragraph_start'].
// ---------------------------------------------------------------------------
function parse_passage(int $chapter, string $passage, bool $para_break): array {
    if ($passage === '') return [];

    $ch_prefix   = "{$chapter}:";
    $next_pat    = " {$chapter}:";
    $next_ch_pat = ' ' . ($chapter + 1) . ':';
    $plen_ch     = strlen($ch_prefix);
    $plen_next   = strlen($next_pat);
    $plen_nextch = strlen($next_ch_pat);
    $len         = strlen($passage);
    $p           = 0;
    $verses      = [];

    while ($p < $len) {
        if (substr($passage, $p, $plen_ch) !== $ch_prefix) break;
        $p += $plen_ch;

        // Parse out the verse number, being digits followed by a space
        $num_start = $p;
        while ($p < $len && $passage[$p] >= '0' && $passage[$p] <= '9') $p++;
        if ($p === $num_start || $p >= $len || $passage[$p] !== ' ') break;
        $verse_num = (int)substr($passage, $num_start, $p - $num_start);
        $p++;

        // Seek the end of this verse's text: the next verse-ref in this or the following chapter
        $text_start = $p;
        $text_end   = $len;

        for ($s = $p; $s < $len; $s++) {
            if (substr($passage, $s, $plen_next) === $next_pat) {
                $ne = $s + $plen_next; $ns = $ne;
                while ($ne < $len && $passage[$ne] >= '0' && $passage[$ne] <= '9') $ne++;
                if ($ne > $ns && $ne < $len && $passage[$ne] === ' ') { $text_end = $s; break; }
            }
            if (substr($passage, $s, $plen_nextch) === $next_ch_pat) {
                $ne = $s + $plen_nextch; $ns = $ne;
                while ($ne < $len && $passage[$ne] >= '0' && $passage[$ne] <= '9') $ne++;
                if ($ne > $ns && $ne < $len && $passage[$ne] === ' ') { $text_end = $s; break; }
            }
        }

        $text = substr($passage, $text_start, $text_end - $text_start);
        $text = str_replace(["\r\n", "\r", "\n"], ' ', $text);
        $text = trim($text);
        $text = preg_replace('/  +/', ' ', $text);

        $verses[] = [
            'verse_id'        => $verse_num,
            'text'            => $text,
            'paragraph_start' => ($verses === []) ? $para_break : false,
        ];

        if ($text_end >= $len) break;
        $p = $text_end + 1;
    }

    return $verses;
}

// ---------------------------------------------------------------------------
// get_books() — gather all books with the count of their chapters
// ---------------------------------------------------------------------------
function get_books(PDO $pdo): array {
    $stmt = $pdo->query(
        'SELECT b.TestamentID, b.BookID, b.ShortName, b.LongName,
                COALESCE(s.NumChapters, 0) AS NumChapters
         FROM Books b
         LEFT JOIN BookStats s ON s.TestamentID = b.TestamentID AND s.BookID = b.BookID
         ORDER BY b.TestamentID, b.BookID'
    );
    return $stmt ? $stmt->fetchAll(PDO::FETCH_ASSOC) : [];
}

// ---------------------------------------------------------------------------
// get_chapter() — a port of bible_db_get_chapter, with the look-back therein
// ---------------------------------------------------------------------------
function get_chapter(PDO $pdo, int $tid, int $book_id, int $chapter): array {
    $verses = [];

    // Look back: the last row of the previous chapter may overflow into this one
    if ($chapter > 1) {
        $stmt = $pdo->prepare(
            'SELECT Passage1 FROM Bible
             WHERE TestamentID=? AND BookID=? AND ChapterID=?
             ORDER BY VerseID DESC LIMIT 1'
        );
        $stmt->execute([$tid, $book_id, $chapter - 1]);
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        if ($row && $row['Passage1']) {
            $pos = strpos($row['Passage1'], " {$chapter}:");
            if ($pos !== false) {
                $verses = parse_passage($chapter, substr($row['Passage1'], $pos + 1), false);
            }
        }
    }

    $stmt = $pdo->prepare(
        'SELECT Passage1 FROM Bible
         WHERE TestamentID=? AND BookID=? AND ChapterID=?
         ORDER BY VerseID'
    );
    $stmt->execute([$tid, $book_id, $chapter]);

    $first_row = ($verses === []);
    while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
        foreach (parse_passage($chapter, (string)$row['Passage1'], !$first_row) as $v) {
            $verses[] = $v;
        }
        $first_row = false;
    }

    return $verses;
}
