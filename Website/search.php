<?php
// ============================================================
// search.php — verse full-text search endpoint
//
// GET: q (query string), trans (translation key), tid (0=all)
// Returns JSON: { query, trans, total, capped, results[] }
//   Each result: { tid, bid, book, ch, v, text, url }
// ============================================================

require_once __DIR__ . '/inc/config.php';
require_once __DIR__ . '/inc/db.php';
require_once __DIR__ . '/inc/render.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

$q       = trim($_GET['q'] ?? '');
$trans_k = (isset($_GET['trans']) && array_key_exists($_GET['trans'], $TRANSLATIONS))
           ? $_GET['trans'] : 'KJV';
$tid_f   = isset($_GET['tid']) ? (int)$_GET['tid'] : 0;

// Interlinear is a view overlay — search its backing text (KJV) instead
if (!empty($TRANSLATIONS[$trans_k]['interlinear'])) $trans_k = 'KJV';
$trans = $TRANSLATIONS[$trans_k];

if (mb_strlen($q, 'UTF-8') < 3) {
    echo json_encode(['results' => [], 'total' => 0, 'query' => $q]);
    exit;
}

$pdo = open_db($BIBLES_DIR . '/' . $trans['file']);
if (!$pdo) {
    echo json_encode(['error' => 'Database unavailable', 'results' => [], 'total' => 0]);
    exit;
}

// Parse "quoted phrases" and bare words.
// Each term must appear in the verse — phrases as exact substrings, words individually.
$terms = [];
preg_match_all('/"([^"]+)"|(\S+)/u', $q, $matches, PREG_SET_ORDER);
foreach ($matches as $m) {
    $val = trim($m[1] !== '' ? $m[1] : $m[2], '"');
    if (mb_strlen($val, 'UTF-8') >= 2) $terms[] = $val;
}
if (!$terms) {
    echo json_encode(['results' => [], 'total' => 0, 'query' => $q]);
    exit;
}

// ---------------------------------------------------------------------------
// Build the SQL WHERE clause
// ---------------------------------------------------------------------------
$conds  = [];
$params = [];

// Testament filter — restrict to those covered by this translation
if ($tid_f > 0 && in_array($tid_f, $trans['tids'], true)) {
    $conds[]  = 'b.TestamentID = ?';
    $params[] = $tid_f;
} else {
    $ph       = implode(',', array_fill(0, count($trans['tids']), '?'));
    $conds[]  = "b.TestamentID IN ($ph)";
    foreach ($trans['tids'] as $t) $params[] = $t;
}

// A LIKE condition for each term (phrases match as exact substrings; words individually)
foreach ($terms as $term) {
    $esc      = str_replace(['\\', '%', '_'], ['\\\\', '\\%', '\\_'], $term);
    $conds[]  = "b.Passage1 LIKE ? ESCAPE '\\'";
    $params[] = '%' . $esc . '%';
}

// ---------------------------------------------------------------------------
// Fetch matching paragraph rows and parse them verse-by-verse
// ---------------------------------------------------------------------------
$SQL_LIMIT = 2000;
$params[]  = $SQL_LIMIT;

$stmt = $pdo->prepare(
    'SELECT b.TestamentID, b.BookID, b.ChapterID, b.Passage1,
            bk.LongName AS BookLong
     FROM Bible b
     JOIN Books bk ON bk.TestamentID = b.TestamentID AND bk.BookID = b.BookID
     WHERE ' . implode(' AND ', $conds) . '
     ORDER BY b.TestamentID, b.BookID, b.ChapterID, b.VerseID
     LIMIT ?'
);
$stmt->execute($params);
$rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

$SHOW_LIMIT  = 200;
$results     = [];
$total       = 0;
$sql_limited   = count($rows) >= $SQL_LIMIT;
$terms_lower   = array_map(fn($t) => mb_strtolower($t, 'UTF-8'), $terms);

foreach ($rows as $row) {
    $tid      = (int)$row['TestamentID'];
    $book_id  = (int)$row['BookID'];
    $chapter  = (int)$row['ChapterID'];
    $book_name = book_display_name($tid, $book_id) ?? $row['BookLong'];

    foreach (parse_passage($chapter, (string)$row['Passage1'], false) as $v) {
        if ($v['verse_id'] <= 0) continue;

        // All terms must appear in this specific verse
        $tl = mb_strtolower($v['text'], 'UTF-8');
        foreach ($terms_lower as $tl2) {
            if (mb_strpos($tl, $tl2) === false) continue 2;
        }

        $total++;
        if (count($results) < $SHOW_LIMIT) {
            $results[] = [
                'tid'  => $tid,
                'bid'  => $book_id,
                'book' => $book_name,
                'ch'   => $chapter,
                'v'    => $v['verse_id'],
                'text' => $v['text'],
                'url'  => nav_url($trans_k, $tid, $book_id, $chapter),
            ];
        }
    }
}

echo json_encode([
    'query'   => $q,
    'trans'   => $trans_k,
    'total'   => $total,
    'capped'  => $total > $SHOW_LIMIT || $sql_limited,
    'results' => $results,
], JSON_UNESCAPED_UNICODE);
