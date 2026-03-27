<?php
// ============================================================
// Bible Reader — PHP Web Edition
// Fashioned after the manner of the C++/ImGui desktop app,
// and arrayed in the dark parchment thereof.
// ============================================================

require_once __DIR__ . '/inc/config.php';
require_once __DIR__ . '/inc/db.php';
require_once __DIR__ . '/inc/render.php';

// ---------------------------------------------------------------------------
// The Request — thus is it parsed from the query string
// ---------------------------------------------------------------------------

// Seek the translation by name, heeding not the case thereof
$trans_key = $_GET['trans'] ?? 'KJV';
if (!array_key_exists($trans_key, $TRANSLATIONS)) {
    $trans_key = 'KJV';
    foreach ($TRANSLATIONS as $k => $_) {
        if (strcasecmp($k, $_GET['trans'] ?? '') === 0) { $trans_key = $k; break; }
    }
}
$trans      = $TRANSLATIONS[$trans_key];
$is_rtl        = $trans['rtl'];
$is_kjv        = ($trans_key === 'KJV');
$is_strongs    = !empty($trans['strongs']);
$is_interlinear = !empty($trans['interlinear']);

$pdo   = open_db($BIBLES_DIR . '/' . $trans['file']);
$db_ok = ($pdo !== null);
$books = $db_ok ? get_books($pdo) : [];

// Gather the books by their testament, that the sidebar may be filled
$books_by_tid = [];
foreach ($books as $b) {
    $books_by_tid[(int)$b['TestamentID']][] = $b;
}

// Determine the testament; let it default to the New when it is present
$avail_tids  = $trans['tids'];
$default_tid = in_array(2, $avail_tids, true) ? 2 : $avail_tids[0];
$tid         = isset($_GET['tid']) ? (int)$_GET['tid'] : $default_tid;
if (!in_array($tid, $avail_tids, true)) $tid = $default_tid;

// Determine the book; if none be named, take the first of this testament
$first_book   = null;
$current_book = null;
foreach ($books as $b) {
    $btid = (int)$b['TestamentID'];
    $bbid = (int)$b['BookID'];
    if ($btid === $tid && $first_book === null) $first_book = $b;
    if ($btid === $tid && $bbid === (isset($_GET['book']) ? (int)$_GET['book'] : 0)) $current_book = $b;
}
if (!$current_book) $current_book = $first_book;
$book_id = $current_book ? (int)$current_book['BookID'] : 1;

// Determine the chapter, keeping it within its appointed bounds
$num_chapters = $current_book ? max(1, (int)$current_book['NumChapters']) : 1;
$chapter      = isset($_GET['ch']) ? (int)$_GET['ch'] : 1;
$chapter      = max(1, min($chapter, $num_chapters));

// Determine the verse; if given, the comparison panel shall open forthwith
$verse_param = isset($_GET['verse']) ? (int)$_GET['verse'] : 0;

// Assemble an ordered list of all books, for traversal from one unto the next
$all_books = array_filter($books, fn($b) => in_array((int)$b['TestamentID'], $avail_tids, true));
$all_books = array_values($all_books);

$cur_pos = -1;
foreach ($all_books as $i => $b) {
    if ((int)$b['TestamentID'] === $tid && (int)$b['BookID'] === $book_id) { $cur_pos = $i; break; }
}

// Reckon the URLs of the previous chapter and the next
$prev_url = null;
$next_url = null;

if ($chapter > 1) {
    $prev_url = nav_url($trans_key, $tid, $book_id, $chapter - 1);
} elseif ($cur_pos > 0) {
    $pb       = $all_books[$cur_pos - 1];
    $prev_url = nav_url($trans_key, (int)$pb['TestamentID'], (int)$pb['BookID'], max(1, (int)$pb['NumChapters']));
}

if ($chapter < $num_chapters) {
    $next_url = nav_url($trans_key, $tid, $book_id, $chapter + 1);
} elseif ($cur_pos >= 0 && $cur_pos < count($all_books) - 1) {
    $nb    = $all_books[$cur_pos + 1];
    $stmt  = $pdo->prepare('SELECT MIN(ChapterID) FROM Bible WHERE TestamentID=? AND BookID=?');
    $stmt->execute([(int)$nb['TestamentID'], (int)$nb['BookID']]);
    $next_url = nav_url($trans_key, (int)$nb['TestamentID'], (int)$nb['BookID'], (int)($stmt->fetchColumn() ?: 1));
}

// Fetch the verses of this chapter from the database
$verses = ($db_ok && $current_book) ? get_chapter($pdo, $tid, $book_id, $chapter) : [];

// Fetch the Strong's annotations, for them that seek the original tongues
$verse_html   = [];
$strongs_data = new stdClass();

if (($is_strongs || $is_interlinear) && $db_ok && $current_book) {
    $ks_pdo = open_db($BIBLES_DIR . '/KJV_strongs.db');
    $s_pdo  = open_db($BIBLES_DIR . '/strongs.db');

    if ($ks_pdo) {
        $sn   = $current_book['ShortName'];
        $stmt = $ks_pdo->prepare('SELECT Verse, HTML FROM VerseHTML WHERE ShortName=? AND Chapter=?');
        $stmt->execute([$sn, $chapter]);
        foreach ($stmt->fetchAll(PDO::FETCH_ASSOC) as $r) {
            $verse_html[(int)$r['Verse']] = $r['HTML'];
        }

        if ($s_pdo && $verse_html) {
            $stmt = $ks_pdo->prepare('SELECT DISTINCT StrongsNum FROM WordTags WHERE ShortName=? AND Chapter=?');
            $stmt->execute([$sn, $chapter]);
            $nums = $stmt->fetchAll(PDO::FETCH_COLUMN);

            if ($nums) {
                $ph    = implode(',', array_fill(0, count($nums), '?'));
                $stmt2 = $s_pdo->prepare(
                    "SELECT StrongsNum, Lemma, Translit, Pronunciation, StrongsDef
                     FROM Strongs WHERE StrongsNum IN ($ph)"
                );
                $stmt2->execute($nums);
                foreach ($stmt2->fetchAll(PDO::FETCH_ASSOC) as $r) {
                    $key               = $r['StrongsNum'];
                    $strongs_data->$key = [
                        'l' => $r['Lemma'],
                        't' => $r['Translit'] ?: $r['Pronunciation'],
                        'd' => $r['StrongsDef'],
                    ];
                }
            }
        }
    }
}

// Fetch the original Hebrew or Greek text for the second column
$orig_verses = [];
$orig_label  = '';
$orig_rtl    = false;

if ($is_strongs && $db_ok && $current_book) {
    $orig_pdo = open_db($BIBLES_DIR . '/' . ($tid === 1 ? 'Tanakh.db' : 'UGNT.db'));
    if ($orig_pdo) {
        $orig_verses = get_chapter($orig_pdo, $tid, $book_id, $chapter);
        $orig_label  = ($tid === 1) ? 'Hebrew — Tanakh' : 'Greek — UGNT';
        $orig_rtl    = ($tid === 1);
    }
}

// Fetch the Greek and Hebrew texts for the interlinear view
$grk_verses_il = [];
$heb_verses_il = [];
if ($is_interlinear && $db_ok && $current_book) {
    $grk_pdo = open_db($BIBLES_DIR . '/' . ($tid === 1 ? 'Septuagint.db' : 'UGNT.db'));
    if ($grk_pdo) {
        // In the LXX, Ezra and Nehemiah are united as 2 Esdras (DC tid=3, book=15, ch 1-23).
        // Ezra (OT book 15) maps to 2 Esdras ch 1-10 (same chapter number).
        // Nehemiah (OT book 16) maps to 2 Esdras ch 11-23 (chapter + 10).
        $lxx_tid  = $tid;
        $lxx_book = $book_id;
        $lxx_ch   = $chapter;
        if ($tid === 1 && $book_id === 15) { $lxx_tid = 3; }
        if ($tid === 1 && $book_id === 16) { $lxx_tid = 3; $lxx_book = 15; $lxx_ch = $chapter + 10; }
        $grk_verses_il = get_chapter($grk_pdo, $lxx_tid, $lxx_book, $lxx_ch);
    }
    if ($tid === 1) {
        $heb_pdo = open_db($BIBLES_DIR . '/Tanakh.db');
        if ($heb_pdo) $heb_verses_il = get_chapter($heb_pdo, $tid, $book_id, $chapter);
    }
}

$book_title = $current_book ? htmlspecialchars($current_book['LongName']) : '';
$page_title = $book_title   ? "$book_title $chapter"                      : 'Bible Reader';

// ---------------------------------------------------------------------------
// If this be an AJAX request, return JSON and depart; let no HTML be written
// ---------------------------------------------------------------------------
if (isset($_GET['ajax'])) {
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode([
        'reader_html'   => $is_interlinear
            ? render_interlinear_html($db_ok, $verses, $book_title, $chapter, $tid, $book_id, $prev_url, $next_url, $verse_html, $grk_verses_il, $heb_verses_il)
            : render_reader_html($db_ok, $verses, $book_title, $chapter, $is_kjv, $is_rtl, $tid, $book_id, $prev_url, $next_url, $trans, $is_strongs, $verse_html),
        'page_title'    => $page_title,
        'book_title'    => $book_title,
        'toolbar_title' => $book_title . ($current_book ? " \u{2014} Ch. $chapter" : ''),
        'prev_url'      => $prev_url,
        'next_url'      => $next_url,
        'tid'           => $tid,
        'book_id'       => $book_id,
        'chapter'       => $chapter,
        'num_chapters'  => $num_chapters,
        'trans'         => $trans_key,
        'is_rtl'        => $is_rtl,
        'strongs_data'  => $strongs_data,
        'orig_html'     => (!$is_interlinear && $orig_verses) ? render_orig_col($orig_verses) : null,
        'orig_label'    => $orig_label,
        'orig_rtl'      => $orig_rtl,
        'book_name'     => book_display_name($tid, $book_id) ?? '',
    ], JSON_UNESCAPED_UNICODE);
    exit;
}

// Mark the stylesheet with the time of its last changing, lest the browser remember the old
$css_ver = filemtime(__DIR__ . '/style.css');

// ---------------------------------------------------------------------------
// Prepare the HTML fragments beforehand, that the output be without blemish
// ---------------------------------------------------------------------------

$trans_options = '';
foreach ($TRANSLATIONS as $key => $t) {
    $sel           = ($key === $trans_key) ? ' selected' : '';
    $trans_options .= '          <option value="' . htmlspecialchars($key) . '"' . $sel . '>'
                    . htmlspecialchars($t['label']) . '</option>' . "\n";
}

$sidebar_sections = '';
foreach ($books_by_tid as $group_tid => $group_books) {
    if (!in_array($group_tid, $avail_tids, true)) continue;
    $section_id = 'tgroup-' . $group_tid;
    $label      = $TID_LABELS[$group_tid] ?? 'Books';
    if (count($avail_tids) > 1) {
        $sidebar_sections .= '    <button class="sidebar-section-head collapsible open"' . "\n"
                           . '            onclick="toggleSection(\'' . $section_id . '\', this)"' . "\n"
                           . '            aria-expanded="true">' . htmlspecialchars($label)
                           . '<span class="collapse-arrow">&#9650;</span></button>' . "\n";
    }
    $sidebar_sections .= '    <ul class="book-list" id="' . $section_id . '">' . "\n";
    foreach ($group_books as $b) {
        $btid   = (int)$b['TestamentID'];
        $bbid   = (int)$b['BookID'];
        $active = ($btid === $tid && $bbid === $book_id);
        $url    = nav_url($trans_key, $btid, $bbid, 1);
        $name   = htmlspecialchars(book_display_name($btid, $bbid) ?? $b['LongName']);
        $cls    = 'book-link nav-ajax' . ($active ? ' active' : '');
        $sidebar_sections .= '      <li><a href="' . $url . '" class="' . $cls . '" data-tid="'
                           . $btid . '" data-book="' . $bbid . '">' . $name . '</a></li>' . "\n";
    }
    $sidebar_sections .= '    </ul>' . "\n";
}

$_raw = $is_interlinear
    ? render_interlinear_html($db_ok, $verses, $book_title, $chapter, $tid, $book_id, $prev_url, $next_url, $verse_html, $grk_verses_il, $heb_verses_il)
    : render_reader_html($db_ok, $verses, $book_title, $chapter, $is_kjv, $is_rtl, $tid, $book_id, $prev_url, $next_url, $trans, $is_strongs, $verse_html);
$reader_html = '      ' . str_replace("\n", "\n      ", rtrim($_raw)) . "\n";
unset($_raw);

$strongs_heading_html = '';
if ($is_strongs) {
    $strongs_heading_html = '    <h1 class="chapter-heading" id="reader-heading">' . $book_title
                          . '<span class="chapter-label">Chapter ' . $chapter . '</span></h1>' . "\n";
}

$orig_panel_html = '';
if ($is_strongs) {
    $_orig       = render_orig_col($orig_verses);
    $_orig       = '      ' . str_replace("\n", "\n      ", rtrim($_orig)) . "\n";
    $orig_panel_html = '    <article class="reader reader-orig" id="reader-orig" dir="' . ($orig_rtl ? 'rtl' : 'ltr') . '"'
                     . ' data-tid="' . $tid . '" data-book="' . $book_id . '" data-ch="' . $chapter . '"'
                     . ' data-book-name="' . htmlspecialchars(book_display_name($tid, $book_id) ?? '') . '">' . "\n"
                     . '      <div class="orig-label">' . htmlspecialchars($orig_label) . '</div>' . "\n"
                     . $_orig
                     . '    </article>' . "\n";
    unset($_orig);
}

?><!DOCTYPE html>
<!--
     ██╗███████╗███████╗██╗   ██╗███████╗     ██████╗██╗  ██╗██████╗ ██╗███████╗████████╗
     ██║██╔════╝██╔════╝██║   ██║██╔════╝    ██╔════╝██║  ██║██╔══██╗██║██╔════╝╚══██╔══╝
     ██║█████╗  ███████╗██║   ██║███████╗    ██║     ███████║██████╔╝██║███████╗   ██║   
██   ██║██╔══╝  ╚════██║██║   ██║╚════██║    ██║     ██╔══██║██╔══██╗██║╚════██║   ██║   
╚█████╔╝███████╗███████║╚██████╔╝███████║    ╚██████╗██║  ██║██║  ██║██║███████║   ██║   
 ╚════╝ ╚══════╝╚══════╝ ╚═════╝ ╚══════╝     ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝╚══════╝   ╚═╝  
-->
<html lang="en" dir="ltr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title><?= $page_title ?></title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Pirata+One&family=EB+Garamond:ital,wght@0,400;0,500;1,400&family=Crimson+Pro:wght@400;600&family=Roboto:wght@400;500&family=Noto+Serif+Hebrew:wght@400&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="<?= BASE_PATH ?>style.css?v=<?= $css_ver ?>">
</head>
<body>

<!-- ================================================================
     The Toolbar — spanning the full width at the top
     ================================================================ -->
<header class="toolbar">
  <button class="sidebar-toggle" onclick="document.body.classList.toggle('sidebar-open')"
          title="Toggle sidebar" aria-label="Toggle sidebar">&#9776;</button>

  <div class="toolbar-trans">
    <select class="trans-select" onchange="switchTranslation(this.value)">
<?= $trans_options ?>    </select>
  </div>

  <span class="toolbar-title" id="toolbar-title"><?= $book_title ?><?= $current_book ? " &mdash; Ch. $chapter" : '' ?></span>

  <nav class="chapter-nav" aria-label="Chapter navigation">
    <a href="<?= $prev_url ?? '#' ?>" id="nav-prev" class="nav-btn<?= $prev_url ? ' nav-ajax' : ' disabled' ?>" title="Previous chapter">&#8249;</a>
    <form method="get" class="chapter-jump" id="chapter-form">
      <input type="hidden" name="trans" value="<?= htmlspecialchars($trans_key) ?>">
      <input type="hidden" name="tid"   id="form-tid"  value="<?= $tid ?>">
      <input type="hidden" name="book"  id="form-book" value="<?= $book_id ?>">
      <input type="number" name="ch" id="ch-input" value="<?= $chapter ?>" min="1" max="<?= $num_chapters ?>" class="ch-input" title="Chapter" aria-label="Chapter number">
      <span class="ch-of">/ <span id="ch-max"><?= $num_chapters ?></span></span>
    </form>
    <a href="<?= $next_url ?? '#' ?>" id="nav-next" class="nav-btn<?= $next_url ? ' nav-ajax' : ' disabled' ?>" title="Next chapter">&#8250;</a>
  </nav>

  <button class="toolbar-btn" id="btn-search" onclick="openSearch()" title="Search (/ or Ctrl+K)" aria-label="Search">
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" width="15" height="15" aria-hidden="true"><path fill-rule="evenodd" d="M8 4a4 4 0 100 8 4 4 0 000-8zM2 8a6 6 0 1110.89 3.476l4.817 4.817a1 1 0 01-1.414 1.414l-4.816-4.816A6 6 0 012 8z" clip-rule="evenodd"/></svg>
  </button>

  <div class="inline-controls">
    <div class="layout-toggle" title="Font size">
      <button class="layout-btn" onclick="adjustFont(-1)" title="Decrease font size">A&#8315;</button>
      <button class="layout-btn" onclick="adjustFont(1)"  title="Increase font size">A&#8314;</button>
    </div>
    <div class="layout-toggle" title="Toggle column layout">
      <button id="btn-single"  onclick="setLayout('single')"  class="layout-btn active">&#9646; Single</button>
      <button id="btn-columns" onclick="setLayout('columns')" class="layout-btn">&#9646;&#9646; Columns</button>
    </div>
    <button class="parallel-toggle" id="parallel-toggle" onclick="toggleParallel()" title="Compare verses across translations">&#9783; Compare</button>
  </div>

  <div class="overflow-wrap" id="overflow-wrap">
    <button class="overflow-btn" onclick="toggleOverflow()" title="Options">&#8943;</button>
    <div class="overflow-menu" id="overflow-menu">
      <div class="overflow-row">
        <span class="overflow-label">Version</span>
        <select class="trans-select overflow-trans" onchange="switchTranslation(this.value);closeOverflow()">
<?= $trans_options ?>        </select>
      </div>
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
        <button class="parallel-toggle" id="parallel-toggle-m" onclick="toggleParallel();closeOverflow()" style="width:100%">&#9783; Compare</button>
      </div>
    </div>
  </div>
</header>

<!-- ================================================================
     The Body Below — the sidebar and the main reader side by side
     ================================================================ -->
<div class="body-below">

<!-- ================================================================
     The Sidebar — wherein all the books are numbered
     ================================================================ -->
<nav class="sidebar" id="sidebar">
  <div class="sidebar-inner">
<?= $sidebar_sections ?>  </div>
</nav>

<!-- ================================================================
     The Main Area — the reader, and all that is therein
     ================================================================ -->
<div class="main">

  <div id="reader-wrap"<?= $is_strongs ? ' data-strongs="1"' : ($is_interlinear ? ' data-interlinear="1"' : '') ?>>
<?= $strongs_heading_html ?>    <article class="reader" id="reader" dir="<?= $is_rtl ? 'rtl' : 'ltr' ?>">
<?= $reader_html ?>    </article>
<?= $orig_panel_html ?>
  </div>

</div>

</div>

<!-- ================================================================
     The Parallel Panel — comparing the selfsame verse across translations
     ================================================================ -->
<aside class="parallel-panel" id="parallel-panel" aria-label="Parallel verse comparison">
  <div class="parallel-header">
    <span class="parallel-title" id="parallel-title">Parallel Verse</span>
    <div class="parallel-header-actions">
      <button class="parallel-copy" id="parallel-copy-btn" onclick="copyParallelLink()"
              title="Copy link to this verse" aria-label="Copy link" style="display:none">&#10697;</button>
      <button class="parallel-close" onclick="closeParallel()" aria-label="Close">&times;</button>
    </div>
  </div>
  <div class="parallel-body" id="parallel-body">
    <p class="parallel-hint">Click a verse number to compare across translations.</p>
  </div>
</aside>

<!-- The Strong's Popover — showing the meaning of the original words -->
<div id="strongs-popup" class="strongs-popup" aria-hidden="true"></div>

<!-- ================================================================
     Search overlay
     ================================================================ -->
<div id="search-overlay" hidden aria-modal="true" role="dialog" aria-label="Search the scriptures">
  <div class="search-card">

    <div class="search-input-row">
      <svg class="search-mag" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" width="18" height="18" aria-hidden="true">
        <path fill-rule="evenodd" d="M8 4a4 4 0 100 8 4 4 0 000-8zM2 8a6 6 0 1110.89 3.476l4.817 4.817a1 1 0 01-1.414 1.414l-4.816-4.816A6 6 0 012 8z" clip-rule="evenodd"/>
      </svg>
      <input id="search-q" type="text" autocomplete="off" spellcheck="false"
             placeholder="Search the scriptures…" aria-label="Search query">
      <kbd class="search-esc" onclick="closeSearch()" title="Close search">esc</kbd>
    </div>

    <div class="search-bar">
      <div class="search-filters" id="search-filters">
        <button class="sf active" data-tid="0">All</button>
<?php
// Default search translation: map Interlinear/KJVPlus → KJV
$_strans = (!empty($trans['interlinear']) || $trans_key === 'KJVPlus') ? 'KJV' : $trans_key;
foreach ($TRANSLATIONS[$_strans]['tids'] as $stid):
?>
        <button class="sf" data-tid="<?= $stid ?>"><?= $TID_LABELS[$stid] ?? 'Other' ?></button>
<?php endforeach; ?>
      </div>
      <select id="search-trans-sel" class="search-trans-sel" aria-label="Search translation">
<?php
$_slabels = ['KJV'=>'KJV','Septuagint'=>'LXX','Vulgate'=>'Vulgate','UGNT'=>'Greek NT','Tanakh'=>'Hebrew','Apocrypha'=>'Apocrypha'];
foreach ($TRANSLATIONS as $_sk => $_st):
    if (!empty($_st['interlinear']) || $_sk === 'KJVPlus') continue;
?>
        <option value="<?= htmlspecialchars($_sk) ?>"<?= $_sk === $_strans ? ' selected' : '' ?>><?= htmlspecialchars($_slabels[$_sk] ?? $_sk) ?></option>
<?php endforeach; ?>
      </select>
      <span id="search-status" class="search-status"></span>
    </div>

    <div id="search-results" class="search-results">
      <p class="search-prompt">Type a word or phrase &mdash; <em>grace and peace</em>, <em>fear not</em>&hellip;</p>
    </div>

  </div>
</div>

<script>
// ---------------------------------------------------------------------------
// Constants handed down from the server of requests
// ---------------------------------------------------------------------------
var BASE_PATH    = <?= json_encode(BASE_PATH) ?>;
var STRONGS_DATA = <?= json_encode($strongs_data, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT) ?>;

// ---------------------------------------------------------------------------
// Utility functions, given for the common benefit
// ---------------------------------------------------------------------------
function escHtml(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
                  .replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}

// ---------------------------------------------------------------------------
// DOM nodes, kept in remembrance lest they be sought again and again
// ---------------------------------------------------------------------------
var $reader       = document.getElementById('reader');
var $readerWrap   = document.getElementById('reader-wrap');
var $toolbarTitle = document.getElementById('toolbar-title');
var $navPrev      = document.getElementById('nav-prev');
var $navNext      = document.getElementById('nav-next');
var $chInput      = document.getElementById('ch-input');
var $chMax        = document.getElementById('ch-max');
var $parallelBody = document.getElementById('parallel-body');

// ---------------------------------------------------------------------------
// State, kept in accord after every navigation
// ---------------------------------------------------------------------------
var currentPrevUrl       = <?= $prev_url ? json_encode($prev_url) : 'null' ?>;
var currentNextUrl       = <?= $next_url ? json_encode($next_url) : 'null' ?>;
var currentChapterUrl    = <?= json_encode(nav_url($trans_key, $tid, $book_id, $chapter)) ?>;
var currentParallelVerse = 0;
var parallelStrongsData  = {};

// ---------------------------------------------------------------------------
// The font size, preserved in localStorage across all sessions
// ---------------------------------------------------------------------------
var FONT_MIN = 14, FONT_MAX = 28;

function getSavedFont() {
  try { return parseInt(localStorage.getItem('br-fontsize')) || 18; } catch(e) { return 18; }
}
function applyFont(size) {
  $reader.style.fontSize = size + 'px';
}
function adjustFont(delta) {
  var next = Math.min(FONT_MAX, Math.max(FONT_MIN, getSavedFont() + delta));
  applyFont(next);
  try { localStorage.setItem('br-fontsize', next); } catch(e) {}
}
applyFont(getSavedFont());

// ---------------------------------------------------------------------------
// The layout — single or columns — likewise preserved in localStorage
// ---------------------------------------------------------------------------
function setLayout(mode) {
  var isStrongs = $readerWrap && $readerWrap.dataset.strongs === '1';
  var origPanel = document.getElementById('reader-orig');
  if (isStrongs) {
    if (origPanel) origPanel.classList.toggle('orig-visible', mode === 'columns');
    $readerWrap.classList.toggle('has-orig', mode === 'columns');
  } else {
    $reader.classList.toggle('columns', mode === 'columns');
  }
  ['btn-single','btn-single-m'].forEach(function(id) {
    var el = document.getElementById(id); if (el) el.classList.toggle('active', mode === 'single');
  });
  ['btn-columns','btn-columns-m'].forEach(function(id) {
    var el = document.getElementById(id); if (el) el.classList.toggle('active', mode === 'columns');
  });
  try { localStorage.setItem('br-layout', mode); } catch(e) {}
}

(function() {
  var saved = 'single';
  try { saved = localStorage.getItem('br-layout') || 'single'; } catch(e) {}
  if (saved === 'columns' && !(window.innerWidth <= 767 && $readerWrap && $readerWrap.dataset.interlinear === '1')) {
    setLayout('columns');
  }
})();

// ---------------------------------------------------------------------------
// The loading of chapters, without the reloading of the whole page
// ---------------------------------------------------------------------------
function loadChapter(url, focusVerse) {
  var ajaxUrl = url + (url.indexOf('?') >= 0 ? '&' : '?') + 'ajax=1';
  fetch(ajaxUrl)
    .then(function(r) { return r.json(); })
    .then(function(d) { applyChapter(d, url, focusVerse || 0); })
    .catch(function() { window.location = url; });
}

function applyChapter(d, url, focusVerse) {
  $reader.innerHTML = d.reader_html;
  $reader.setAttribute('dir', d.is_rtl ? 'rtl' : 'ltr');

  var rh = document.getElementById('reader-heading');
  if (rh) {
    rh.innerHTML = escHtml(d.book_title || '') + '<span class="chapter-label">Chapter ' + d.chapter + '</span>';
  }

  var origPanel = document.getElementById('reader-orig');
  if (origPanel) {
    origPanel.dataset.tid      = d.tid;
    origPanel.dataset.book     = d.book_id;
    origPanel.dataset.ch       = d.chapter;
    origPanel.dataset.bookName = d.book_name || '';
    if (d.orig_html) {
      origPanel.innerHTML = '<div class="orig-label">' + escHtml(d.orig_label || '') + '</div>' + d.orig_html;
      origPanel.setAttribute('dir', d.orig_rtl ? 'rtl' : 'ltr');
    } else {
      origPanel.innerHTML = '<p class="orig-empty">—</p>';
    }
  }

  var layout = 'single';
  try { layout = localStorage.getItem('br-layout') || 'single'; } catch(e) {}
  if (window.innerWidth <= 767 && $readerWrap && $readerWrap.dataset.interlinear === '1') layout = 'single';
  var isStrongs = $readerWrap && $readerWrap.dataset.strongs === '1';
  if (isStrongs) {
    if (origPanel) origPanel.classList.toggle('orig-visible', layout === 'columns');
    $readerWrap.classList.toggle('has-orig', layout === 'columns');
  } else {
    $reader.classList.toggle('columns', layout === 'columns');
  }
  applyFont(getSavedFont());

  $toolbarTitle.textContent = d.toolbar_title;
  updateNavBtn($navPrev, d.prev_url);
  updateNavBtn($navNext, d.next_url);

  document.getElementById('form-tid').value  = d.tid;
  document.getElementById('form-book').value = d.book_id;
  $chInput.value = d.chapter;
  $chInput.max   = d.num_chapters;
  $chMax.textContent = d.num_chapters;

  document.querySelectorAll('.book-link').forEach(function(a) {
    a.classList.toggle('active', parseInt(a.dataset.tid) === d.tid && parseInt(a.dataset.book) === d.book_id);
  });

  if (d.trans) {
    document.querySelectorAll('.trans-select').forEach(function(sel) { sel.value = d.trans; });
  }

  currentPrevUrl      = d.prev_url;
  currentNextUrl      = d.next_url;
  currentChapterUrl   = url;
  currentParallelVerse = 0;

  document.title = d.page_title;
  history.pushState({ url: url }, d.page_title, url);

  var copyBtn = document.getElementById('parallel-copy-btn');
  if (copyBtn) copyBtn.style.display = 'none';

  $reader.scrollTop = 0;
  window.scrollTo(0, 0);

  if (d.strongs_data) STRONGS_DATA = d.strongs_data;
  for (var k in parallelStrongsData) {
    if (Object.prototype.hasOwnProperty.call(parallelStrongsData, k)) STRONGS_DATA[k] = parallelStrongsData[k];
  }
  _spPinned = false;
  hideStrongsPopup();
  if (focusVerse) scrollToVerse(focusVerse);
}

function scrollToVerse(v) {
  var el = $reader.querySelector('.vnum[data-v="' + v + '"], [data-v="' + v + '"]');
  if (!el) return;
  el.scrollIntoView({ behavior: 'smooth', block: 'center' });
  // Interlinear: highlight the whole verse block
  var block = el.closest('.interlinear-verse');
  if (block) { block.classList.add('verse-hl'); return; }
  // Regular reader: highlight vnum + following vtext
  el.classList.add('verse-hl');
  var next = el.nextElementSibling;
  if (next && next.classList.contains('vtext')) next.classList.add('verse-hl');
}

function updateNavBtn(btn, url) {
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

// ---------------------------------------------------------------------------
// The switching of translations
// ---------------------------------------------------------------------------
function switchTranslation(trans) {
  var tid  = document.getElementById('form-tid').value;
  var book = document.getElementById('form-book').value;
  var ch   = document.getElementById('ch-input').value;
  window.location = BASE_PATH + trans.toLowerCase() + '/' + tid + '/' + book + '/' + ch;
}

// ---------------------------------------------------------------------------
// The collapsing and expanding of testament sections
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
// Helpers for the overflow menu
// ---------------------------------------------------------------------------
function toggleOverflow() { document.getElementById('overflow-menu').classList.toggle('open'); }
function closeOverflow()  { document.getElementById('overflow-menu').classList.remove('open'); }

// ---------------------------------------------------------------------------
// A single handler for all clicks upon the document
// ---------------------------------------------------------------------------
document.addEventListener('click', function(e) {
  // Intercept clicks on verse numbers in the original-language column
  var origVnum = e.target.closest('.vnum[data-v]');
  if (origVnum) {
    var op = origVnum.closest('#reader-orig');
    if (op) {
      loadParallel(+op.dataset.tid, +op.dataset.book, +op.dataset.ch, +origVnum.dataset.v, op.dataset.bookName);
      return;
    }
  }

  // Intercept navigation links, fetching chapters without a full reload
  var link = e.target.closest('a.nav-ajax');
  if (link) {
    var href = link.getAttribute('href');
    if (href && href !== '#') {
      e.preventDefault();
      loadChapter(href);
      if (window.innerWidth < 768) document.body.classList.remove('sidebar-open');
      return;
    }
  }

  // Pin or unpin the Strong's popup upon clicking a word
  var sspan = e.target.closest('span[data-s]');
  if (sspan) {
    if (_spPinned && sspan === _spActive) { _spPinned = false; hideStrongsPopup(); }
    else                                  { _spPinned = true;  showStrongsPopup(sspan); }
    return;
  }
  if (_spPinned && !_spopup.contains(e.target)) { _spPinned = false; hideStrongsPopup(); }

  // Close the overflow menu and sidebar when clicking elsewhere
  var ow = document.getElementById('overflow-wrap');
  if (ow && !ow.contains(e.target)) closeOverflow();
  if (window.innerWidth < 768) {
    var sb = document.getElementById('sidebar');
    if (sb && !sb.contains(e.target) && !e.target.classList.contains('sidebar-toggle')) {
      document.body.classList.remove('sidebar-open');
    }
  }
});

// ---------------------------------------------------------------------------
// The chapter form, and the popstate thereof
// ---------------------------------------------------------------------------
document.getElementById('chapter-form').addEventListener('submit', function(e) {
  e.preventDefault();
  loadChapter('?' + new URLSearchParams(new FormData(this)).toString());
});
document.getElementById('ch-input').addEventListener('change', function() {
  document.getElementById('chapter-form').dispatchEvent(new Event('submit'));
});
window.addEventListener('popstate', function(e) {
  loadChapter((e.state && e.state.url) ? e.state.url : window.location.search);
});

// ---------------------------------------------------------------------------
// Keyboard shortcuts, for the swift of hand
// ---------------------------------------------------------------------------
document.addEventListener('keydown', function(e) {
  var searchOpen = !document.getElementById('search-overlay').hasAttribute('hidden');
  // ESC closes search first; other panels handled when search is shut
  if (e.key === 'Escape') {
    if (searchOpen) { closeSearch(); return; }
    closeParallel();
    if (_spPinned) { _spPinned = false; }
    hideStrongsPopup();
    return;
  }
  // Open search with / or Ctrl/Cmd+K (when not already in an input)
  if (!searchOpen) {
    if (e.key === '/' && e.target.tagName !== 'INPUT' && e.target.tagName !== 'SELECT') {
      e.preventDefault(); openSearch(); return;
    }
    if (e.key === 'k' && (e.ctrlKey || e.metaKey)) {
      e.preventDefault(); openSearch(); return;
    }
  }
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT') return;
  if (e.key === 'ArrowLeft'  && currentPrevUrl) { loadChapter(currentPrevUrl); return; }
  if (e.key === 'ArrowRight' && currentNextUrl) { loadChapter(currentNextUrl); return; }
});

// ---------------------------------------------------------------------------
// The parallel verse comparison panel
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
  setActiveVerse(0);
  currentParallelVerse = 0;
  parallelStrongsData  = {};
  var copyBtn = document.getElementById('parallel-copy-btn');
  if (copyBtn) copyBtn.style.display = 'none';
}

function setActiveVerse(verse) {
  var prev = document.querySelector('.vnum--active');
  if (prev) prev.classList.remove('vnum--active');
  if (verse > 0) {
    var el = document.querySelector('.vnum[data-v="' + verse + '"]');
    if (el) {
      el.classList.add('vnum--active');
      el.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    }
  }
}

function loadParallel(tid, book, chapter, verse, bookName) {
  setActiveVerse(verse);
  currentParallelVerse = verse;
  var copyBtn = document.getElementById('parallel-copy-btn');
  if (copyBtn) copyBtn.style.display = '';

  openParallel();
  document.getElementById('parallel-title').textContent = (bookName || 'Verse') + ' ' + chapter + ':' + verse;
  $parallelBody.innerHTML = '<p class="loading">Loading\u2026</p>';

  fetch(BASE_PATH + 'api.php?tid=' + tid + '&book=' + book + '&ch=' + chapter + '&verse=' + verse)
    .then(function(r) { return r.json(); })
    .then(function(data) {
      if (data.error) { $parallelBody.innerHTML = '<p class="error-msg">' + data.error + '</p>'; return; }
      var html = '';
      for (var i = 0; i < data.length; i++) {
        var item = data[i];
        var dir  = item.rtl ? ' dir="rtl"' : '';
        var body = item.html  ? item.html
                 : item.text  ? escHtml(item.text)
                 : null;
        html += '<div class="parallel-entry' + (item.rtl ? ' rtl' : '') + '">'
              + '<div class="parallel-trans">' + escHtml(item.label) + '</div>'
              + (body ? '<div class="parallel-text"' + dir + '>' + body + '</div>'
                      : '<div class="parallel-text missing">—</div>')
              + '</div>';
        if (item.strongs) {
          for (var k in item.strongs) {
            if (Object.prototype.hasOwnProperty.call(item.strongs, k)) {
              STRONGS_DATA[k] = item.strongs[k];
              parallelStrongsData[k] = item.strongs[k];
            }
          }
        }
      }
      $parallelBody.innerHTML = html;
    })
    .catch(function() { $parallelBody.innerHTML = '<p class="error-msg">Failed to load.</p>'; });
}

function copyParallelLink() {
  if (!currentParallelVerse) return;
  var url = location.protocol + '//' + location.host + currentChapterUrl + '/' + currentParallelVerse;
  var btn = document.getElementById('parallel-copy-btn');
  function flash() {
    if (!btn) return;
    btn.textContent = 'Copied!';
    setTimeout(function() { btn.innerHTML = '&#10697;'; }, 1500);
  }
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(url).then(flash).catch(function() { fallbackCopy(url); flash(); });
  } else {
    fallbackCopy(url); flash();
  }
}

function fallbackCopy(text) {
  var ta = document.createElement('textarea');
  ta.value = text;
  ta.style.cssText = 'position:fixed;opacity:0';
  document.body.appendChild(ta);
  ta.select();
  try { document.execCommand('copy'); } catch(e) {}
  document.body.removeChild(ta);
}

// ---------------------------------------------------------------------------
// The Strong's tooltip, revealing the meaning of the original word
// ---------------------------------------------------------------------------
var _spopup  = document.getElementById('strongs-popup');
var _spActive = null;
var _spPinned = false;

function showStrongsPopup(span) {
  if (span === _spActive && _spopup.style.display === 'block') return;
  _spActive = span;
  var key  = span.dataset.s;
  var data = STRONGS_DATA && STRONGS_DATA[key];
  var h    = '<div class="sp-num">' + escHtml(key) + '</div>';
  if (data) {
    if (data.l) h += '<div class="sp-lemma">'    + escHtml(data.l) + '</div>';
    if (data.t) h += '<div class="sp-translit">' + escHtml(data.t) + '</div>';
    if (data.d) h += '<div class="sp-def">'      + escHtml(data.d) + '</div>';
  }
  _spopup.innerHTML = h;
  _spopup.removeAttribute('aria-hidden');
  _spopup.style.display = 'block';
  positionStrongsPopup(span);
}

function hideStrongsPopup() {
  _spActive = null;
  _spopup.style.display = 'none';
  _spopup.setAttribute('aria-hidden', 'true');
}

function positionStrongsPopup(anchor) {
  _spopup.style.left = '0';
  _spopup.style.top  = '0';
  var rect = anchor.getBoundingClientRect();
  var pw   = _spopup.offsetWidth;
  var ph   = _spopup.offsetHeight;
  var x    = rect.left + window.scrollX;
  var y    = rect.bottom + window.scrollY + 6;
  var maxX = window.scrollX + window.innerWidth - pw - 8;
  if (x > maxX) x = maxX;
  if (x < 8)    x = 8;
  if (rect.bottom + ph + 16 > window.innerHeight) y = rect.top + window.scrollY - ph - 6;
  _spopup.style.left = x + 'px';
  _spopup.style.top  = y + 'px';
}

document.addEventListener('mouseover', function(e) {
  if (_spPinned) return;
  var span = e.target.closest('span[data-s]');
  if (span) showStrongsPopup(span);
});
document.addEventListener('mouseout', function(e) {
  if (_spPinned) return;
  var span = e.target.closest('span[data-s]');
  if (!span) return;
  var to = e.relatedTarget;
  if (to && (to === _spopup || _spopup.contains(to))) return;
  hideStrongsPopup();
});
_spopup.addEventListener('mouseleave', function() { if (!_spPinned) hideStrongsPopup(); });

// ---------------------------------------------------------------------------
// Search — seeking the scriptures
// ---------------------------------------------------------------------------
var _searchTid    = 0;
var _searchTimer  = null;
var _searchTrans  = <?= json_encode($_strans) ?>;
var _searchTransTids = <?php
    $std = [];
    foreach ($TRANSLATIONS as $k => $t) {
        if (!empty($t['interlinear']) || $k === 'KJVPlus') continue;
        $std[$k] = $t['tids'];
    }
    echo json_encode($std);
?>;
var _searchActive = false;

var $searchOverlay = document.getElementById('search-overlay');
var $searchQ       = document.getElementById('search-q');
var $searchStatus  = document.getElementById('search-status');
var $searchResults = document.getElementById('search-results');

function openSearch() {
  $searchOverlay.removeAttribute('hidden');
  _searchActive = true;
  $searchQ.focus();
  $searchQ.select();
}

function closeSearch() {
  $searchOverlay.setAttribute('hidden', '');
  _searchActive = false;
}

// Backdrop click closes
$searchOverlay.addEventListener('click', function(e) {
  if (e.target === $searchOverlay) closeSearch();
});

// Debounced input
$searchQ.addEventListener('input', function() {
  clearTimeout(_searchTimer);
  var q = this.value.trim();
  if (q.length < 3) {
    $searchStatus.textContent = '';
    $searchResults.innerHTML = '<p class="search-prompt">Type a word or phrase &mdash; <em>grace and peace</em>, <em>fear not</em>&hellip;</p>';
    return;
  }
  $searchStatus.textContent = 'Searching\u2026';
  _searchTimer = setTimeout(function() { execSearch(q); }, 350);
});

// Testament filter tabs (delegated — rebuilt dynamically when translation changes)
document.getElementById('search-filters').addEventListener('click', function(e) {
  var btn = e.target.closest('.sf');
  if (!btn) return;
  this.querySelectorAll('.sf').forEach(function(b) { b.classList.remove('active'); });
  btn.classList.add('active');
  _searchTid = parseInt(btn.dataset.tid, 10);
  var q = $searchQ.value.trim();
  if (q.length >= 3) execSearch(q);
});

// Translation selector — rebuild tabs and re-run search
document.getElementById('search-trans-sel').addEventListener('change', function() {
  _searchTrans = this.value;
  _searchTid   = 0;
  rebuildFilterTabs(_searchTransTids[_searchTrans] || []);
  var q = $searchQ.value.trim();
  if (q.length >= 3) execSearch(q);
  else { $searchStatus.textContent = ''; }
});

function rebuildFilterTabs(tids) {
  var labels = {1: 'Old Testament', 2: 'New Testament', 3: 'Deuterocanonical'};
  var html   = '<button class="sf active" data-tid="0">All</button>';
  tids.forEach(function(t) {
    html += '<button class="sf" data-tid="' + t + '">' + (labels[t] || t) + '</button>';
  });
  document.getElementById('search-filters').innerHTML = html;
}

function execSearch(q) {
  var url = BASE_PATH + 'search.php?q=' + encodeURIComponent(q)
          + '&trans=' + encodeURIComponent(_searchTrans)
          + '&tid='   + _searchTid;
  fetch(url)
    .then(function(r) { return r.json(); })
    .then(function(d) { renderSearch(d); })
    .catch(function()  { $searchStatus.textContent = 'Search unavailable.'; });
}

function _escSr(s) {
  return String(s).replace(/[&<>"']/g, function(c) {
    return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];
  });
}

function _highlight(text, words) {
  var out = _escSr(text);
  words.forEach(function(w) {
    if (!w || w.length < 2) return;
    out = out.replace(
      new RegExp(w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'gi'),
      '<mark>$&</mark>'
    );
  });
  return out;
}

function renderSearch(d) {
  // Parse quoted phrases and bare words (mirrors search.php logic)
  var words = [];
  if (d.query) {
    var re = /"([^"]+)"|(\S+)/g, m;
    while ((m = re.exec(d.query)) !== null) {
      var w = (m[1] || m[2]).replace(/"/g, '').trim();
      if (w.length >= 2) words.push(w);
    }
  }

  if (!d.results || d.results.length === 0) {
    $searchStatus.textContent = '';
    $searchResults.innerHTML = '<p class="search-prompt search-none">No results for \u201c' + _escSr(d.query) + '\u201d</p>';
    return;
  }

  var countLabel = d.capped ? '200+ results' : (d.total + (d.total === 1 ? ' result' : ' results'));
  $searchStatus.textContent = countLabel + ' for \u201c' + d.query + '\u201d';

  // Group by book, preserving order
  var bookOrder = [];
  var byBook    = {};
  d.results.forEach(function(r) {
    var k = r.book || ('Book ' + r.bid);
    if (!byBook[k]) { byBook[k] = []; bookOrder.push(k); }
    byBook[k].push(r);
  });

  var html = '';
  bookOrder.forEach(function(bookName) {
    var vv = byBook[bookName];
    html += '<div class="sr-group">'
          + '<div class="sr-book-head">' + _escSr(bookName)
          + ' <span class="sr-book-count">' + vv.length + '</span></div>';
    vv.forEach(function(r) {
      html += '<div class="sr-verse" data-url="' + _escSr(r.url) + '" data-v="' + r.v + '">'
            + '<span class="sr-ref">' + r.ch + ':' + r.v + '</span>'
            + '<span class="sr-text">' + _highlight(r.text, words) + '</span>'
            + '</div>';
    });
    html += '</div>';
  });

  if (d.capped) {
    html += '<p class="search-cap-note">Showing first 200 results \u2014 refine your search for more precise results.</p>';
  }

  $searchResults.innerHTML = html;
  $searchResults.scrollTop = 0;
}

// Click a result to navigate and highlight the verse
$searchResults.addEventListener('click', function(e) {
  var verse = e.target.closest('.sr-verse');
  if (!verse) return;
  closeSearch();
  loadChapter(verse.dataset.url, parseInt(verse.dataset.v, 10) || 0);
});

// ---------------------------------------------------------------------------
// Upon loading, open the comparison panel if a verse be given in the URL
// ---------------------------------------------------------------------------
(function() {
  var iv = <?= (int)$verse_param ?>;
  if (iv > 0) loadParallel(<?= $tid ?>, <?= $book_id ?>, <?= $chapter ?>, iv, <?= json_encode(book_display_name($tid, $book_id) ?? '') ?>);
})();
</script>
<!-- with love, from Lubbock, TX -->
</body>
</html>
