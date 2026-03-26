<?php
// ============================================================
// Bible Reader — PHP Web Edition
// Dark-parchment theme mirroring the C++/ImGui desktop app.
// ============================================================

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

$BIBLES_DIR = __DIR__ . '/Bibles';

// Base path for pretty URLs. Change to '/Website/' if not served from root.
define('BASE_PATH', '/');

// ---------------------------------------------------------------------------
// Canonical display names, ported from book_names.h
// Falls back to the DB's LongName for unknown IDs.
// ---------------------------------------------------------------------------
function book_display_name(int $tid, int $book_id): ?string {
    static $OT = [
        1=>"Genesis",2=>"Exodus",3=>"Leviticus",4=>"Numbers",5=>"Deuteronomy",
        6=>"Joshua",7=>"Judges",8=>"Ruth",9=>"1 Samuel",10=>"2 Samuel",
        11=>"1 Kings",12=>"2 Kings",13=>"1 Chronicles",14=>"2 Chronicles",
        15=>"Ezra",16=>"Nehemiah",17=>"Esther",18=>"Job",19=>"Psalms",
        20=>"Proverbs",21=>"Ecclesiastes",22=>"Song of Solomon",23=>"Isaiah",
        24=>"Jeremiah",25=>"Lamentations",26=>"Ezekiel",27=>"Daniel",
        28=>"Hosea",29=>"Joel",30=>"Amos",31=>"Obadiah",32=>"Jonah",
        33=>"Micah",34=>"Nahum",35=>"Habakkuk",36=>"Zephaniah",
        37=>"Haggai",38=>"Zechariah",39=>"Malachi",
    ];
    static $NT = [
        1=>"Matthew",2=>"Mark",3=>"Luke",4=>"John",5=>"Acts",
        6=>"Romans",7=>"1 Corinthians",8=>"2 Corinthians",9=>"Galatians",
        10=>"Ephesians",11=>"Philippians",12=>"Colossians",
        13=>"1 Thessalonians",14=>"2 Thessalonians",15=>"1 Timothy",
        16=>"2 Timothy",17=>"Titus",18=>"Philemon",19=>"Hebrews",
        20=>"James",21=>"1 Peter",22=>"2 Peter",23=>"1 John",
        24=>"2 John",25=>"3 John",26=>"Jude",27=>"Revelation",
    ];
    static $DC = [
        1=>"Tobit",2=>"Judith",3=>"Additions to Esther",4=>"Wisdom",
        5=>"Sirach",6=>"Baruch",7=>"Epistle of Jeremiah",8=>"Susanna",
        9=>"Bel and the Dragon",10=>"1 Maccabees",11=>"2 Maccabees",
        12=>"3 Maccabees",13=>"4 Maccabees",14=>"1 Esdras",15=>"2 Esdras",
        16=>"Prayer of Manasseh",17=>"Prayer of Azariah",18=>"Odes",
        19=>"Psalms of Solomon",20=>"Joshua (B Text)",21=>"Judges (B Text)",
        22=>"Laodiceans",
    ];
    if ($tid === 1) return $OT[$book_id] ?? null;
    if ($tid === 2) return $NT[$book_id] ?? null;
    if ($tid === 3) return $DC[$book_id] ?? null;
    return null;
}


$TRANSLATIONS = [
    'KJV'        => ['file' => 'KJV.db',        'label' => 'King James Version',    'rtl' => false, 'tids' => [1, 2]],
    'Septuagint' => ['file' => 'Septuagint.db', 'label' => 'Septuagint (LXX)',      'rtl' => false, 'tids' => [1]],
    'Vulgate'    => ['file' => 'Vulgate.db',     'label' => 'Latin Vulgate',         'rtl' => false, 'tids' => [1, 2]],
    'UGNT'       => ['file' => 'UGNT.db',        'label' => 'Greek New Testament',   'rtl' => false, 'tids' => [2]],
    'Tanakh'     => ['file' => 'Tanakh.db',      'label' => 'Hebrew Tanakh',         'rtl' => true,  'tids' => [1]],
    'Apocrypha'  => ['file' => 'Apocrypha.db',  'label' => 'Apocrypha',            'rtl' => false, 'tids' => [3]],
];

$TID_LABELS = [1 => 'Old Testament', 2 => 'New Testament', 3 => 'Deuterocanonical'];

// ---------------------------------------------------------------------------
// Database helper
// ---------------------------------------------------------------------------

function open_db(string $path): ?PDO {
    if (!file_exists($path)) return null;
    try {
        $pdo = new PDO("sqlite:$path");
        $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $pdo->exec("PRAGMA cache_size = 4000;");
        $pdo->exec("PRAGMA temp_store = MEMORY;");
        return $pdo;
    } catch (Exception $e) {
        return null;
    }
}

// ---------------------------------------------------------------------------
// parse_passage() — faithful port of the C++ version in bible_db.cpp.
//
// The DB stores verse paragraphs as a single Passage1 string:
//   "1:1 In the beginning... 1:2 And the earth was..."
// Each DB row may contain multiple consecutive verses (paragraph group).
// para_break=true marks the first verse as a paragraph opener.
//
// Returns array of ['verse_id', 'text', 'paragraph_start'].
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
        // Must begin with "chapter:" prefix
        if (substr($passage, $p, $plen_ch) !== $ch_prefix) break;
        $p += $plen_ch;

        // Parse verse number (digits followed by a single space)
        $num_start = $p;
        while ($p < $len && $passage[$p] >= '0' && $passage[$p] <= '9') $p++;
        if ($p === $num_start || $p >= $len || $passage[$p] !== ' ') break;
        $verse_num = (int)substr($passage, $num_start, $p - $num_start);
        $p++; // skip separator space

        // Find end of this verse's text: the next verse-ref in the same or next chapter
        $text_start = $p;
        $text_end   = $len;

        for ($s = $p; $s < $len; $s++) {
            // Next verse in same chapter: " N:V "
            if (substr($passage, $s, $plen_next) === $next_pat) {
                $ne = $s + $plen_next;
                $ns = $ne;
                while ($ne < $len && $passage[$ne] >= '0' && $passage[$ne] <= '9') $ne++;
                if ($ne > $ns && $ne < $len && $passage[$ne] === ' ') {
                    $text_end = $s;
                    break;
                }
            }
            // Spill into next chapter: " (N+1):V "
            if (substr($passage, $s, $plen_nextch) === $next_ch_pat) {
                $ne = $s + $plen_nextch;
                $ns = $ne;
                while ($ne < $len && $passage[$ne] >= '0' && $passage[$ne] <= '9') $ne++;
                if ($ne > $ns && $ne < $len && $passage[$ne] === ' ') {
                    $text_end = $s;
                    break;
                }
            }
        }

        // Extract and normalise text
        $text = substr($passage, $text_start, $text_end - $text_start);
        $text = str_replace(["\r\n", "\r", "\n"], ' ', $text);
        $text = trim($text);
        $text = preg_replace('/  +/', ' ', $text);

        $verses[] = [
            'verse_id'        => $verse_num,
            'text'            => $text,
            'paragraph_start' => $first ? $para_break : false,
        ];
        $first = false;

        if ($text_end >= $len) break;
        $p = $text_end + 1; // skip the space that precedes the next ref
    }

    return $verses;
}

// ---------------------------------------------------------------------------
// get_books() — fetch all books with chapter counts
// ---------------------------------------------------------------------------
function get_books(PDO $pdo): array {
    $sql = "SELECT b.TestamentID, b.BookID, b.ShortName, b.LongName,
                   COALESCE(s.NumChapters, 0) AS NumChapters
            FROM Books b
            LEFT JOIN BookStats s
                   ON s.TestamentID = b.TestamentID AND s.BookID = b.BookID
            ORDER BY b.TestamentID, b.BookID";
    $stmt = $pdo->query($sql);
    return $stmt ? $stmt->fetchAll(PDO::FETCH_ASSOC) : [];
}

// ---------------------------------------------------------------------------
// get_chapter() — port of bible_db_get_chapter (including the look-back)
// ---------------------------------------------------------------------------
function get_chapter(PDO $pdo, int $tid, int $book_id, int $chapter): array {
    $verses = [];

    // Look-back: last row of the previous chapter may spill verses into this one
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
                $verses   = parse_passage($chapter, $overflow, false);
            }
        }
    }

    // Main rows for this chapter
    $stmt = $pdo->prepare(
        "SELECT VerseID, Passage1 FROM Bible
         WHERE TestamentID=? AND BookID=? AND ChapterID=?
         ORDER BY VerseID"
    );
    $stmt->execute([$tid, $book_id, $chapter]);
    $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

    $first_row = (count($verses) === 0);
    foreach ($rows as $row) {
        $para_break = !$first_row;
        $first_row  = false;
        $parsed     = parse_passage($chapter, (string)$row['Passage1'], $para_break);
        foreach ($parsed as $v) $verses[] = $v;
    }

    return $verses;
}

// ---------------------------------------------------------------------------
// Build a nav URL preserving all current state except what changes
// ---------------------------------------------------------------------------
function nav_url(string $trans, int $tid, int $book, int $ch): string {
    return BASE_PATH . strtolower($trans) . '/' . $tid . '/' . $book . '/' . $ch;
}

// ---------------------------------------------------------------------------
// Request state
// ---------------------------------------------------------------------------

// Case-insensitive lookup so /kjv, /Septuagint, /septuagint, etc. all work
$trans_key = $_GET['trans'] ?? 'KJV';
if (!array_key_exists($trans_key, $TRANSLATIONS)) {
    $found = 'KJV';
    foreach ($TRANSLATIONS as $k => $_) {
        if (strcasecmp($k, $trans_key) === 0) { $found = $k; break; }
    }
    $trans_key = $found;
}
$trans   = $TRANSLATIONS[$trans_key];
$is_rtl  = $trans['rtl'];
$is_kjv  = ($trans_key === 'KJV');

$db_path = $BIBLES_DIR . '/' . $trans['file'];
$pdo     = open_db($db_path);
$db_ok   = ($pdo !== null);

$books   = $db_ok ? get_books($pdo) : [];

// Group books by testament for the sidebar
$books_by_tid = [];
foreach ($books as $b) {
    $books_by_tid[(int)$b['TestamentID']][] = $b;
}

// Resolve tid
$avail_tids = $trans['tids'];
// Default to NT (tid=2) when the translation has it and nothing is specified
$default_tid = in_array(2, $avail_tids, true) ? 2 : $avail_tids[0];
$tid = isset($_GET['tid']) ? (int)$_GET['tid'] : $default_tid;
if (!in_array($tid, $avail_tids, true)) $tid = $default_tid;

// Resolve book_id — default to first book in this tid
$first_book = null;
foreach ($books as $b) {
    if ((int)$b['TestamentID'] === $tid) { $first_book = $b; break; }
}
$default_book = ($first_book ? (int)$first_book['BookID'] : 1);
$book_id = isset($_GET['book']) ? (int)$_GET['book'] : $default_book;

// Find current book record
$current_book = null;
foreach ($books as $b) {
    if ((int)$b['TestamentID'] === $tid && (int)$b['BookID'] === $book_id) {
        $current_book = $b;
        break;
    }
}
if (!$current_book && $first_book) {
    $current_book = $first_book;
    $book_id      = (int)$first_book['BookID'];
}

$num_chapters = $current_book ? max(1, (int)$current_book['NumChapters']) : 1;
$chapter      = isset($_GET['ch']) ? (int)$_GET['ch'] : 1;
if ($chapter < 1) $chapter = 1;
if ($chapter > $num_chapters) $chapter = $num_chapters;

// Build flat ordered list of all (tid, book_id) for cross-book navigation
$all_books = [];
foreach ($books as $b) {
    if (in_array((int)$b['TestamentID'], $avail_tids, true)) {
        $all_books[] = $b;
    }
}

// Find position of current book in that list
$cur_pos = -1;
foreach ($all_books as $i => $b) {
    if ((int)$b['TestamentID'] === $tid && (int)$b['BookID'] === $book_id) {
        $cur_pos = $i;
        break;
    }
}

// Prev / Next navigation
$prev_url = null;
$next_url = null;

if ($chapter > 1) {
    $prev_url = nav_url($trans_key, $tid, $book_id, $chapter - 1);
} elseif ($cur_pos > 0) {
    $pb = $all_books[$cur_pos - 1];
    $prev_url = nav_url($trans_key, (int)$pb['TestamentID'], (int)$pb['BookID'],
                        max(1, (int)$pb['NumChapters']));
}

if ($chapter < $num_chapters) {
    $next_url = nav_url($trans_key, $tid, $book_id, $chapter + 1);
} elseif ($cur_pos >= 0 && $cur_pos < count($all_books) - 1) {
    $nb = $all_books[$cur_pos + 1];
    $first_ch_stmt = $pdo->prepare(
        "SELECT MIN(ChapterID) FROM Bible WHERE TestamentID=? AND BookID=?"
    );
    $first_ch_stmt->execute([(int)$nb['TestamentID'], (int)$nb['BookID']]);
    $first_ch = (int)($first_ch_stmt->fetchColumn() ?: 1);
    $next_url = nav_url($trans_key, (int)$nb['TestamentID'], (int)$nb['BookID'], $first_ch);
}

// Load chapter content
$verses = ($db_ok && $current_book) ? get_chapter($pdo, $tid, $book_id, $chapter) : [];

$book_title  = $current_book ? htmlspecialchars($current_book['LongName']) : '';
$page_title  = $book_title ? "$book_title $chapter" : 'Bible Reader';

// ---------------------------------------------------------------------------
// render_reader_html() — renders the inner content of <article.reader>.
// Called for both full-page and AJAX responses.
// ---------------------------------------------------------------------------
function render_reader_html(
    bool $db_ok, array $verses, string $book_title, int $chapter,
    bool $is_kjv, bool $is_rtl, int $tid, int $book_id,
    ?string $prev_url, ?string $next_url, array $trans
): string {
    ob_start();

    if (!$db_ok):
?>
      <p class="error-msg">Could not open <strong><?= htmlspecialchars($trans['file']) ?></strong>.<br>
         PDO SQLite: <code><?= extension_loaded('pdo_sqlite') ? 'loaded' : 'NOT loaded' ?></code></p>
<?php
    elseif (empty($verses)):
?>
      <p class="error-msg">No verses found for this chapter.</p>
<?php
    else:
?>
      <h1 class="chapter-heading">
        <?= $book_title ?>
        <span class="chapter-label">Chapter <?= $chapter ?></span>
      </h1>

      <?php
        $in_para    = false;
        $first_verse = true;
        foreach ($verses as $v):
          $vid  = $v['verse_id'];
          $text = $v['text'];
          $para = $v['paragraph_start'];

          if ($para || $first_verse) {
              if ($in_para) echo "</p>\n";
              echo '<p class="verse-para">';
              $in_para = true;
          }

          if ($is_kjv && $first_verse && $vid === 1 && $text !== '') {
              preg_match('/^./us', $text, $m);
              $dc_char = $m[0] ?? '';
              $rest    = substr($text, strlen($dc_char));
              echo '<span class="dropcap-wrap">';
              echo '<span class="dropcap" title="Compare verse 1 across translations" '
                  . 'onclick="loadParallel(' . $tid . ',' . $book_id . ',' . $chapter . ',' . $vid . ')">'
                  . htmlspecialchars($dc_char) . '</span>';
              echo htmlspecialchars($rest);
              echo '</span>';
          } else {
              echo '<sup class="vnum" data-v="' . $vid . '" '
                  . 'onclick="loadParallel(' . $tid . ',' . $book_id . ',' . $chapter . ',' . $vid . ')">'
                  . $vid . '</sup>';
              echo '<span class="vtext">' . htmlspecialchars($text) . '</span> ';
          }

          $first_verse = false;
        endforeach;
        if ($in_para) echo "</p>\n";
      ?>

<?php
    endif;

    if ($prev_url || $next_url):
?>
    <div class="bottom-nav">
      <?php if ($prev_url): ?>
        <a href="<?= $prev_url ?>" class="nav-btn-lg nav-ajax">&lsaquo; Previous</a>
      <?php else: ?>
        <span class="nav-btn-lg disabled"></span>
      <?php endif; ?>
      <?php if ($next_url): ?>
        <a href="<?= $next_url ?>" class="nav-btn-lg nav-ajax">Next &rsaquo;</a>
      <?php else: ?>
        <span class="nav-btn-lg disabled"></span>
      <?php endif; ?>
    </div>
<?php
    endif;

    return ob_get_clean();
}

// ---------------------------------------------------------------------------
// AJAX endpoint — return JSON and exit before any HTML output
// ---------------------------------------------------------------------------
if (isset($_GET['ajax'])) {
    header('Content-Type: application/json; charset=utf-8');
    $reader_html = render_reader_html(
        $db_ok, $verses, $book_title, $chapter,
        $is_kjv, $is_rtl, $tid, $book_id,
        $prev_url, $next_url, $trans
    );
    echo json_encode([
        'reader_html'    => $reader_html,
        'page_title'     => $page_title,
        'toolbar_title'  => $book_title . ($current_book ? " \xe2\x80\x94 Ch. $chapter" : ''),
        'prev_url'       => $prev_url,
        'next_url'       => $next_url,
        'tid'            => $tid,
        'book_id'        => $book_id,
        'chapter'        => $chapter,
        'num_chapters'   => $num_chapters,
        'trans'          => $trans_key,
        'is_rtl'         => $is_rtl,
    ], JSON_UNESCAPED_UNICODE);
    exit;
}

?><!DOCTYPE html>
<!--
┏┳┏┓┏┓┳┳┏┓  ┏┓┓┏┳┓┳┏┓┏┳┓
 ┃┣ ┗┓┃┃┗┓  ┃ ┣┫┣┫┃┗┓ ┃ 
┗┛┗┛┗┛┗┛┗┛  ┗┛┛┗┛┗┻┗┛ ┻
-->
<html lang="en" dir="ltr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><?= $page_title ?></title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Pirata+One&family=EB+Garamond:ital,wght@0,400;0,500;1,400&family=Crimson+Pro:wght@400;600&family=Roboto:wght@400;500&family=Noto+Serif+Hebrew:wght@400&display=swap" rel="stylesheet">
<link rel="stylesheet" href="<?= BASE_PATH ?>style.css">
</head>
<body>

<!-- ================================================================ -->
<!-- Sidebar                                                           -->
<!-- ================================================================ -->
<nav class="sidebar" id="sidebar">
  <div class="sidebar-inner">

    <!-- Translation picker -->
    <div class="sidebar-trans">
      <form method="get" id="trans-form">
        <select name="trans" onchange="switchTranslation(this.value)" class="trans-select">
          <?php foreach ($TRANSLATIONS as $key => $t): ?>
            <option value="<?= htmlspecialchars($key) ?>"<?= $key === $trans_key ? ' selected' : '' ?>>
              <?= htmlspecialchars($t['label']) ?>
            </option>
          <?php endforeach; ?>
        </select>
        <!-- Preserve current position when switching translation -->
        <input type="hidden" name="tid"  id="trans-tid"  value="<?= $tid ?>">
        <input type="hidden" name="book" id="trans-book" value="<?= $book_id ?>">
        <input type="hidden" name="ch"   id="trans-ch"   value="<?= $chapter ?>">
      </form>
    </div>

    <!-- Book list — collapsible testament sections -->
    <?php foreach ($books_by_tid as $group_tid => $group_books): ?>
      <?php if (!in_array($group_tid, $avail_tids, true)) continue; ?>
      <?php
        // Open the section that contains the currently-viewed book; collapse the rest.
        $section_active = true; // all sections start expanded
        $section_id = 'tgroup-' . $group_tid;
        $label = $TID_LABELS[$group_tid] ?? 'Books';
      ?>
      <?php if (count($avail_tids) > 1): ?>
        <button class="sidebar-section-head collapsible<?= $section_active ? ' open' : '' ?>"
                onclick="toggleSection('<?= $section_id ?>', this)"
                aria-expanded="<?= $section_active ? 'true' : 'false' ?>">
          <?= htmlspecialchars($label) ?>
          <span class="collapse-arrow"><?= $section_active ? '&#9650;' : '&#9660;' ?></span>
        </button>
      <?php endif; ?>
      <ul class="book-list" id="<?= $section_id ?>"
          <?= (count($avail_tids) > 1 && !$section_active) ? 'style="display:none"' : '' ?>>
        <?php foreach ($group_books as $b):
          $active = ((int)$b['TestamentID'] === $tid && (int)$b['BookID'] === $book_id);
          $url = nav_url($trans_key, (int)$b['TestamentID'], (int)$b['BookID'], 1);
        ?>
          <li>
            <a href="<?= $url ?>" class="book-link nav-ajax<?= $active ? ' active' : '' ?>"
               data-tid="<?= (int)$b['TestamentID'] ?>" data-book="<?= (int)$b['BookID'] ?>">
              <?= htmlspecialchars(book_display_name((int)$b['TestamentID'], (int)$b['BookID']) ?? $b['LongName']) ?>
            </a>
          </li>
        <?php endforeach; ?>
      </ul>
    <?php endforeach; ?>

  </div>
</nav>

<!-- ================================================================ -->
<!-- Main area                                                         -->
<!-- ================================================================ -->
<div class="main">

  <!-- Toolbar -->
  <header class="toolbar">
    <button class="sidebar-toggle" onclick="document.body.classList.toggle('sidebar-open')"
            title="Toggle sidebar" aria-label="Toggle sidebar">&#9776;</button>

    <span class="toolbar-title" id="toolbar-title">
      <?= $book_title ?><?= $current_book ? " &mdash; Ch. $chapter" : '' ?>
    </span>

    <!-- Chapter navigation -->
    <nav class="chapter-nav" aria-label="Chapter navigation">
      <a href="<?= $prev_url ?? '#' ?>" id="nav-prev"
         class="nav-btn<?= $prev_url ? ' nav-ajax' : ' disabled' ?>" title="Previous chapter">&#8249;</a>

      <form method="get" class="chapter-jump" id="chapter-form">
        <input type="hidden" name="trans" value="<?= htmlspecialchars($trans_key) ?>">
        <input type="hidden" name="tid"   id="form-tid"  value="<?= $tid ?>">
        <input type="hidden" name="book"  id="form-book" value="<?= $book_id ?>">
        <input type="number" name="ch" id="ch-input" value="<?= $chapter ?>" min="1" max="<?= $num_chapters ?>"
               class="ch-input" title="Chapter" aria-label="Chapter number">
        <span class="ch-of">/ <span id="ch-max"><?= $num_chapters ?></span></span>
      </form>

      <a href="<?= $next_url ?? '#' ?>" id="nav-next"
         class="nav-btn<?= $next_url ? ' nav-ajax' : ' disabled' ?>" title="Next chapter">&#8250;</a>
    </nav>

    <!-- Desktop: inline controls -->
    <div class="inline-controls">
      <div class="layout-toggle" title="Font size">
        <button class="layout-btn" onclick="adjustFont(-1)" title="Decrease font size">A&#8315;</button>
        <button class="layout-btn" onclick="adjustFont(1)"  title="Increase font size">A&#8314;</button>
      </div>
      <div class="layout-toggle" title="Toggle column layout">
        <button id="btn-single"  onclick="setLayout('single')"  class="layout-btn active">&#9646; Single</button>
        <button id="btn-columns" onclick="setLayout('columns')" class="layout-btn">&#9646;&#9646; Columns</button>
      </div>
      <button class="parallel-toggle" id="parallel-toggle" onclick="toggleParallel()"
              title="Compare verses across translations">&#9783; Compare</button>
    </div>

    <!-- Mobile: overflow menu -->
    <div class="overflow-wrap" id="overflow-wrap">
      <button class="overflow-btn" onclick="toggleOverflow()" title="Options">&#8943;</button>
      <div class="overflow-menu" id="overflow-menu">
        <div class="overflow-row">
          <span class="overflow-label">Font</span>
          <div class="layout-toggle">
            <button class="layout-btn" onclick="adjustFont(-1)">A&#8315;</button>
            <button class="layout-btn" onclick="adjustFont(1)">A&#8314;</button>
          </div>
        </div>
        <div class="overflow-row">
          <span class="overflow-label">Layout</span>
          <div class="layout-toggle">
            <button id="btn-single-m"  class="layout-btn active" onclick="setLayout('single')">&#9646; Single</button>
            <button id="btn-columns-m" class="layout-btn"        onclick="setLayout('columns')">&#9646;&#9646; Col</button>
          </div>
        </div>
        <div class="overflow-row">
          <button class="parallel-toggle" id="parallel-toggle-m" onclick="toggleParallel();closeOverflow()"
                  style="width:100%">&#9783; Compare</button>
        </div>
      </div>
    </div>
  </header>

  <!-- Reader -->
  <article class="reader" id="reader" dir="<?= $is_rtl ? 'rtl' : 'ltr' ?>">
    <?= render_reader_html($db_ok, $verses, $book_title, $chapter, $is_kjv, $is_rtl, $tid, $book_id, $prev_url, $next_url, $trans) ?>
  </article>
</div>

<!-- ================================================================ -->
<!-- Parallel comparison panel                                         -->
<!-- ================================================================ -->
<aside class="parallel-panel" id="parallel-panel" aria-label="Parallel verse comparison">
  <div class="parallel-header">
    <span class="parallel-title" id="parallel-title">Parallel Verse</span>
    <button class="parallel-close" onclick="closeParallel()" aria-label="Close">&times;</button>
  </div>
  <div class="parallel-body" id="parallel-body">
    <p class="parallel-hint">Click a verse number to compare across translations.</p>
  </div>
</aside>

<script>
var BASE_PATH = <?= json_encode(BASE_PATH) ?>;

// Translation switcher — navigate to a pretty URL preserving book/chapter
function switchTranslation(trans) {
  var tid  = document.getElementById('trans-tid').value;
  var book = document.getElementById('trans-book').value;
  var ch   = document.getElementById('trans-ch').value;
  window.location = BASE_PATH + trans.toLowerCase() + '/' + tid + '/' + book + '/' + ch;
}

// ---------------------------------------------------------------------------
// State — kept in sync after every AJAX navigation
// ---------------------------------------------------------------------------
var currentPrevUrl = <?= $prev_url ? json_encode($prev_url) : 'null' ?>;
var currentNextUrl = <?= $next_url ? json_encode($next_url) : 'null' ?>;

// ---------------------------------------------------------------------------
// AJAX chapter loading
// ---------------------------------------------------------------------------
function loadChapter(url) {
  var ajaxUrl = url + (url.indexOf('?') >= 0 ? '&' : '?') + 'ajax=1';
  fetch(ajaxUrl)
    .then(function(r) { return r.json(); })
    .then(function(d) { applyChapter(d, url); })
    .catch(function() { window.location = url; }); // fallback: full reload
}

function applyChapter(d, url) {
  // Reader content
  var reader = document.getElementById('reader');
  reader.innerHTML = d.reader_html;
  reader.setAttribute('dir', d.is_rtl ? 'rtl' : 'ltr');

  // Re-apply persisted font size and layout
  applyFont(getSavedFont());
  var layout = 'single';
  try { layout = localStorage.getItem('br-layout') || 'single'; } catch(e) {}
  reader.classList.toggle('columns', layout === 'columns');

  // Toolbar title
  document.getElementById('toolbar-title').textContent = d.toolbar_title;

  // Toolbar nav buttons
  updateNavBtn('nav-prev', d.prev_url);
  updateNavBtn('nav-next', d.next_url);

  // Chapter form
  document.getElementById('form-tid').value  = d.tid;
  document.getElementById('form-book').value = d.book_id;
  // Translation form — keep position in sync so switching trans lands on the right book
  document.getElementById('trans-tid').value  = d.tid;
  document.getElementById('trans-book').value = d.book_id;
  document.getElementById('trans-ch').value   = d.chapter;
  var chInput = document.getElementById('ch-input');
  chInput.value = d.chapter;
  chInput.max   = d.num_chapters;
  document.getElementById('ch-max').textContent = d.num_chapters;

  // Sidebar active book
  document.querySelectorAll('.book-link').forEach(function(a) {
    var active = parseInt(a.dataset.tid)  === d.tid &&
                 parseInt(a.dataset.book) === d.book_id;
    a.classList.toggle('active', active);
  });

  // Browser history + page title
  currentPrevUrl = d.prev_url;
  currentNextUrl = d.next_url;
  document.title = d.page_title;
  history.pushState({ url: url }, d.page_title, url);

  // Scroll reader to top
  reader.scrollTop = 0;
  window.scrollTo(0, 0);
}

function updateNavBtn(id, url) {
  var btn = document.getElementById(id);
  if (!btn) return;
  if (url) {
    btn.href = url;
    btn.classList.remove('disabled');
    btn.classList.add('nav-ajax');
  } else {
    btn.removeAttribute('href');
    btn.classList.add('disabled');
    btn.classList.remove('nav-ajax');
  }
}

// Intercept all .nav-ajax link clicks
document.addEventListener('click', function(e) {
  var link = e.target.closest('a.nav-ajax');
  if (!link) return;
  var href = link.getAttribute('href');
  if (!href || href === '#') return;
  e.preventDefault();
  loadChapter(href);
  // Close sidebar on mobile after picking a book
  if (window.innerWidth < 768) document.body.classList.remove('sidebar-open');
});

// Chapter form — intercept submit and ch-input changes
document.getElementById('chapter-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var params = new URLSearchParams(new FormData(this));
  loadChapter('?' + params.toString());
});
document.getElementById('ch-input').addEventListener('change', function() {
  document.getElementById('chapter-form').dispatchEvent(new Event('submit'));
});

// Browser back/forward
window.addEventListener('popstate', function(e) {
  var url = (e.state && e.state.url) ? e.state.url : window.location.search;
  loadChapter(url);
});

// ---------------------------------------------------------------------------
// Font size — persisted in localStorage
// ---------------------------------------------------------------------------
var FONT_MIN = 14, FONT_MAX = 28;
function getSavedFont() {
  try { return parseInt(localStorage.getItem('br-fontsize')) || 18; } catch(e) { return 18; }
}
function applyFont(size) {
  document.getElementById('reader').style.fontSize = size + 'px';
}
function adjustFont(delta) {
  var next = Math.min(FONT_MAX, Math.max(FONT_MIN, getSavedFont() + delta));
  applyFont(next);
  try { localStorage.setItem('br-fontsize', next); } catch(e) {}
}
applyFont(getSavedFont());

// ---------------------------------------------------------------------------
// Layout toggle (single / columns) — persisted in localStorage
// ---------------------------------------------------------------------------
function setLayout(mode) {
  document.getElementById('reader').classList.toggle('columns', mode === 'columns');
  ['btn-single','btn-single-m'].forEach(function(id) {
    var el = document.getElementById(id); if (el) el.classList.toggle('active', mode === 'single');
  });
  ['btn-columns','btn-columns-m'].forEach(function(id) {
    var el = document.getElementById(id); if (el) el.classList.toggle('active', mode === 'columns');
  });
  try { localStorage.setItem('br-layout', mode); } catch(e) {}
}

function toggleOverflow() {
  var menu = document.getElementById('overflow-menu');
  menu.classList.toggle('open');
}
function closeOverflow() {
  document.getElementById('overflow-menu').classList.remove('open');
}
(function() {
  var saved = 'single';
  try { saved = localStorage.getItem('br-layout') || 'single'; } catch(e) {}
  if (saved === 'columns') setLayout('columns');
})();

// ---------------------------------------------------------------------------
// Testament section collapse/expand
// ---------------------------------------------------------------------------
function toggleSection(id, btn) {
  var ul   = document.getElementById(id);
  var open = ul.style.display !== 'none';
  ul.style.display = open ? 'none' : '';
  btn.classList.toggle('open', !open);
  btn.setAttribute('aria-expanded', String(!open));
  btn.querySelector('.collapse-arrow').innerHTML = open ? '&#9660;' : '&#9650;';
}

// ---------------------------------------------------------------------------
// Close overflow menu and sidebar when clicking outside
document.addEventListener('click', function(e) {
  var ow = document.getElementById('overflow-wrap');
  if (ow && !ow.contains(e.target)) closeOverflow();
  if (window.innerWidth < 768) {
    var sidebar = document.getElementById('sidebar');
    if (!sidebar.contains(e.target) && !e.target.classList.contains('sidebar-toggle')) {
      document.body.classList.remove('sidebar-open');
    }
  }
});

// ---------------------------------------------------------------------------
// Parallel verse comparison
// ---------------------------------------------------------------------------
var parallelOpen = false;

function toggleParallel() { parallelOpen ? closeParallel() : openParallel(); }

function openParallel() {
  parallelOpen = true;
  document.getElementById('parallel-panel').classList.add('open');
  document.getElementById('parallel-toggle').classList.add('active');
}

function closeParallel() {
  parallelOpen = false;
  document.getElementById('parallel-panel').classList.remove('open');
  document.getElementById('parallel-toggle').classList.remove('active');
}

function loadParallel(tid, book, chapter, verse) {
  openParallel();
  var title = document.getElementById('parallel-title');
  var body  = document.getElementById('parallel-body');
  title.textContent = 'Verse ' + chapter + ':' + verse;
  body.innerHTML    = '<p class="loading">Loading\u2026</p>';

  fetch(BASE_PATH + 'api.php?tid=' + tid + '&book=' + book + '&ch=' + chapter + '&verse=' + verse)
    .then(function(r) { return r.json(); })
    .then(function(data) {
      if (data.error) { body.innerHTML = '<p class="error-msg">' + data.error + '</p>'; return; }
      var html = '';
      for (var i = 0; i < data.length; i++) {
        var item = data[i];
        html += '<div class="parallel-entry' + (item.rtl ? ' rtl' : '') + '">';
        html += '<div class="parallel-trans">' + escHtml(item.label) + '</div>';
        if (item.text) {
          html += '<div class="parallel-text" dir="' + (item.rtl ? 'rtl' : 'ltr') + '">'
               + escHtml(item.text) + '</div>';
        } else {
          html += '<div class="parallel-text missing">—</div>';
        }
        html += '</div>';
      }
      body.innerHTML = html;
    })
    .catch(function() { body.innerHTML = '<p class="error-msg">Failed to load.</p>'; });
}

function escHtml(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
                  .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}

// ---------------------------------------------------------------------------
// Keyboard shortcuts
// ---------------------------------------------------------------------------
document.addEventListener('keydown', function(e) {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
  if (e.key === 'ArrowLeft') {
    if (currentPrevUrl) loadChapter(currentPrevUrl);
  } else if (e.key === 'ArrowRight') {
    if (currentNextUrl) loadChapter(currentNextUrl);
  } else if (e.key === 'Escape') {
    closeParallel();
  }
});
</script>
</body>
</html>
