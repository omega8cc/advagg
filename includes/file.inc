<?php

/**
 * @file
 * File functions for the advagg module.
 */

/**
 * Generate a checksum for a given filename.
 *
 * @param $filename
 *   filename
 * @return
 *   Checksum value.
 */
function advagg_checksum($filename) {
  advagg_clearstatcache(TRUE, $filename);
  if (file_exists($filename)) {
    $mode = variable_get('advagg_checksum_mode', ADVAGG_CHECKSUM_MODE);
    if ($mode == 'filemtime' && function_exists('filemtime')) {
      $checksum = @filemtime($filename);
      if ($checksum === FALSE) {
        touch($filename);
        advagg_clearstatcache(TRUE, $filename);
        $checksum = @filemtime($filename);
        // Use md5 as a last option.
        if ($checksum === FALSE) {
          $checksum = md5(file_get_contents($filename));
        }
      }
    }
    elseif ($mode = 'md5' && function_exists('md5')) {
      $checksum = md5(file_get_contents($filename));
    }
  }
  else {
    $checksum = '-1';
  }
  return $checksum;
}

/**
 * Callback to delete files modified more than a set time ago.
 *
 * @param $filename
 *   name of a file to check how old it is.
 */
function advagg_delete_file_if_stale($filename) {
  // Do not process .gz files
  if (strpos($filename, '.gz') !== FALSE) {
    return;
  }
  $now = REQUEST_TIME;
  $file_last_mod = variable_get('advagg_stale_file_threshold', ADVAGG_STALE_FILE_THRESHOLD);
  $file_last_used = variable_get('advagg_stale_file_last_used_threshold', ADVAGG_STALE_FILE_LAST_USED_THRESHOLD);

  // Default mtime stale file threshold is 6 days.
  advagg_clearstatcache(TRUE, $filename);
  if ($now - filemtime($filename) <= $file_last_mod) {
    return;
  }

  // Check to see if this file is still in use.
  $data = cache_get($filename, 'cache_advagg');
  if (!empty($data->data)) {
    advagg_clearstatcache(TRUE, $filename);
    $file_last_a = @fileatime($filename);
    $file_last_agz = @fileatime($filename . '.gz');
    $file_last_a = max($file_last_a, $file_last_agz);
    if ($now - $data->data > $file_last_used && $now - $file_last_a > $file_last_used) {
      // Delete file if it hasn't been used in the last 3 days.
      file_unmanaged_delete($filename);
      file_unmanaged_delete($filename . '.gz');
    }
    else {
      // Touch file so we don't check again for another 6 days.
      touch($filename);
    }
  }
  else {
    // Delete file if it is not in the cache.
    file_unmanaged_delete($filename);
    file_unmanaged_delete($filename . '.gz');
  }
}

/**
 * Aggregate CSS/JS files, putting them in the files directory.
 *
 * @see drupal_build_js_cache()
 * @see drupal_build_css_cache()
 *
 * @param $type
 *   js or css
 * @param $files
 *   An array of JS files to aggregate and compress into one file.
 * @param $query_string
 *   (optional) Query string to add on to the file if bundle isn't ready.
 * @param $counter
 *   (optional) Counter value.
 * @param $force
 *   (optional) Rebuild even if file already exists.
 * @param $md5
 *   (optional) Bundle's machine name.
 * @return
 *   array with the filepath as the key and prefix and suffix in another array.
 */
function advagg_css_js_file_builder($type, $files, $query_string = '', $counter = FALSE, $force = FALSE, $md5 = '') {
  global $_advagg, $base_path;
  $data = '';

  // Try cache first. When ever the counter changes this cache gets reset.
  $schema = advagg_get_server_schema();
  $cached_data_key = 'advagg_file_builder_' . md5($schema . implode('', array_filter(array_unique($files))));
  if (!$force) {
    // Try cache first; cache table is cache_advagg_bundle_reuse.
    $cached_data = advagg_cached_bundle_get($cached_data_key, 'file_builder_cache_object');
    if (!empty($cached_data)) {
      foreach ($cached_data as $filepath => $values) {
        // Ping cache.
        advagg_bundle_built($filepath);
      }
      return $cached_data;
    }
  }

  list($css_path, $js_path) = advagg_get_root_files_dir();
  if ($type == 'js') {
    $file_type_path = $js_path[0];
  }
  if ($type == 'css') {
    $file_type_path = $css_path[0];
  }
  // Send $files, get filename back
  $filenames = advagg_get_filename($files, $type, $counter, $md5);
  if (empty($filenames)) {
    return FALSE;
  }

  // Debugging.
  if (variable_get('advagg_debug', ADVAGG_DEBUG)) {
    $_advagg['debug']['file_builder_get_filenames'][] = array(
      'key' => $cached_data_key,
      'filenames' => $filenames,
    );
  }
  $output = array();
  $locks = array();
  $cacheable = TRUE;
  $files_used = array();
  foreach ($filenames as $info) {
    $filename = $info['filename'];
    $files = $info['files'];
    $bundle_md5 = $info['bundle_md5'];
    $prefix = '';
    $suffix = '';
    $filepath = $file_type_path . '/' . $filename;

    // Invoke hook_advagg_js_extra_alter() or hook_advagg_css_extra_alter to
    // give installed modules a chance to modify the prefix or suffix for a
    // given filename.
    $values = array($filename, $bundle_md5, $prefix, $suffix);
    drupal_alter('advagg_' . $type . '_extra', $values);
    list($filename, $bundle_md5, $prefix, $suffix) = $values;

    // Check that the file exists & filesize is not zero
    $built = advagg_bundle_built($filepath);

    if (!$built || $force) {
      // Generate on request?
      if (variable_get('advagg_async_generation', ADVAGG_ASYNC_GENERATION) && !$force && empty($_GET['generator'])) {
        // Build request.
        $redirect_counter = isset($_GET['redirect_counter']) ? intval($_GET['redirect_counter']) : 0;
        $url = _advagg_build_url($filepath . '?generator=1&redirect_counter=' . $redirect_counter);
        $headers = array(
          'Host' => $_SERVER['HTTP_HOST'],
          'Connection' => 'close',
        );

        // Request file.
        if (function_exists('stream_socket_client') && function_exists('stream_select')) {
          advagg_async_connect_http_request($url, array('headers' => $headers));
        }
        else {
          // Set timeout.
          $socket_timeout = ini_set('default_socket_timeout', variable_get('advagg_socket_timeout', ADVAGG_SOCKET_TIMEOUT));
          drupal_http_request($url, array('headers' => $headers));
          ini_set('default_socket_timeout', $socket_timeout);
        }

        // Return filepath if we are going to wait for the bundle to be
        // generated or if the bundle already exists.
        if (variable_get('advagg_aggregate_mode', ADVAGG_AGGREGATE_MODE) < 2 || advagg_bundle_built($filepath)) {
          $output[$filepath] = array(
            'prefix' => $prefix,
            'suffix' => $suffix,
            'files' => array_map('advagg_return_true', array_flip($files)),
          );
        }
        else {
          // Aggregate isn't built yet, send back the files that where going to
          // be in it.
          foreach ($files as $file) {
            $output[$file . $query_string] = array(
              'prefix' => '',
              'suffix' => '',
              'files' => array($file . $query_string => TRUE),
            );
          }
          $cacheable = FALSE;
          advagg_disable_page_cache();
        }
        continue;
      }

      // Only generate once.
      $lock_name = 'advagg_' . $filename;
      if (!lock_acquire($lock_name)) {
        if (variable_get('advagg_aggregate_mode', ADVAGG_AGGREGATE_MODE) == 0 ) {
          $locks[] = array($lock_name => $filepath);
          $output[$filepath] = array(
            'prefix' => $prefix,
            'suffix' => $suffix,
            'files' => array_map('advagg_return_true', array_flip($files)),
          );
        }
        else {
          // Aggregate isn't built yet, send back the files that where going
          // to be in it.
          foreach ($files as $file) {
            $output[$file . $query_string] = array(
              'prefix' => '',
              'suffix' => '',
              'files' => array($file . $query_string => TRUE),
            );
          }
          $cacheable = FALSE;
          advagg_disable_page_cache();
        }
        continue;
      }

      if ($type == 'css') {
        $data = advagg_build_css_bundle($files);
      }
      elseif ($type == 'js') {
        $data = advagg_build_js_bundle($files);
      }

      // Invoke hook_advagg_js_alter() or hook_advagg_css_alter to give
      // installed modules a chance to modify the data in the bundle if
      // necessary.
      drupal_alter('advagg_' . $type, $data, $files, $bundle_md5);
      $files_used = array_merge($files_used, $files);

      // If data is empty then do not include this bundle in the final output.
      if (empty($data) && !$force) {
        lock_release($lock_name);
        continue;
      }

      // Create the advagg_$type/ within the files folder.
      file_prepare_directory($file_type_path, FILE_CREATE_DIRECTORY);

      // Write file. default function called: advagg_file_saver
      $function = variable_get('advagg_file_save_function', ADVAGG_FILE_SAVE_FUNCTION);
      $good = $function($data, $filepath, $force, $type);

      // Release lock.
      lock_release($lock_name);

      // If file save was not good then downgrade to non aggregated mode.
      if (!$good) {
        $output[$filepath] = FALSE;
        $cacheable = FALSE;
        continue;
      }
    }
    else {
      $files_used = array_merge($files_used, $files);
    }
    $output[$filepath] = array(
      'prefix' => $prefix,
      'suffix' => $suffix,
      'files' => array_map('advagg_return_true', array_flip($files)),
    );
  }

  // Wait for all locks before returning.
  if (!empty($locks)) {
    foreach ($locks as $lock_name => $filepath) {
      lock_wait($lock_name);
      if (!advagg_bundle_built($filepath)) {
        $output[$filepath] = FALSE;
      }
    }
  }

  if (empty($output)) {
    $output[] = FALSE;
    return $output;
  }

  // Cache the output
  if (!$force && $cacheable) {
    $new_cached_data_key = 'advagg_file_builder_' . md5($schema . implode('', array_filter(array_unique($files_used))));
    // Verify the files in equals the files out.
    if ($new_cached_data_key == $cached_data_key) {
      cache_set($cached_data_key, $output, 'cache_advagg_bundle_reuse', CACHE_TEMPORARY);
    }
  }

  return $output;
}

/**
 * Use a cache table to see if a file exists.
 *
 * @param $filename
 *   name of file
 * @return
 *   TRUE or FALSE
 */
function advagg_file_exists($filename) {
  static $files = array();
  if (empty($files)) {
    $data = cache_get('advagg_file_checksum', 'cache');
    if (empty($data->data)) {
      $result = db_query("SELECT filename, checksum FROM {advagg_files}");
      while ($row = $result->fetchAssoc()) {
        $files[$row['filename']] = $row['checksum'];
      }
      cache_set('advagg_file_checksum', $files, 'cache', CACHE_TEMPORARY);
    }
    else {
      $files = $data->data;
    }
  }
  if (!empty($files[$filename]) && $files[$filename] != -1) {
    return TRUE;
  }
  else {
    advagg_clearstatcache(TRUE, $filename);
    return file_exists($filename);
  }
}

/**
 * Save a string to the specified destination. Verify that file size is not zero.
 *
 * @param $data
 *   A string containing the contents of the file.
 * @param $dest
 *   A string containing the destination location.
 * @return
 *   Boolean indicating if the file save was successful.
 */
function advagg_file_saver($data, $dest, $force, $type) {
  // Make sure the tmp folder is ready for use
  $tmp = file_directory_temp();
  file_prepare_directory($tmp, FILE_CREATE_DIRECTORY);

  // Create the JS file.
  $file_save_data = 'file_save_data';
  $custom_path = variable_get('advagg_custom_files_dir', ADVAGG_CUSTOM_FILES_DIR);
  if (!empty($custom_path)) {
    $file_save_data = 'advagg_file_save_data';
  }

  if (!$file_save_data($data, $dest, FILE_EXISTS_REPLACE)) {
    return FALSE;
  }

  // Make sure filesize is not zero.
  advagg_clearstatcache(TRUE, $dest);
  if (@filesize($dest) == 0 && !empty($data)) {
    if (!$file_save_data($data, $dest, FILE_EXISTS_REPLACE)) {
      return FALSE;
    }
    advagg_clearstatcache(TRUE, $dest);
    if (@filesize($dest) == 0 && !empty($data)) {
      // Filename is bad, create a new one next time.
      file_unmanaged_delete($dest);
      return FALSE;
    }
  }

  if (variable_get('advagg_gzip_compression', ADVAGG_GZIP_COMPRESSION) && extension_loaded('zlib')) {
    $gzip_dest = $dest . '.gz';
    advagg_clearstatcache(TRUE, $gzip_dest);
    if (!file_exists($gzip_dest) || $force) {
      $gzip_data = gzencode($data, 9, FORCE_GZIP);
      if (!$file_save_data($gzip_data, $gzip_dest, FILE_EXISTS_REPLACE)) {
        return FALSE;
      }

      // Make sure filesize is not zero.
      advagg_clearstatcache(TRUE, $gzip_dest);
      if (@filesize($gzip_dest) == 0 && !empty($gzip_data)) {
        if (!$file_save_data($gzip_data, $gzip_dest, FILE_EXISTS_REPLACE)) {
          return FALSE;
        }
        advagg_clearstatcache(TRUE, $gzip_dest);
        if (@filesize($gzip_dest) == 0 && !empty($gzip_data)) {
          // Filename is bad, create a new one next time.
          file_unmanaged_delete($gzip_dest);
          return FALSE;
        }
      }
    }
  }

  // Make sure .htaccess file exists.
  advagg_htaccess_check_generate($dest);

  cache_set($dest, REQUEST_TIME, 'cache_advagg', CACHE_PERMANENT);
  return TRUE;
}

/**
 * ***MODIFIED CORE FUNCTIONS BELOW***
 *
 * @see file_save_data()
 * @see file_move()
 * @see file_copy()
 */

/**
 * Save a string to the specified destination.
 *
 * @see file_save_data()
 *
 * @param $data A string containing the contents of the file.
 * @param $dest A string containing the destination location.
 * @param $replace Replace behavior when the destination file already exists.
 *   - FILE_EXISTS_REPLACE - Replace the existing file
 *   - FILE_EXISTS_RENAME - Append _{incrementing number} until the filename is unique
 *   - FILE_EXISTS_ERROR - Do nothing and return FALSE.
 *
 * @return A string containing the resulting filename or 0 on error
 */
function advagg_file_save_data($data, $dest, $replace = FILE_EXISTS_RENAME) {
  $temp = file_directory_temp();
  // On Windows, tempnam() requires an absolute path, so we use realpath().
  $file = tempnam(realpath($temp), 'file');
  if (!$fp = fopen($file, 'wb')) {
    drupal_set_message(t('The file could not be created.'), 'error');
    return 0;
  }
  fwrite($fp, $data);
  fclose($fp);

  if (!advagg_file_move($file, $dest, $replace)) {
    return 0;
  }

  return $file;
}

/**
 * Moves a file to a new location.
 *
 * @see file_move()
 *
 * - Checks if $source and $dest are valid and readable/writable.
 * - Performs a file move if $source is not equal to $dest.
 * - If file already exists in $dest either the call will error out, replace the
 *   file or rename the file based on the $replace parameter.
 *
 * @param $source
 *   Either a string specifying the file location of the original file or an
 *   object containing a 'filepath' property. This parameter is passed by
 *   reference and will contain the resulting destination filename in case of
 *   success.
 * @param $dest
 *   A string containing the directory $source should be copied to. If this
 *   value is omitted, Drupal's 'files' directory will be used.
 * @param $replace
 *   Replace behavior when the destination file already exists.
 *   - FILE_EXISTS_REPLACE: Replace the existing file.
 *   - FILE_EXISTS_RENAME: Append _{incrementing number} until the filename is
 *     unique.
 *   - FILE_EXISTS_ERROR: Do nothing and return FALSE.
 * @return
 *   TRUE for success, FALSE for failure.
 */
function advagg_file_move(&$source, $dest = 0, $replace = FILE_EXISTS_RENAME) {
  $path_original = is_object($source) ? $source->filepath : $source;

  if (advagg_file_copy($source, $dest, $replace)) {
    $path_current = is_object($source) ? $source->filepath : $source;

    if ($path_original == $path_current || file_unmanaged_delete($path_original)) {
      return 1;
    }
    drupal_set_message(t('The removal of the original file %file has failed.', array('%file' => $path_original)), 'error');
  }
  return 0;
}

/**
 * Copies a file to a new location.
 *
 * @see file_copy()
 *
 * This is a powerful function that in many ways performs like an advanced
 * version of copy().
 * - Checks if $source and $dest are valid and readable/writable.
 * - Performs a file copy if $source is not equal to $dest.
 * - If file already exists in $dest either the call will error out, replace the
 *   file or rename the file based on the $replace parameter.
 *
 * @param $source
 *   Either a string specifying the file location of the original file or an
 *   object containing a 'filepath' property. This parameter is passed by
 *   reference and will contain the resulting destination filename in case of
 *   success.
 * @param $dest
 *   A string containing the directory $source should be copied to. If this
 *   value is omitted, Drupal's 'files' directory will be used.
 * @param $replace
 *   Replace behavior when the destination file already exists.
 *   - FILE_EXISTS_REPLACE: Replace the existing file.
 *   - FILE_EXISTS_RENAME: Append _{incrementing number} until the filename is
 *     unique.
 *   - FILE_EXISTS_ERROR: Do nothing and return FALSE.
 * @return
 *   TRUE for success, FALSE for failure.
 */
function advagg_file_copy(&$source, $dest = 0, $replace = FILE_EXISTS_RENAME) {
  $directory = dirname($dest);

  // Process a file upload object.
  if (is_object($source)) {
    $file = $source;
    $source = $file->filepath;
    if (!$basename) {
      $basename = $file->filename;
    }
  }

  $source = realpath($source);
  advagg_clearstatcache(TRUE, $source);
  if (!file_exists($source)) {
    drupal_set_message(t('The selected file %file could not be copied, because no file by that name exists. Please check that you supplied the correct filename.', array('%file' => $source)), 'error');
    return 0;
  }

  // If the destination file is not specified then use the filename of the source file.
  $basename = basename($dest);
  $basename = $basename ? $basename : basename($source);
  $dest = $directory . '/' . $basename;

  // Make sure source and destination filenames are not the same, makes no sense
  // to copy it if they are. In fact copying the file will most likely result in
  // a 0 byte file. Which is bad. Real bad.
  if ($source != realpath($dest)) {
    if (!$dest = file_destination($dest, $replace)) {
      drupal_set_message(t('The selected file %file could not be copied, because a file by that name already exists in the destination.', array('%file' => $source)), 'error');
      return FALSE;
    }

    if (!@copy($source, $dest)) {
      drupal_set_message(t('The selected file %file could not be copied. ' . $dest, array('%file' => $source)), 'error');
      return 0;
    }

    // Give everyone read access so that FTP'd users or
    // non-webserver users can see/read these files,
    // and give group write permissions so group members
    // can alter files uploaded by the webserver.
    @chmod($dest, 0664);
  }

  if (isset($file) && is_object($file)) {
    $file->filename = $basename;
    $file->filepath = $dest;
    $source = $file;
  }
  else {
    $source = $dest;
  }

  return 1; // Everything went ok.
}
