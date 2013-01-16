<?php

/**
 * @file
 * Utility and wrapper functions for the advagg module.
 */

/**
 * Given path output uri to that file
 */
function advagg_build_uri($path) {
  static $hook_file_url_alter = array();

  // If the current path is an absolute path, return immediately.
  $fragments = parse_url($path);
  if (isset($fragments['host'])) {
    return $path;
  }

  $original_path = $path;
  // CDN Support.
  if (module_exists('cdn')) {
    $status = variable_get(CDN_STATUS_VARIABLE, CDN_DISABLED);
    if ($status == CDN_ENABLED || ($status == CDN_TESTING && user_access(CDN_PERM_ACCESS_TESTING))) {
      // Alter URL when the file_create_url() patch is not there.
      if (variable_get(CDN_THEME_LAYER_FALLBACK_VARIABLE, FALSE)) {
        cdn_file_url_alter($path);
      }
      // Use the patched version of file_create_url().
      else {
        $path = file_create_url($path);
      }
      // Return here if the path was changed above.
      if (strcmp($original_path, $path) != 0) {
        return $path;
      }
    }
  }

  // Other modules besides CDN might want to use hook_file_url_alter.
  if (empty($hook_file_url_alter)) {
    $hook_file_url_alter = module_implements('file_url_alter');
  }
  if (!empty($hook_file_url_alter)) {
    $path = file_create_url($path);
    // Return here if the path was changed above.
    if (strcmp($original_path, $path) != 0) {
      return $path;
    }
  }

  // If nothing was altered then use the drupal default.
  return base_path() . $path;
}

/**
 * Helper function to build an URL for asynchronous requests.
 *
 * @param $filepath
 *   Path to a URI excluding everything to the left and including the base path.
 */
function _advagg_build_url($filepath = '') {
  global $base_path;

  // Server auth.
  $auth = '';
  if (isset($_SERVER['AUTH_TYPE']) && $_SERVER['AUTH_TYPE'] == 'Basic') {
    $auth = $_SERVER['PHP_AUTH_USER'] . ':' . $_SERVER['PHP_AUTH_PW'] . '@';
  }

  // Host.
  $ip = variable_get('advagg_server_addr', FALSE);
  if ($ip == -1) {
    $ip = $_SERVER['HTTP_HOST'];
  }
  elseif (empty($ip)) {
    $ip = empty($_SERVER['SERVER_ADDR']) ? '127.0.0.1' : $_SERVER['SERVER_ADDR'];
  }

  // Port.
  $port = '';
//   if (   isset($_SERVER['SERVER_PORT'])
//       && is_numeric($_SERVER['SERVER_PORT'])
//       && ($_SERVER['SERVER_PORT'] != 80 || $_SERVER['SERVER_PORT'] != 443)
//         ) {
//     $port = ':' . $_SERVER['SERVER_PORT'];
//   }
  return 'http://' . $auth . $ip . $port . $base_path . $filepath;
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
function advagg_clearstatcache($clear_realpath_cache = FALSE, $filename = NULL) {
  static $php530;
  if (!isset($php530)) {
    $php530 = version_compare(PHP_VERSION, '5.3.0', '>=');
  }

  if ($php530) {
    return clearstatcache($clear_realpath_cache, $filename);
  }
  else {
    return clearstatcache();
  }
}

/**
 * Helper function to determine if the cookie to bypass aggregation is active.
 *
 * @return bool
 *   TRUE if enabled, FALSE otherwise.
 */
function advagg_cookie_bypass() {
  return !empty($_COOKIE[ADVAGG_COOKIE_NAME]) && $_COOKIE[ADVAGG_COOKIE_NAME] === md5(drupal_get_private_key()) ? TRUE : FALSE;
}

/**
 * Select records in the database matching where IN(...).
 *
 * NOTE Be aware of the servers max_packet_size variable.
 *
 * @param $table
 *   The name of the table.
 * @param $field
 *  field name to be compared to
 * @param $placeholder
 *   db_query placeholders; like %d or '%s'
 * @param $data
 *   array of values you wish to compare to
 * @param $returns
 *   array of db fields you return
 * @return
 *   returns db_query() result.
 */
function advagg_db_multi_select_in($table, $field, $placeholder, $data, $returns = array(), $groupby = '') {
  // Set returns if empty
  if (empty($returns)) {
    $returns[] = '*';
  }
  // Get the number of rows that will be inserted
  $rows = count($data);
  // Create what goes in the IN ()
  $in = $placeholder;
  // Add the rest of the place holders
  for ($i = 1; $i < $rows; $i++) {
    $in .= ', ' . $placeholder;
  }
  // Build the query
  $query = "SELECT " . implode(', ', $returns) . " FROM {" . $table . "} WHERE $field IN ($in) $groupby";
  // Run the query
  // TODO Please convert this statement to the D7 database API syntax.
  return db_query($query, $data);
}

/**
 * Get the CSS & JS path for advagg.
 *
 * @return
 *   Example below:
 *   array(
 *     array(
 *       public://advagg_css,
 *       sites/default/files/advagg_css,
 *     ),
 *     array(
 *       public://advagg_js,
 *       sites/default/files/advagg_js,
 *     ),
 *   )
 */
function advagg_get_root_files_dir() {
  static $css_path;
  static $js_path;
  // Make sure directories are available and writable.
  if (empty($css_path) || empty($js_path)) {
    $css_path = 'public://advagg_css';
    $js_path = 'public://advagg_js';
    file_prepare_directory($css_path, FILE_CREATE_DIRECTORY);
    file_prepare_directory($js_path, FILE_CREATE_DIRECTORY);
    $css_path = parse_url(file_create_url($css_path));
    $js_path = parse_url(file_create_url($js_path));
  }
  return array(ltrim($css_path['path'], '/'), ltrim($js_path['path'], '/'));
}

/**
 * Return the server schema (http or https).
 *
 * @return string
 *   http OR https.
 * @TODO: We probably should use relative references instead of trying to
 *        determine it based on the $_SERVER array.
 * @see: http://tools.ietf.org/html/rfc3986#section-4.2
 * @see: http://paulirish.com/2010/the-protocol-relative-url
 * @see: http://www.stevesouders.com/blog/2010/02/10/5a-missing-schema-double-download/
 */
function advagg_get_server_schema() {
  return (   (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] == 'on')
          || (isset($_SERVER['HTTP_X_FORWARDED_PROTO']) && $_SERVER['HTTP_X_FORWARDED_PROTO'] == 'https')
          || (isset($_SERVER['HTTP_HTTPS']) && $_SERVER['HTTP_HTTPS'] == 'on')
            ) ? 'https' : 'http';
}

/**
 * Always return TRUE, used for array_map in advagg_css_js_file_builder().
 */
function advagg_return_true() {
  return TRUE;
}

/**
 * See if a string ends with a substring.
 *
 * @param $haystack
 *   The main string being compared.
 * @param $needle
 *   The secondary string being compared.
 * @return
 *   bool
 */
function advagg_string_ends_with($haystack, $needle) {
  // Define substr_compare if it doesn't exist (PHP 4 fix).
  if (!function_exists('substr_compare')) {
    /**
     * Binary safe comparison of two strings from an offset, up to length
     * characters.
     *
     * Compares main_str from position offset with str up to length characters.
     * @see http://php.net/substr-compare#53084
     *
     * @param $main_str
     *   The main string being compared.
     * @param $str
     *   The secondary string being compared.
     * @param $offset
     *   The start position for the comparison. If negative, it starts counting
     *   from the end of the string.
     * @param $length
     *   The length of the comparison. The default value is the largest of the
     *   length of the str compared to the length of main_str less the offset.
     * @param $case_insensitivity
     *   If TRUE, comparison is case insensitive.
     * @return
     *   Returns < 0 if main_str from position offset is less than str, > 0 if
     *   it is greater than str, and 0 if they are equal. If offset is equal to
     *   or greater than the length of main_str or length is set and is less than
     *   1, substr_compare() prints a warning and returns FALSE.
     */
    function substr_compare($main_str, $str, $offset, $length = NULL, $case_insensitivity = FALSE) {
      $offset = (int) $offset;

      // Throw a warning because the offset is invalid
      if ($offset >= strlen($main_str)) {
        trigger_error('The start position cannot exceed initial string length.', E_USER_WARNING);
        return FALSE;
      }

      // We are comparing the first n-characters of each string, so let's use the PHP function to do it
      if ($offset == 0 && is_int($length) && $case_insensitivity === TRUE) {
        return strncasecmp($main_str, $str, $length);
      }

      // Get the substring that we are comparing
      if (is_int($length)) {
        $main_substr = substr($main_str, $offset, $length);
        $str_substr = substr($str, 0, $length);
      }
      else {
        $main_substr = substr($main_str, $offset);
        $str_substr = $str;
      }

      // Return a case-insensitive comparison of the two strings
      if ($case_insensitivity === TRUE) {
        return strcasecmp($main_substr, $str_substr);
      }

      // Return a case-sensitive comparison of the two strings
      return strcmp($main_substr, $str_substr);
    }
  }

  $haystack_len = strlen($haystack);
  $needle_len = strlen($needle);
  if ($needle_len > $haystack_len) {
    return FALSE;
  }
  return substr_compare($haystack, $needle, $haystack_len -$needle_len, $needle_len, TRUE) === 0;
}
