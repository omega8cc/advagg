<?php

/**
 * @file
 * Advanced CSS/JS aggregation module
 *
 * These functions are needed for cache misses.
 */

// Database operations.
/**
 * Insert/Update data in advagg_files, advagg_aggregates, &
 * advagg_aggregates_versions tables.
 *
 * @param $files
 *   List of files in the aggregate as well as the aggregate name.
 * @param $type
 *   css or js.
 * @param $root
 *   Is this a root aggregate.
 *
 * @return bool
 *   Return TRUE if anything was written to the database.
 */
function advagg_insert_update_db($files, $type, $root) {
  // Record if a database write was done.
  $write_done = FALSE;

  // Loop through all files.
  foreach ($files as $aggregate_file_name => $values) {
    // Build lock name variable.
    $lock_name = 'advagg_insert_update_db:' . $values['aggregate_filenames_hash'] . ADVAGG_SPACE . $values['aggregate_contents_hash'];

    // If lock is already acquired, return here.
    if (!lock_acquire($lock_name, 5)) {
      continue;
    }

    // Insert aggregate version information into advagg_aggregates_versions.
    // If it already exists, skip the rest of the logic in this loop.
    if (advagg_insert_aggregate_version($values['aggregate_filenames_hash'], $values['aggregate_contents_hash'], $root) == FALSE) {
      continue;
    }
    $write_done = TRUE;

    // Insert files into the advagg_files table if it doesn't exist.
    // Update if needed.
    advagg_insert_update_files($values['files'], $type);

    // Insert aggregate into the advagg_aggregates table if it doesn't exist.
    advagg_insert_aggregate($values['files'], $values['aggregate_filenames_hash']);

    // Release Lock.
    lock_release($lock_name);
  }
  return $write_done;
}

/**
 * Insert data in the advagg_aggregates_versions table.
 *
 * @param $aggregate_filenames_hash
 *   Hash of the groupings of files.
 * @param $aggregate_contents_hash
 *   Hash of the files contents.
 * @param $root
 *   Is this a root aggregate.
 *
 * @return bool
 *   Return TRUE if anything was written to the database.
 */
function advagg_insert_aggregate_version($aggregate_filenames_hash, $aggregate_contents_hash, $root) {
  // Build query checking if this aggregate exists in the
  // advagg_aggregates_versions table.
  $aggregate_exists = db_select('advagg_aggregates_versions', 'aav')
    ->fields('aav')
    ->condition('aggregate_filenames_hash', $aggregate_filenames_hash)
    ->condition('aggregate_contents_hash', $aggregate_contents_hash)
    ->execute()
    ->fetchAssoc();
  // If this aggregate version already exists, return FALSE here.
  if (!empty($aggregate_exists)) {
    return FALSE;
  }

  // Save new aggregate into the database.
  $record = array(
    'aggregate_filenames_hash' => $aggregate_filenames_hash,
    'aggregate_contents_hash' => $aggregate_contents_hash,
    'atime' => 0,
    'root' => $root,
  );
  return drupal_write_record('advagg_aggregates_versions', $record);
}

/**
 * Insert/Update data in the advagg_aggregates table.
 *
 * @param $aggregate_filenames_hash
 *   Hash of the groupings of files.
 * @param $files
 *   List of files in the aggregate including files meta data.
 *
 * @return bool
 *   Return TRUE if anything was written to the database.
 */
function advagg_insert_aggregate($files, $aggregate_filenames_hash) {
  // Record if a database write was done.
  $write_done = FALSE;

  $count = 0;
  foreach ($files as $file_meta_data) {
    $count++;
    // Check if the aggregate is in the database.
    $file_exists = db_select('advagg_aggregates', 'aa')
      ->fields('aa')
      ->condition('aggregate_filenames_hash', $aggregate_filenames_hash)
      ->condition('filename_hash', $file_meta_data['filename_hash'])
      ->execute()
      ->fetchAssoc();
    // Skip if it already exists.
    if (!empty($file_exists)) {
      continue;
    }

    // Write record into the database.
    $record = array(
      'aggregate_filenames_hash' => $aggregate_filenames_hash,
      'filename_hash' => $file_meta_data['filename_hash'],
      'porder' => $count,
    );
    if (drupal_write_record('advagg_aggregates', $record)) {
      $write_done = TRUE;
    }
  }
  return $write_done;
}

/**
 * Insert/Update data in the advagg_files table.
 *
 * @param $files
 *   List of files in the aggregate including files meta data.
 * @param $type
 *   css or js.
 *
 * @return bool
 *   Return TRUE if anything was written to the database.
 */
function advagg_insert_update_files($files, $type) {
  // Record if a database write was done.
  $write_done = FALSE;

  foreach ($files as $filename => $file_meta_data) {
    // Create record.
    $record = array(
      'filename' => $filename,
      'filename_hash' => $file_meta_data['filename_hash'],
      'content_hash' => $file_meta_data['content_hash'],
      'filetype' => $type,
      'filesize' => $file_meta_data['filesize'],
      'mtime' => $file_meta_data['mtime'],
      'linecount' => $file_meta_data['linecount']
    );

    // Check the file in the database.
    $file_exists = db_select('advagg_files', 'af')
      ->fields('af')
      ->condition('filename_hash', $file_meta_data['filename_hash'])
      ->execute()
      ->fetchAssoc();
    if (empty($file_exists)) {
      // Insert into database.
      $record['changes'] = 1;
      if (drupal_write_record('advagg_files', $record)) {
        $write_done = TRUE;
      }
    }
    else {
      // Take changes counter out of the diff equation.
      $changes = $file_exists['changes'];
      unset($file_exists['changes']);

      // If something is different, update.
      $diff = array_diff($record, $file_exists);
      if (!empty($diff)) {
        $diff['changes'] = $changes + 1;
        $diff['filename_hash'] = $record['filename_hash'];
        if (drupal_write_record('advagg_files', $diff, array('filename_hash'))) {
          $write_done = TRUE;
        }
      }
    }
  }
  return $write_done;
}

/**
 * Update atime advagg_aggregates_versions table & cache_advagg_info cache bin.
 *
 * @param $files
 *   List of files in the aggregate as well as the aggregate name.
 *
 * @return bool
 *   Return TRUE if anything was written to the database.
 */
function advagg_update_all_atimes($files) {
  // Record if a database write was done.
  $write_done = FALSE;

  // Loop through all files.
  foreach ($files as $aggregate_file_name => $values) {
    // Build lock name variable.
    if (advagg_update_atime($values['aggregate_filenames_hash'], $values['aggregate_contents_hash'])) {
      $write_done = TRUE;
    }
  }

  return $write_done;
}

// CSS/JS operations.
/**
 * Convert the array from *_get_js() into something more like the css array.
 *
 * @param $files
 *   List of files including files meta data.
 *
 * @return array
 *   An array that can be used by advagg_build_aggregate_plans
 */
function advagg_convert_js_structure($files) {
  $return = array();
  $counter = 0;
  foreach ($files as $group_name => $group) {
    foreach ($group as $values) {
      // Add in browsers.
      if (!isset($values['browsers'])) {
        $values['browsers'] = array(
          '!IE' => TRUE,
          'IE' => TRUE,
        );
      }
      // Add in media.
      if (!isset($values['media'])) {
        $values['media'] = 'all';
      }
      $return[$counter]['items'][] = $values;
    }
    $counter++;
  }
  return array($return);
}

// File operations.
/**
 * Given a group of files calculate what the aggregate filename will be.
 *
 * @param $groups
 *   An array of CSS/JS groups.
 * @param $type
 *   String; css or js.
 * @return array
 *   Files array.
 */
function advagg_generate_filenames($groups, $type) {
  $files = array();
  foreach ($groups as $key => $data) {
    foreach ($data as $values) {
      // Build a list of filenames.
      $group_of_filenames = array();
      foreach ($values as $file_info) {
        $group_of_filenames[] = $file_info['data'];
      }

      // Get the aggregate filename & info about each file.
      $aggregate_info = advagg_get_aggregate_info_from_files($type, $group_of_filenames);
      $values['files'] = $aggregate_info[1];
      $values['aggregate_filenames_hash'] = $aggregate_info[2];
      $values['aggregate_contents_hash'] = $aggregate_info[3];

      // Add information to the files array.
      $files[$aggregate_info[0]] = $values;
    }
  }
  return $files;
}

/**
 * Given a group of files calculate various hashes and gather meta data.
 *
 * @param $type
 *   String; css or js.
 * @param $files
 *   An array of CSS/JS files.
 * @return
 *   array
 */
function advagg_get_aggregate_info_from_files($type, $files) {
  $filename_hashes = array();
  $content_hashes = array();
  $filenames = array();
  foreach ($files as $filename) {
    $info = advagg_get_info_on_file($filename);
    // Skip if file doesn't exist.
    if (empty($info['content_hash'])) {
      continue;
    }

    // Add info to arrays.
    $filename_hashes[] = $info['filename_hash'];
    $content_hashes[] = $info['content_hash'];
    $filenames[$filename] = $info;
  }

  // Generate filename.
  $aggregate_filenames_hash = drupal_hash_base64(implode('', $filename_hashes));
  $aggregate_contents_hash = drupal_hash_base64(implode('', $content_hashes));
  $aggregate_filename = advagg_build_filename($type, $aggregate_filenames_hash, $aggregate_contents_hash);
  return array($aggregate_filename, $filenames, $aggregate_filenames_hash, $aggregate_contents_hash);
}

/**
 * Given a filename calculate various hashes and gather meta data.
 *
 * @param $filename
 *   String; filename containing path information
 * @param $bypass_cache
 *   Bool: TRUE to bypass the cache.
 * @return
 *   array
 */
function advagg_get_info_on_file($filename, $bypass_cache = FALSE) {
  $filename_hash = drupal_hash_base64($filename);

  // Use caching to avoid hitting disk if possible.
  $cache_id = 'advagg:file:' . $filename_hash;
  if (!$bypass_cache && variable_get('advagg_use_file_cache', ADVAGG_USE_FILE_CACHE) && $cache = cache_get($cache_id, 'cache_advagg_info')) {
    return $cache->data;
  }

  // Clear PHP's internal file status cache.
  advagg_clearstatcache($filename);

  // Remove file in the cache if it does not exist.
  if (!file_exists($filename)) {
    cache_clear_all($cache_id, 'cache_advagg_info', FALSE);
    // Only return filename hash.
    return array('filename_hash' => $filename_hash);
  }

  // Get the file contents
  $file_contents = file_get_contents($filename);
  // Get the number of lines.
  $linecount = substr_count($file_contents, "\n");

  // Build meta data array and set cache.
  $data = array(
    'filesize' => filesize($filename),
    'mtime' => @filemtime($filename),
    'filename_hash' => $filename_hash,
    'content_hash' => drupal_hash_base64($file_contents),
    'linecount' => $linecount,
  );
  if (variable_get('advagg_use_file_cache', ADVAGG_USE_FILE_CACHE)) {
    cache_set($cache_id, $data, 'cache_advagg_info', CACHE_PERMANENT);
  }
  return $data;
}

/**
 * Build the filename.
 *
 * @param $filetype
 *   css or js.
 * @param $aggregate_filenames_hash
 *   Hash of the groupings of files.
 * @param $aggregate_contents_hash
 *   Hash of the files contents.
 */
function advagg_build_filename($type, $aggregate_filenames_hash, $aggregate_contents_hash) {
  $hooks_hash = advagg_get_hooks_hash();
  return $type . ADVAGG_SPACE .
    $aggregate_filenames_hash . ADVAGG_SPACE .
    $aggregate_contents_hash . ADVAGG_SPACE .
    $hooks_hash . '.' . $type;
}

/**
 * Wrapper around clearstatcache so it can use php 5.3's new features.
 *
 * @param $clear_realpath_cache
 *   Bool.
 * @param $filename
 *   String.
 * @return
 *   value from clearstatcache().
 */
function advagg_clearstatcache($filename = NULL) {
  static $php530;
  if (!isset($php530)) {
    $php530 = version_compare(PHP_VERSION, '5.3.0', '>=');
  }

  if ($php530) {
    return clearstatcache(TRUE, $filename);
  }
  else {
    return clearstatcache();
  }
}

// Modify CSS/JS arrays.
/**
 * Group the CSS/JS into the biggest buckets possible.
 *
 * @param $files_to_aggregate
 *   An array of CSS/JS groups.
 * @return array
 *   New version of groups.
 */
function advagg_generate_groups($files_to_aggregate) {
  $groups = array();
  $count = 0;
  $location = 0;
  $media = '';
  $defer = '';
  $cache = '';
  $scope = '';
  $browsers = array();
  foreach ($files_to_aggregate as $data) {
    foreach ($data as $values) {

      // Group into the biggest buckets possible.
      foreach ($values['items'] as $file_info) {
        // Check to see if media, browsers, defer, cache, or scope has changed
        // from the previous run of this loop.
        $changed = FALSE;
        if (isset($file_info['media']) && $media != $file_info['media']) {
          $changed = TRUE;
          $media = $file_info['media'];
        }
        if (isset($file_info['browsers'])) {
          $diff = array_diff($browsers, $file_info['browsers']);
          if (!empty($diff)) {
            $changed = TRUE;
            $browsers = $file_info['browsers'];
          }
        }
        if (isset($file_info['defer']) && $defer != $file_info['defer']) {
          $changed = TRUE;
          $defer = $file_info['defer'];
        }
        if (isset($file_info['cache']) && $cache != $file_info['cache']) {
          $changed = TRUE;
          $cache = $file_info['cache'];
        }
        if (isset($file_info['scope']) && $scope != $file_info['scope']) {
          $changed = TRUE;
          $scope = $file_info['scope'];
        }

        // If one of the above options changed, it needs to be in a different
        // aggregate.
        if ($changed) {
          $count++;
        }
        $groups[$location][$count][] = $file_info;
      }
    }
    // Grouping if inline is mixed between files.
    $location++;
  }
  return $groups;
}

/**
 * Replacement for drupal_build_css_cache() and drupal_build_js_cache().
 *
 * @param $files_to_aggregate
 *   An array of CSS/JS groups.
 * @param $type
 *   css or js.
 * @return
 *   array of aggregate files.
 */
function advagg_build_aggregate_plans($files_to_aggregate, $type) {
  // Place into biggest grouping possible.
  $groups = advagg_generate_groups($files_to_aggregate);

  // Get filenames.
  $files = advagg_generate_filenames($groups, $type);

  // Insert/Update Database.
  advagg_insert_update_db($files, $type, 1);
  // Update atimes for root.
  advagg_update_all_atimes($files);

  // Run hooks to modify the aggregate.
  // Call hook_advagg_build_aggregate_plans_alter().
  $modified = FALSE;
  drupal_alter('advagg_build_aggregate_plans', $files, $modified, $type);

  // If the hook above modified anything, re-insert into database.
  if ($modified) {
    // Insert/Update Database.
    advagg_insert_update_db($files, $type, 0);
    // Update atimes for non root.
    advagg_update_all_atimes($files);
  }

  // Get file paths.
  list($css_path, $js_path) = advagg_get_root_files_dir();

  // Build the plan.
  $plans = array();
  foreach ($files as $agg_filename => $values) {
    $first = reset($values);
    $url = ($type == 'css') ? $css_path[0] : $js_path[0];
    $path = ($type == 'css') ? $css_path[1] : $js_path[1];
    $plans[] = array(
      'data' => $url . '/' . $agg_filename,
      'media' => $first['media'],
      'browsers' => $first['browsers'],
      'type' => $first['type'],
      'items' => $values,
      'filepath' => $path . '/' . $agg_filename,
      'filename' => $agg_filename,
    );
  }

  // Create the aggregate files.
  advagg_create_aggregate_files($plans);

  return $plans;
}

/**
 * Do a non blocking request of the aggregate if it does not exist.
 *
 * @param $plans
 *   An array of aggregate file names.
 */
function advagg_create_aggregate_files($plans) {
  $urls = array();
  $filenames = array();
  foreach ($plans as $plan) {
    // Use cache here?
    if (!file_exists($plan['filepath'])) {
      $urls[] = file_create_url($plan['data']);
      $filenames[] = $plan['filename'];
    }
  }
  if (empty($urls)) {
    return;
  }

  // If the httprl module exists and we want to use it.
  if (module_exists('httprl') && variable_get('advagg_use_httprl', ADVAGG_USE_HTTPRL)) {
    // Set options for the requests.
    $options = array(
      'blocking' => FALSE,
      'domain_connections' => 8,
      'headers' => array('Accept-Encoding' => 'gzip, deflate'),
    );
    // Queue requests.
    httprl_request($urls, $options);
    // Execute requests.
    httprl_send_request();
  }
  else {
    // Otherwise call the file generation function directly.
    module_load_include('inc', 'advagg', 'advagg.missing');
    foreach ($filenames as $filename) {
      // Only create the file if we have a lock.
      $lock_name = 'advagg_' . $filename;
      if (lock_acquire($lock_name, 10)) {
        advagg_missing_create_file($filename);
        lock_release($lock_name);
      }
    }
  }
}
