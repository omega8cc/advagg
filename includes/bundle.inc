<?php

/**
 * @file
 * Bundle functions for the advagg module.
 */
 
/**
 * Given a list of files; return back the aggregated filename.
 *
 * @param $files
 *   List of files in the proposed bundle.
 * @param $filetype
 *   css or js.
 * @param $counter
 *   (optional) Counter value.
 * @param $bundle_md5
 *   (optional) Bundle's machine name.
 * @return
 *   Aggregated filename.
 */
function advagg_get_filename($files, $filetype, $counter = FALSE, $bundle_md5 = '') {
  if (empty($files) || empty($filetype)) {
    return FALSE;
  }

  global $_advagg;
  $filenames = array();

  $run_alter = FALSE;
  if (empty($bundle_md5)) {
    // Create bundle md5
    $schema = advagg_get_server_schema();
    $bundle_md5 = md5($schema . implode('', $files));
    $run_alter = TRUE;

    // Record root request in db.
    // Get counter if there.
    if (empty($counter)) {
      $counter = db_query("SELECT counter FROM {advagg_bundles} WHERE bundle_md5 = :bundle_md5", array(':bundle_md5' => $bundle_md5))->fetchField();
    }
    // If this is a brand new bundle then insert file/bundle info into database.
    if ($counter === FALSE) {
      $counter = 0;
      advagg_insert_bundle_db($files, $filetype, $bundle_md5, TRUE);
    }
    // If bundle should be root and is not, then make it root.
    // Refresh timestamp if older then 12 hours.
    $row = db_fetch_array(db_query("SELECT root, timestamp FROM {advagg_bundles} WHERE bundle_md5 = :bundle_md5", array(':bundle_md5' => $bundle_md5)));
    if ($row['root'] === 0 || REQUEST_TIME - $row['timestamp'] > variable_get('advagg_file_last_used_interval', ADVAGG_FILE_LAST_USED_INTERVAL)) {
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("UPDATE {advagg_bundles} SET root = '1', timestamp = %d WHERE bundle_md5 = '%s'", REQUEST_TIME, $bundle_md5) */
      db_update('advagg_bundles')
  ->fields(array(
        'root' => '1',
        'timestamp' => REQUEST_TIME,
      ))
  ->condition('bundle_md5', $bundle_md5)
  ->execute();
    }
  }

  // Set original array.
  $filenames[] = array(
    'filetype' => $filetype,
    'files' => $files,
    'counter' => $counter,
    'bundle_md5' => $bundle_md5,
  );

  // Invoke hook_advagg_filenames_alter() to give installed modules a chance to
  // alter filenames. One to many relationships come to mind.
  // Do not run alter if MD5 was given, we want to generate that file only in
  // this special case.
  if ($run_alter) {
    // Force counter to be looked up later.
    $filenames[0]['counter'] = FALSE;
    drupal_alter('advagg_filenames', $filenames);
  }

  // Write to DB if needed and create filenames.
  $output = array();
  $used_md5 = array();
  if (variable_get('advagg_debug', ADVAGG_DEBUG)) {
    $_advagg['debug']['get_filename_post_alter'][] = array(
      'key' => $bundle_md5,
      'filenames' => $filenames,
    );
  }

  // Get all counters at once
  $counters = array();
  foreach ($filenames as $key => $values) {
    if (empty($values['counter'])) {
      $counters[$key] = $values['bundle_md5'];
    }
  }
  $result = advagg_db_multi_select_in('advagg_bundles', 'bundle_md5', "'%s'", $counters, array('counter', 'bundle_md5'), 'GROUP BY bundle_md5');
  while ($row = db_fetch_array($result)) {
    $key = array_search($row['bundle_md5'], $counters);
    if (empty($filenames[$key]['counter']) && $filenames[$key]['counter'] !== 0) {
      $filenames[$key]['counter'] = intval($row['counter']);
    }
  }

  foreach ($filenames as $values) {
    // Get info from array.
    $filetype = $values['filetype'];
    $files = $values['files'];
    $counter = $values['counter'];
    $bundle_md5 = $values['bundle_md5'];

    // See if a JS bundle exists that already has the same files in it, just in a
    // different order.
//     if ($filetype == 'js' && $run_alter) {
//       advagg_find_existing_bundle($files, $bundle_md5);
//     }

    // Do not add the same bundle twice.
    if (isset($used_md5[$bundle_md5])) {
      continue;
    }
    $used_md5[$bundle_md5] = TRUE;

    // If this is a brand new bundle then insert file/bundle info into database.
    if (empty($counter) && $counter !== 0) {
      $counter = 0;
      advagg_insert_bundle_db($files, $filetype, $bundle_md5, FALSE);
    }

    // Prefix filename to prevent blocking by firewalls which reject files
    // starting with "ad*".
    $output[] = array(
      'filename' => advagg_build_filename($filetype, $bundle_md5, $counter),
      'files' => $files,
      'bundle_md5' => $bundle_md5,
    );

    // Refresh timestamp if older then 12 hours.
    $row = db_fetch_array(db_query("SELECT timestamp FROM {advagg_bundles} WHERE bundle_md5 = :bundle_md5", array(':bundle_md5' => $bundle_md5)));
    if (REQUEST_TIME - $row['timestamp'] > variable_get('advagg_file_last_used_interval', ADVAGG_FILE_LAST_USED_INTERVAL)) {
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("UPDATE {advagg_bundles} SET timestamp = %d WHERE bundle_md5 = '%s'", REQUEST_TIME, $bundle_md5) */
      db_update('advagg_bundles')
  ->fields(array(
        'timestamp' => REQUEST_TIME,
      ))
  ->condition('bundle_md5', $bundle_md5)
  ->execute();
    }

  }
  return $output;
}

/**
 * Get a bundle from the cache & verify it is good.
 *
 * @param $cached_data_key
 *   cache key for the cache_advagg_bundle_reuse table.
 * @param $debug_name
 *   Name to output in the array if debugging is enabled.
 * @return
 *   data from the cache.
 */
function advagg_cached_bundle_get($cached_data_key, $debug_name) {
  global $_advagg;
  $data = cache_get($cached_data_key, 'cache_advagg_bundle_reuse');
  if (!empty($data->data)) {
    $data = $data->data;
    $bundle_contents = array();
    $good = TRUE;
    // Debugging.
    if (variable_get('advagg_debug', ADVAGG_DEBUG)) {
      // Verify cached data is good.
      foreach ($data as $filename => $extra) {
        if (is_numeric($filename)) {
          continue;
        }
        // Get md5 from aggregated filename.
        $b_md5 = explode('/', $filename);
        $b_md5 = explode('_', array_pop($b_md5));
        $b_md5 = $b_md5[1];

        // Lookup bundle and make sure it is valid.
        if (!empty($b_md5)) {
          list($b_filetype, $b_files) = advagg_get_files_in_bundle($b_md5);
          $bundle_contents[$filename] = $b_files;
          if (empty($b_files)) {
            $good = FALSE;
          }
        }
      }

      $_advagg['debug'][$debug_name][] = array(
        'key' => $cached_data_key,
        'cache' => $data,
        'bundle_contents' => $bundle_contents,
      );
    }
    if ($good) {
      return $data;
    }
  }
  return FALSE;
}

/**
 * Given a list of files, see if a bundle already exists containing all of those
 * files. If in strict mode then the file count has to be the same.
 *
 * @param $files
 *   List of files in the proposed bundle.
 * @param $bundle_md5
 *   Bundle's machine name.
 */
function advagg_find_existing_bundle(&$files, &$bundle_md5) {
  // Sort files for better cache hits.
  $temp_files = $files;
  sort($temp_files);
  $schema = advagg_get_server_schema();
  $cached_data_key = 'advagg_existing_' . md5($schema . implode('', $temp_files));

  // Try cache first; cache table is cache_advagg_bundle_reuse. string is debug name.
  $cached_data = advagg_cached_bundle_get($cached_data_key, 'advagg_find_existing_bundle');
  if (!empty($cached_data)) {
    $files = $cached_data[0]['files'];
    $bundle_md5 = $cached_data[0]['bundle_md5'];
    return;
  }

  // Build union query.
  $query = 'SELECT root.bundle_md5 FROM {advagg_bundles} AS root';
  $joins = array();
  $wheres = array();
  $args = array();
  $counter = 0;
  foreach ($files as $filename) {
    // Use alpha for table aliases; numerics do not work.
    $key = strtr($counter, '01234567890', 'abcdefghij');

    $joins[$key] = "\nINNER JOIN {advagg_bundles} AS $key USING(bundle_md5)\n";
    if ($counter == 0) {
      $wheres[$key] = "WHERE $key.filename_md5 = '%s'";
    }
    else {
      $wheres[$key] = "AND $key.filename_md5 = '%s'";
    }
    $args[$key] = md5($filename);
    $counter++;
  }
  $query .= implode("\n", $joins);
  $query .= implode("\n", $wheres);
  $query .= ' GROUP BY bundle_md5';

  // Find matching bundles and select first good one.
  $files_count = count($files);
  $results = db_query($query, $args);
  while ($new_md5 = $results->fetchField()) {
    $count = db_query("SELECT count(*) FROM {advagg_bundles} WHERE bundle_md5 = :bundle_md5", array(':bundle_md5' => $new_md5))->fetchField();
    // Make sure bundle has the same number of files if using strict matching.
    if (!empty($count) && $count == $files_count) {
      $bundle_md5 = $new_md5;
      $data = array(array(
          'files' => $files,
          'bundle_md5' => $bundle_md5,
        ));
      cache_set($cached_data_key, $data, 'cache_advagg_bundle_reuse', CACHE_TEMPORARY);
      return;
    }
  }
}

/**
 * Build the filename.
 *
 * @param $filetype
 *   css or js.
 * @param $counter
 *   Counter value.
 * @param $bundle_md5
 *   Bundle's machine name.
 */
function advagg_build_filename($filetype, $bundle_md5, $counter) {
  return $filetype . '_' . $bundle_md5 . '_' . $counter . '.' . $filetype;
}

/**
 * Insert info into the advagg_files and advagg_bundles database.
 *
 * @param $files
 *   List of files in the proposed bundle.
 * @param $filetype
 *   css or js.
 * @param $bundle_md5
 *   Bundle's machine name.
 * @param $root
 *   Is this a root bundle.
 */
function advagg_insert_bundle_db($files, $filetype, $bundle_md5, $root) {
  $lock_name = 'advagg_insert_bundle_db' . $bundle_md5;
  if (!lock_acquire($lock_name)) {
    // If using async, wait before returning to give the other request time
    // to complete.
    if (variable_get('advagg_aggregate_mode', ADVAGG_AGGREGATE_MODE) < 2) {
      lock_wait($lock_name);
    }
    return;
  }

  // Double check that the bundle doesn't exist now that we are in a lock.
  $bundle_exists = db_query("SELECT 1 FROM {advagg_bundles} WHERE bundle_md5 = :bundle_md5", array(':bundle_md5' => $bundle_md5))->fetchField();
  if ($bundle_exists) {
    lock_release($lock_name);
    return;
  }
  foreach ($files as $order => $filename) {
    $filename_md5 = md5($filename);

    // Insert file into the advagg_files table if it doesn't exist.
    $checksum = db_query("SELECT checksum FROM {advagg_files} WHERE filename_md5 = :filename_md5", array(':filename_md5' => $filename_md5))->fetchField();
    if (empty($checksum)) {
      $checksum = advagg_checksum($filename);
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("INSERT INTO {advagg_files} (filename, filename_md5, checksum, filetype, filesize) VALUES ('%s', '%s', '%s', '%s', %d)", $filename, $filename_md5, $checksum, $filetype, @filesize($filename)) */
      $id = db_insert('advagg_files')
  ->fields(array(
        'filename' => $filename,
        'filename_md5' => $filename_md5,
        'checksum' => $checksum,
        'filetype' => $filetype,
        'filesize' => filesize($filename),
      ))
  ->execute();
    }

    // Create the entries in the advagg_bundles table.
    // TODO Please review the conversion of this statement to the D7 database API syntax.
    /* db_query("INSERT INTO {advagg_bundles} (bundle_md5, filename_md5, counter, porder, root, timestamp) VALUES ('%s', '%s', '%d', '%d', '%d', '%d')", $bundle_md5, $filename_md5, 0, $order, $root, REQUEST_TIME) */
    $id = db_insert('advagg_bundles')
  ->fields(array(
      'bundle_md5' => $bundle_md5,
      'filename_md5' => $filename_md5,
      'counter' => 0,
      'porder' => $order,
      'root' => $root,
      'timestamp' => REQUEST_TIME,
    ))
  ->execute();
  }

  lock_release($lock_name);
}

/**
 * See if this bundle has been built.
 *
 * @param $filepath
 *   filename
 * @return
 *   Boolean indicating if the bundle already exists.
 */
function advagg_bundle_built($filepath) {
  // Don't use the cache if not selected.
  if (!variable_get('advagg_bundle_built_mode', ADVAGG_BUNDLE_BUILT_MODE)) {
    advagg_clearstatcache(TRUE, $filepath);
    return file_exists($filepath);
  }

  $data = advagg_get_bundle_from_filename(basename($filepath));
  if (is_array($data)) {
    list($type, $md5, $counter) = $data;
  }
  else {
    return FALSE;
  }

  $data = cache_get($filepath, 'cache_advagg');
  if (isset($data->data)) {
    // Refresh timestamp if older then 12 hours.
    if (REQUEST_TIME - $data->data > variable_get('advagg_file_last_used_interval', ADVAGG_FILE_LAST_USED_INTERVAL)) {
      cache_set($filepath, REQUEST_TIME, 'cache_advagg', CACHE_PERMANENT);
      // TODO Please review the conversion of this statement to the D7 database API syntax.
      /* db_query("UPDATE {advagg_bundles} SET timestamp = %d WHERE bundle_md5 = '%s'", REQUEST_TIME, $md5) */
      db_update('advagg_bundles')
  ->fields(array(
        'timestamp' => REQUEST_TIME,
      ))
  ->condition('bundle_md5', $md5)
  ->execute();
    }
    return TRUE;
  }

  // If not in cache check disk.
  advagg_clearstatcache(TRUE, $filepath);
  if (file_exists($filepath)) {
    if (@filesize($filepath) == 0) {
      return FALSE;
    }
  }
  else {
    return FALSE;
  }
  // File existed on disk; place in cache.
  cache_set($filepath, REQUEST_TIME, 'cache_advagg', CACHE_PERMANENT);
  // TODO Please review the conversion of this statement to the D7 database API syntax.
  /* db_query("UPDATE {advagg_bundles} SET timestamp = %d WHERE bundle_md5 = '%s'", REQUEST_TIME, $md5) */
  db_update('advagg_bundles')
  ->fields(array(
    'timestamp' => REQUEST_TIME,
  ))
  ->condition('bundle_md5', $md5)
  ->execute();
  return TRUE;
}

/**
 * @todo Please document this function.
 * @see http://drupal.org/node/1354
 */
function advagg_get_bundle_from_filename($filename) {
  // Verify requested filename has the correct pattern.
  if (!preg_match('/^(j|cs)s_[0-9a-f]{32}_\d+\.(j|cs)s$/', $filename)) {
    return t('Wrong Pattern.');
  }

  // Get type
  $type = substr($filename, 0, strpos($filename, '_'));

  // Get extension
  $ext = substr($filename, strpos($filename, '.', 37) + 1);

  // Make sure extension is the same as the type.
  if ($ext != $type) {
    return t('Type does not match extension.');
  }

  // Extract info from wanted filename.
  if ($type == 'css') {
    $md5 = substr($filename, 4, 32);
    $counter = substr($filename, 37, strpos($filename, '.', 38) -37);
  }
  elseif ($type == 'js') {
    $md5 = substr($filename, 3, 32);
    $counter = substr($filename, 36, strpos($filename, '.', 37) -36);
  }
  else {
    return t('Wrong file type.');
  }

  return array($type, $md5, $counter);
}

/**
 * Rebuild a bundle.
 *
 * @param $bundle_md5
 *   Bundle's machine name.
 * @param $counter
 *   Counter value.
 * @param $force
 *   Rebuild even if file already exists.
 */
function advagg_rebuild_bundle($bundle_md5, $counter = '', $force = FALSE) {
  global $conf, $_advagg;
  list($filetype, $files) = advagg_get_files_in_bundle($bundle_md5);

  $conf['advagg_async_generation'] = FALSE;
  $good = advagg_css_js_file_builder($filetype, $files, '', $counter, $force, $bundle_md5);
  if (!$good) {
    watchdog('advagg', 'This bundle could not be generated correctly. Bundle MD5: %md5', array('%md5' => $bundle_md5));
  }
  else {
    $_advagg['rebuilt'][] = $bundle_md5;
  }
  return $good;
}

/**
 * Get list of files and the filetype given a bundle md5.
 *
 * @param $bundle_md5
 *   Bundle's machine name.
 * @return
 *   array ($filetype, $files)
 */
function advagg_get_files_in_bundle($bundle_md5) {
  $files = array();
  $filetype = NULL;
  $results = db_query("SELECT filename, filetype FROM {advagg_files} AS af INNER JOIN {advagg_bundles} AS ab USING ( filename_md5 ) WHERE bundle_md5 = :bundle_md5 ORDER BY porder ASC", array(':bundle_md5' => $bundle_md5));
  while ($row = db_fetch_array($results)) {
    $files[] = $row['filename'];
    $filetype = $row['filetype'];
  }

  return array($filetype, $files);
}
