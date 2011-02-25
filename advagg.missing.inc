<?php

/**
 * @file
 * Advanced aggregation module; 404 handler.
 *
 */

/**
 * Menu Callback; regenerates a missing css file.
 *
 * @param $filepath
 *   filename
 * @return
 *   Boolean indicating if the bundle already exists.
 */
function advagg_missing_css() {
  ignore_user_abort();

  // Try to regenerate missing file
  advagg_missing_regenerate();

  // If here send out fast 404.
  advagg_missing_fast404();
}

/**
 * Menu Callback; regenerates a missing js file.
 */
function advagg_missing_js() {
  ignore_user_abort();

  // Try to regenerate missing file
  advagg_missing_regenerate();

  // If here send out fast 404.
  advagg_missing_fast404();
}

/**
 * regenerates a missing css file.
 *
 * @param $filename
 *   filename
 * @param $type
 *   css or js
 * @return
 *   false if bundle couldn't be generated.
 */
function advagg_missing_regenerate() {
  global $base_path, $conf;

  // Get filename from request.
  $arg = arg();
  $filename = array_pop($arg);

  // Verfy requested filename has the correct pattern.
  if (preg_match('/^(j|cs)s_[0-9a-f]{32}_\d+\.(j|cs)s$/', $filename) == FALSE) {
    return FALSE;
  }

  // Get type
  $type = substr($filename, 0, strpos($filename, '_'));

  // Get extension
  $ext = substr($filename, strpos($filename, '.', 37)+1);

  // Make sure extension is the same as the type.
  if ($ext != $type) {
    return FALSE;
  }

  // Extract info from wanted filename.
  if ($type == 'css') {
    $md5 = substr($filename, 4, 32);
    $counter = substr($filename, 37, strpos($filename, '.', 38)-37);
  }
  elseif ($type == 'js') {
    $md5 = substr($filename, 3, 32);
    $counter = substr($filename, 36, strpos($filename, '.', 37)-36);
  }
  else {
    return FALSE;
  }

  $_GET['redirect_counter'] = isset($_GET['redirect_counter']) ? intval($_GET['redirect_counter']) : 0;
  if ($_GET['redirect_counter'] > 5) {
    watchdog('advagg', 'This request could not generate correctly. Loop detected. Request data: %info', array('%info' => $_GET['q']));
    return FALSE;
  }

  // Remove file from the cache.
  cache_clear_all($_GET['q'], 'cache_advagg');

  // Counter in database.
  $counter_in_db = db_result(db_query("SELECT counter FROM {advagg_bundles} WHERE bundle_md5 = '%s'", $md5));
  if ($counter_in_db === FALSE) {
    return FALSE;
  }

  // Only process if we got an older counter.
  if ($counter > $counter_in_db || $counter < 0) {
    return FALSE;
  }

  // Rebuild file.
  $conf['advagg_async_generation'] = FALSE;
  $good = advagg_rebuld_bundle($md5, $counter);
  if (!$good) {
    watchdog('advagg', 'This request could not generate correctly. Aggregate not generated. Request data: %info', array('%info' => $_GET['q']));
    return FALSE;
  }

  // Redirect to file.
  $_GET['redirect_counter']++;
  $uri = $base_path . $_GET['q'] . '?redirect_counter=' . $_GET['redirect_counter'];
  header('Location: ' . $uri, TRUE, 307);
  exit;
}
