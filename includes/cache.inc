<?php

/**
 * @file
 * Cache functions for the advagg module.
 */

/**
 * Implements hook_admin_menu_cache_info().
 *
 * Add in a cache flush for advagg.
 */
function advagg_admin_menu_cache_info() {
  if (variable_get('advagg_enabled', ADVAGG_ENABLED)) {
    $caches['advagg'] = array(
      'title' => t('CSS/JS Aggregation'),
      'callback' => 'advagg_admin_flush_cache',
    );
    return $caches;
  }
}

/**
 * Implements hook_admin_menu_output_alter().
 *
 * Add in a cache flush for advagg.
 */
function advagg_admin_menu_output_alter(&$content) {
  if (variable_get('advagg_enabled', ADVAGG_ENABLED)) {
    // Remove default core aggregation link.
    unset($content['icon']['icon']['flush-cache']['assets']);
  }
}

/**
 * Cache clear callback for admin_menu/flush-cache/advagg.
 */
function advagg_admin_flush_cache() {
  global $_advagg;
  _drupal_flush_css_js();
  $cache_tables = advagg_flush_caches();
  foreach ($cache_tables as $table) {
    cache_clear_all('*', $table, TRUE);
  }

  if (empty($_advagg['files'])) {
    drupal_set_message(t('Advanced CSS/JS Aggregation cache scanned and no out of date bundles detected.'));
  }
  else {
    if (empty($_advagg['rebuilt'])) {
      drupal_set_message(t('Advanced CSS/JS Aggregation cache scanned and out of date bundles have been marked. <br />Old Files: <br />!files <br />Marked Bundles Count: %count', array(
        '!files' => nl2br(filter_xss(implode("\n", $_advagg['files']))),
        '%count' => count($_advagg['bundles']),
      )
      ));
    }
    else {
      drupal_set_message(t('Advanced CSS/JS Aggregation cache scanned and out of date bundles have been incremented and rebuilt. <br />Old Files: <br />%files <br />%count done.', array(
        '%files' => implode('<br />', $_advagg['files']),
        '%count' => count($_advagg['rebuilt']),
      )
      ));
    }
  }
  drupal_goto(current_path());
}

/**
 * Implements hook_flush_caches().
 */
function advagg_flush_caches() {
  // Try to allocate enough time to flush the cache
  if (function_exists('set_time_limit')) {
    @set_time_limit(240);
  }

  global $_advagg;
  // Only one advagg cache flusher can run at a time.
  if (!lock_acquire('advagg_flush_caches')) {
    return;
  }

  // Only run code below if the advagg db tables exist.
  if (!db_table_exists('advagg_files')) {
    return array('cache_advagg_bundle_reuse');
  }

  // Find files that have changed.
  $needs_refreshing = array();
  $results = db_query("SELECT * FROM {advagg_files}");
  while ($row = $results->fetchAssoc()) {
    $checksum = advagg_checksum($row['filename']);
    // Let other modules see if the bundles needs to be rebuilt.
    // hook_advagg_files_table
    // Return TRUE in order to increment the counter.
    $hook_results = module_invoke_all('advagg_files_table', $row, $checksum);

    // Check each return value; see if an update is needed.
    $update = FALSE;
    if (!empty($hook_results)) {
      foreach ($hook_results as $update) {
        if ($update === TRUE) {
          break;
        }
      }
    }

    // Increment the counter if needed and mark file for bundle refreshment.
    if ($checksum != $row['checksum'] || $update == TRUE) {
      $needs_refreshing[$row['filename_md5']] = $row['filename'];
      // Update checksum; increment counter.
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("UPDATE {advagg_files} SET checksum = '%s', counter = counter + 1 WHERE filename_md5 = '%s'", $checksum, $row['filename_md5']) */
      db_update('advagg_files')
  ->fields(array(
        'checksum' => $checksum,
        'counter' => counter + 1,
      ))
  ->condition('filename_md5', $row['filename_md5'])
  ->execute();
    }
  }

  // Get the bundles.
  $bundles = array();
  foreach ($needs_refreshing as $filename_md5 => $filename) {
    $results = db_query("SELECT bundle_md5 FROM {advagg_bundles} WHERE filename_md5 = :filename_md5", array(':filename_md5' => $filename_md5));
    while ($row = db_fetch_array($results)) {
      $bundles[$row['bundle_md5']] = $row['bundle_md5'];
    }
  }

  foreach ($bundles as $bundle_md5) {
    // Increment Counter
    // TODO Please review the conversion of this statement to the D7 database API syntax.
    /* db_query("UPDATE {advagg_bundles} SET counter = counter + 1, timestamp = %d WHERE bundle_md5 = '%s'", REQUEST_TIME, $bundle_md5) */
    db_update('advagg_bundles')
  ->fields(array(
      'counter' => counter + 1,
      'timestamp' => REQUEST_TIME,
    ))
  ->condition('bundle_md5', $bundle_md5)
  ->execute();

    if (variable_get('advagg_rebuild_on_flush', ADVAGG_REBUILD_ON_FLUSH)) {
      // Rebuild bundles on shutdown in the background. This is needed so that
      // the cache_advagg_bundle_reuse table has been cleared.
      register_shutdown_function('advagg_rebuild_bundle', $bundle_md5, '', TRUE);
    }
  }
  $_advagg['bundles'] = $bundles;
  $_advagg['files'] = $needs_refreshing;

  // Garbage collection
  list($css_path, $js_path) = advagg_get_root_files_dir();
  file_scan_directory($css_path[1], '/.*/', array('callback' => 'advagg_delete_file_if_stale'));
  file_scan_directory($js_path[1], '/.*/', array('callback' => 'advagg_delete_file_if_stale'));

  lock_release('advagg_flush_caches');
  return array('cache_advagg_bundle_reuse');
}

/**
 * Disable the page cache if the aggregate is not in the bundle.
 */
function advagg_disable_page_cache() {
  global $conf;
  $conf['advagg_use_full_cache'] = FALSE;
  if (variable_get('advagg_page_cache_mode', ADVAGG_PAGE_CACHE_MODE)) {
    $conf['cache'] = CACHE_DISABLED;

    // Invoke hook_advagg_disable_page_cache(). Allows 3rd party page cache
    // plugins like boost or varnish to not cache this page.
    module_invoke_all('advagg_disable_page_cache');
  }
}

/**
 * Get data about a file.
 *
 * @param $filename_md5
 *   md5 of filename.
 * @return
 *   data array from database.
 */
function advagg_get_file_data($filename_md5) {
  $data = cache_get($filename_md5, 'cache_advagg_files_data');
  if (empty($data->data)) {
    return FALSE;
  }
  return $data->data;
}

/**
 * Set data about a file.
 *
 * @param $filename_md5
 *   md5 of filename.
 * @param $data
 *   data to store.
 */
function advagg_set_file_data($filename_md5, $data) {
  cache_set($filename_md5, $data, 'cache_advagg_files_data', CACHE_PERMANENT);
}
