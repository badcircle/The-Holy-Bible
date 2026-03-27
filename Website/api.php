<?php
// ============================================================
// api.php — The parallel verse JSON endpoint
//
// GET params: tid, book, ch, verse
// Returneth a JSON array, one entry for each translation:
//   [{ translation, label, text, rtl, html?, strongs? }, ...]
// html and strongs are present only for translations bearing annotated markup (KJVPlus).
// ============================================================

require_once __DIR__ . '/inc/config.php';
require_once __DIR__ . '/inc/db.php';

header('Content-Type: application/json; charset=utf-8');

$tid     = isset($_GET['tid'])   ? (int)$_GET['tid']   : 0;
$book_id = isset($_GET['book'])  ? (int)$_GET['book']  : 0;
$chapter = isset($_GET['ch'])    ? (int)$_GET['ch']    : 0;
$verse   = isset($_GET['verse']) ? (int)$_GET['verse'] : 0;

if ($tid <= 0 || $book_id <= 0 || $chapter <= 0 || $verse <= 0) {
    echo json_encode(['error' => 'Missing or invalid parameters.']);
    exit;
}

// ---------------------------------------------------------------------------
// fetch_verse() — extracteth a single verse from the database
// ---------------------------------------------------------------------------
function fetch_verse(string $db_path, int $tid, int $book_id, int $chapter, int $verse_id): string {
    $pdo = open_db($db_path);
    if (!$pdo) return '';

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
                foreach (parse_passage($chapter, substr($row['Passage1'], $pos + 1), false) as $v) {
                    if ($v['verse_id'] === $verse_id) return $v['text'];
                }
            }
        }
    }

    // Seek the DB row that holdeth this verse (the highest VerseID not exceeding the target)
    $stmt = $pdo->prepare(
        'SELECT Passage1 FROM Bible
         WHERE TestamentID=? AND BookID=? AND ChapterID=? AND VerseID<=?
         ORDER BY VerseID DESC LIMIT 1'
    );
    $stmt->execute([$tid, $book_id, $chapter, $verse_id]);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

    // Also search the rows that follow, lest the verse have spilled from one of them
    $stmt2 = $pdo->prepare(
        'SELECT Passage1 FROM Bible
         WHERE TestamentID=? AND BookID=? AND ChapterID=? AND VerseID>?
         ORDER BY VerseID ASC LIMIT 3'
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
// fetch_strongs_verse() — the annotated HTML and Strong's definitions for a verse
// Returneth ['html' => string, 'strongs' => object|null]
// ---------------------------------------------------------------------------
function fetch_strongs_verse(string $bibles_dir, int $tid, int $book_id, int $chapter, int $verse): array {
    // Resolve the book's ShortName from the KJV database
    $kjv_pdo = open_db($bibles_dir . '/KJV.db');
    if (!$kjv_pdo) return ['html' => '', 'strongs' => null];

    $stmt = $kjv_pdo->prepare('SELECT ShortName FROM Books WHERE TestamentID=? AND BookID=? LIMIT 1');
    $stmt->execute([$tid, $book_id]);
    $sn = $stmt->fetchColumn();
    if (!$sn) return ['html' => '', 'strongs' => null];

    $ks_pdo = open_db($bibles_dir . '/KJV_strongs.db');
    if (!$ks_pdo) return ['html' => '', 'strongs' => null];

    $stmt = $ks_pdo->prepare('SELECT HTML FROM VerseHTML WHERE ShortName=? AND Chapter=? AND Verse=?');
    $stmt->execute([$sn, $chapter, $verse]);
    $html = $stmt->fetchColumn() ?: '';
    if (!$html) return ['html' => '', 'strongs' => null];

    // Fetch the definitions of all Strong's numbers found in this verse
    $s_pdo = open_db($bibles_dir . '/strongs.db');
    if (!$s_pdo) return ['html' => $html, 'strongs' => null];

    $stmt = $ks_pdo->prepare('SELECT StrongsNum FROM WordTags WHERE ShortName=? AND Chapter=? AND Verse=?');
    $stmt->execute([$sn, $chapter, $verse]);
    $nums = $stmt->fetchAll(PDO::FETCH_COLUMN);
    if (!$nums) return ['html' => $html, 'strongs' => null];

    $ph      = implode(',', array_fill(0, count($nums), '?'));
    $stmt    = $s_pdo->prepare(
        "SELECT StrongsNum, Lemma, Translit, Pronunciation, StrongsDef FROM Strongs WHERE StrongsNum IN ($ph)"
    );
    $stmt->execute($nums);
    $strongs = new stdClass();
    foreach ($stmt->fetchAll(PDO::FETCH_ASSOC) as $r) {
        $k           = $r['StrongsNum'];
        $strongs->$k = ['l' => $r['Lemma'], 't' => $r['Translit'] ?: $r['Pronunciation'], 'd' => $r['StrongsDef']];
    }

    return ['html' => $html, 'strongs' => $strongs];
}

// ---------------------------------------------------------------------------
// Build up the result, gathering from each translation in turn
// ---------------------------------------------------------------------------
$result = [];

foreach ($TRANSLATIONS as $key => $t) {
    if (!empty($t['interlinear'])) continue;
    if (!in_array($tid, $t['tids'], true)) {
        $result[] = ['translation' => $key, 'label' => $t['label'], 'text' => '', 'rtl' => $t['rtl']];
        continue;
    }
    $text = fetch_verse($BIBLES_DIR . '/' . $t['file'], $tid, $book_id, $chapter, $verse);
    $entry = ['translation' => $key, 'label' => $t['label'], 'text' => $text, 'rtl' => $t['rtl']];
    if (!empty($t['strongs'])) {
        $sv             = fetch_strongs_verse($BIBLES_DIR, $tid, $book_id, $chapter, $verse);
        $entry['html']    = $sv['html'];
        $entry['strongs'] = $sv['strongs'];
    }
    $result[] = $entry;
}

echo json_encode($result, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
