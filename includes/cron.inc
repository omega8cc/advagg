<?php

/**
 * @file
 * Drupal cron functions for the advagg module.
 */

/**
 * Implements hook_cron().
 */
function advagg_cron() {
  if (!variable_get('advagg_prune_on_cron', ADVAGG_PRUNE_ON_CRON)) {
    return;
  }

  // Set the oldest file/bundle to keep at 2 weeks.
  $max_time = module_exists('advagg_bundler') ? variable_get('advagg_bundler_outdated', ADVAGG_BUNDLER_OUTDATED) : 1209600;
  $max_file_time = REQUEST_TIME - $max_time;
  $max_bundle_time = REQUEST_TIME - ($max_time * 3);
  $bundles_removed = 0;
  $files_removed = array();

  // Prune old files
  $results = db_query("SELECT filename, filename_md5 FROM {advagg_files}");
  while ($row = $results->fetchAssoc()) {
    // If the file exists, do nothing
    if (file_exists($row['filename'])) {
      continue;
    }

    // Remove bundles referencing missing files, if they are older than 2 weeks.
    $bundles = db_query("SELECT bundle_md5 FROM {advagg_bundles} WHERE filename_md5 = :filename_md5 AND timestamp < :timestamp", array(':filename_md5' => $row['filename_md5'], ':timestamp' => $max_file_time));
    while ($bundle_md5 = $bundles->fetchField()) {
      $bundles_removed++;
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("DELETE FROM {advagg_bundles} WHERE bundle_md5 = '%s'", $bundle_md5) */
      db_delete('advagg_bundles')
  ->condition('bundle_md5', $bundle_md5)
  ->execute();
    }
    $count = db_query("SELECT COUNT(*) FROM {advagg_bundles} WHERE filename_md5 = :filename_md5", array(':filename_md5' => $row['filename_md5']))->fetchField();

    // If no more bundles reference the missing file then remove the file.
    if (empty($count)) {
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("DELETE FROM {advagg_files} WHERE filename_md5 = '%s'", $row['filename_md5']) */
      db_delete('advagg_files')
  ->condition('filename_md5', $row['filename_md5'])
  ->execute();
      $files_removed[] = $row['filename'];
    }
  }

  // Prune old bundles
  $bundles_removed += db_query("
    SELECT COUNT(DISTINCT bundle_md5) AS advagg_count
    FROM advagg_bundles
    WHERE timestamp < %d
  ", array($max_bundle_time))->fetchField();
  // TODO Please review the conversion of this statement to the D7 database API syntax.
  /* db_query("DELETE FROM {advagg_bundles} WHERE timestamp < %d", $max_bundle_time) */
  $results = db_delete('advagg_bundles')
  ->condition('timestamp', $max_bundle_time, '<')
  ->execute();

  // Report to watchdog if anything was done.
  if (!empty($bundles_removed) || !empty($files_removed)) {
    watchdog('advagg', 'Cron ran and the following files where removed from the database: %files <br /> %count old bundles where also removed from the database.', array(
      '%files' => implode(', ', $files_removed),
      '%count' => $bundles_removed,
    ));
  }
}
