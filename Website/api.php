<?php
// ============================================================
// api.php — Parallel verse JSON endpoint
//
// GET params: tid, book, ch, verse
// Returns: JSON array, one entry per translation:
//   [{ translation, label, text, rtl }, ...]
// ============================================================

$BIBLES_DIR = __DIR__ . '/Bibles';

$TRANSLATIONS = [
    'KJV'        => ['file' => 'KJV.db',        'label' => 'King James Version',  'rtl' => false, 'tids' => [1, 2]],
    'Septuagint' => ['file' => 'Septuagint.db', 'label' => 'Septuagint (LXX)',    'rtl' => false, 'tids' => [1]],
    'Vulgate'    => ['file' => 'Vulgate.db',     'label' => 'Latin Vulgate',       'rtl' => false, 'tids' => [1, 2]],
    'UGNT'       => ['file' => 'UGNT.db',        'label' => 'Greek New Testament', 'rtl' => false, 'tids' => [2]],
    'Tanakh'     => ['file' => 'Tanakh.db',      'label' => 'Hebrew Tanakh',       'rtl' => true,  'tids' => [1]],
    'Apocrypha'  => ['file' => 'Apocrypha.db',  'label' => 'Apocrypha',          'rtl' => false, 'tids' => [3]],
];

header('Content-Type: application/json; charset=utf-8');

// ---------------------------------------------------------------------------
// Validate input
// ---------------------------------------------------------------------------
$tid     = isset($_GET['tid'])   ? (int)$_GET['tid']   : 0;
$book_id = isset($_GET['book'])  ? (int)$_GET['book']  : 0;
$chapter = isset($_GET['ch'])    ? (int)$_GET['ch']    : 0;
$verse   = isset($_GET['verse']) ? (int)$_GET['verse'] : 0;

if ($tid <= 0 || $book_id <= 0 || $chapter <= 0 || $verse <= 0) {
    echo json_encode(['error' => 'Missing or invalid parameters.']);
    exit;
}

// ---------------------------------------------------------------------------
// parse_passage — same logic as index.php; returns flat array of verses
// ---------------------------------------------------------------------------
function parse_passage(int $chapter, string $passage, bool $para_break): array {
    $verses = [];
    if ($passage === '') return $verses;

    $ch_prefix   = "{$chapter}:";
    $next_pat    = " {$chapter}:";
    $next_ch_pat = ' ' . ($chapter + 1) . ':';
    $plen_ch     = strlen($ch_prefix);
    $plen_next   = strlen($next_pat);
    $plen_nextch = strlen($next_ch_pat);
    $len         = strlen($passage);
    $p           = 0;
    $first       = true;

    while ($p < $len) {
        if (substr($passage, $p, $plen_ch) !== $ch_prefix) break;
        $p += $plen_ch;

        $num_start = $p;
        while ($p < $len && $passage[$p] >= '0' && $passage[$p] <= '9') $p++;
        if ($p === $num_start || $p >= $len || $passage[$p] !== ' ') break;
        $verse_num = (int)substr($passage, $num_start, $p - $num_start);
        $p++;

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

        $verses[] = ['verse_id' => $verse_num, 'text' => $text];
        $first    = false;
        if ($text_end >= $len) break;
        $p = $text_end + 1;
    }

    return $verses;
}

// ---------------------------------------------------------------------------
// Fetch a single verse from a database file
// Returns the verse text, or empty string if not found / DB not available.
// ---------------------------------------------------------------------------
function fetch_verse(string $db_path, int $tid, int $book_id, int $chapter, int $verse_id): string {
    if (!file_exists($db_path)) return '';

    try {
        $pdo = new PDO("sqlite:$db_path");
        $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    } catch (Exception $e) {
        return '';
    }

    // Look-back: check previous chapter's last row for spill
    if ($chapter > 1) {
        $stmt = $pdo->prepare(
            "SELECT Passage1 FROM Bible
             WHERE TestamentID=? AND BookID=? AND ChapterID=?
             ORDER BY VerseID DESC LIMIT 1"
        );
        $stmt->execute([$tid, $book_id, $chapter - 1]);
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        if ($row && $row['Passage1']) {
            $search = " {$chapter}:";
            $pos    = strpos($row['Passage1'], $search);
            if ($pos !== false) {
                $overflow = substr($row['Passage1'], $pos + 1);
                foreach (parse_passage($chapter, $overflow, false) as $v) {
                    if ($v['verse_id'] === $verse_id) return $v['text'];
                }
            }
        }
    }

    // Find the DB row that should contain this verse (row with the highest
    // VerseID that is <= our target verse_id)
    $stmt = $pdo->prepare(
        "SELECT Passage1 FROM Bible
         WHERE TestamentID=? AND BookID=? AND ChapterID=? AND VerseID<=?
         ORDER BY VerseID DESC LIMIT 1"
    );
    $stmt->execute([$tid, $book_id, $chapter, $verse_id]);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

    // Also check the row immediately after (in case our verse spilled from it)
    $stmt2 = $pdo->prepare(
        "SELECT Passage1 FROM Bible
         WHERE TestamentID=? AND BookID=? AND ChapterID=? AND VerseID>?
         ORDER BY VerseID ASC LIMIT 3"
    );
    $stmt2->execute([$tid, $book_id, $chapter, $verse_id]);
    $rows_after = $stmt2->fetchAll(PDO::FETCH_ASSOC);

    foreach (array_merge($rows, $rows_after) as $row) {
        if (!$row['Passage1']) continue;
        foreach (parse_passage($chapter, $row['Passage1'], false) as $v) {
            if ($v['verse_id'] === $verse_id) return $v['text'];
        }
    }

    return '';
}

// ---------------------------------------------------------------------------
// Build result
// ---------------------------------------------------------------------------
$result = [];

foreach ($TRANSLATIONS as $key => $t) {
    // Only include translations that can contain this testament
    if (!in_array($tid, $t['tids'], true) && $tid !== 3) {
        // For Apocrypha books (tid=3) we only search Apocrypha.db
        if ($tid === 3 && $key !== 'Apocrypha') {
            $result[] = ['translation' => $key, 'label' => $t['label'], 'text' => '', 'rtl' => $t['rtl']];
            continue;
        }
    }
    if ($tid === 3 && $key !== 'Apocrypha') {
        $result[] = ['translation' => $key, 'label' => $t['label'], 'text' => '', 'rtl' => $t['rtl']];
        continue;
    }

    $db_path = $BIBLES_DIR . '/' . $t['file'];
    $text    = fetch_verse($db_path, $tid, $book_id, $chapter, $verse);
    $result[] = ['translation' => $key, 'label' => $t['label'], 'text' => $text, 'rtl' => $t['rtl']];
}

echo json_encode($result, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
