<?php
// ============================================================
// Shared configuration — common to index.php and api.php alike
// ============================================================

$BIBLES_DIR = dirname(__DIR__) . '/Bibles';

define('BASE_PATH', '/');

$TRANSLATIONS = [
    'KJV'        => ['file' => 'KJV.db',        'label' => 'King James Version',      'rtl' => false, 'tids' => [1, 2]],
    'KJVPlus'    => ['file' => 'KJV.db',        'label' => "KJV + Strong's Numbers",  'rtl' => false, 'tids' => [1, 2], 'strongs' => true],
    'Interlinear'=> ['file' => 'KJV.db',        'label' => 'Interlinear View (Unofficial)', 'rtl' => false, 'tids' => [1, 2], 'interlinear' => true],
    'Septuagint' => ['file' => 'Septuagint.db', 'label' => 'Septuagint (LXX)',        'rtl' => false, 'tids' => [1, 3]],
    'Vulgate'    => ['file' => 'Vulgate.db',     'label' => 'Latin Vulgate',           'rtl' => false, 'tids' => [1, 2]],
    'UGNT'       => ['file' => 'UGNT.db',        'label' => 'Greek New Testament',     'rtl' => false, 'tids' => [2]],
    'Tanakh'     => ['file' => 'Tanakh.db',      'label' => 'Hebrew Tanakh',           'rtl' => true,  'tids' => [1]],
    'Apocrypha'  => ['file' => 'Apocrypha.db',  'label' => 'Apocrypha',              'rtl' => false, 'tids' => [3]],
];

$TID_LABELS = [1 => 'Old Testament', 2 => 'New Testament', 3 => 'Deuterocanonical'];
