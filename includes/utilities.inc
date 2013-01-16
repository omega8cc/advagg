<?php

/**
 * @file
 * Utility and wrapper functions for the advagg module.
 */

/**
 * Filter array by matching keys against a regular expression.
 *
 * @param string $pattern
 *   The regular expression to check keys with.
 * @param array $array
 *   The array to iterate over, this is referenced.
 * @param int $depth
 *   (optional) The maximum depth to iterate through. Defaults to 0, the entire array.
 * @param int $level
 *   The internal variable to determine which iteration level the function is at. Do not use.
 */
function advagg_array_filter_preg_match($pattern, array &$array, $depth = 0, $level = 1) {
  foreach ($array as $key => $value) {
    // If pattern matches key, remove it.
    if (preg_match($pattern, $key) === 1) {
      unset($array[$key]);
    }
    // Continue iterating over array if necessary.
    elseif (is_array($value) && ($depth === 0 || $depth < $level)) {
      advagg_preg_array_filter_key($pattern, $array[$key], $depth, $level + 1);
    }
  }
}

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
 * Returns an array of values needed for aggregation
 *
 * @param $noagg
 *   (optional) Bool indicating that aggregation should be disabled if TRUE.
 * @param $type
 *   (optional) String. js or css.
 * @return
 *   array of values to be imported via list() function.
 */
function advagg_process_prep($noagg = FALSE, $type = NULL) {
  global $conf;
  $preprocess = (!defined('MAINTENANCE_MODE') || MAINTENANCE_MODE != 'update');

  // URL bypass.
  if ($noagg || (isset($_GET['advagg']) && $_GET['advagg'] == 0 && user_access('bypass advanced aggregation'))) {
    $preprocess = FALSE;
    $conf['advagg_use_full_cache'] = FALSE;
  }

  // Cookie bypass.
  if (advagg_cookie_bypass()) {
    $preprocess = FALSE;
    $conf['advagg_use_full_cache'] = FALSE;
  }

  $scheme = file_default_scheme();
  if ($scheme !== 'public') {
    $custom_path = variable_get('advagg_custom_files_dir', ADVAGG_CUSTOM_FILES_DIR);
    if (!empty($custom_path)) {
      $scheme = 'public';
    }
  }

  if ($preprocess) {
    if ($type == 'js' && !variable_get('advagg_preprocess_js', ADVAGG_PREPROCESS_JS)) {
      $preprocess = FALSE;
    }
    if ($type == 'css' && !variable_get('advagg_preprocess_css', ADVAGG_PREPROCESS_CSS)) {
      $preprocess = FALSE;
    }
  }

  // A dummy query-string is added to filenames, to gain control over
  // browser-caching. The string changes on every update or full cache
  // flush, forcing browsers to load a new copy of the files, as the
  // URL changed.
  $query_string = '?' . substr(variable_get('css_js_query_string', '0'), 0, 1);

  return array($preprocess, $scheme, $query_string);
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

if (!function_exists('array_merge_recursive_distinct')) {
  /**
  * Merges any number of arrays / parameters recursively, replacing
  * entries with string keys with values from latter arrays.
  * If the entry or the next value to be assigned is an array, then it
  * automagically treats both arguments as an array.
  * Numeric entries are appended, not replaced, but only if they are
  * unique
  *
  * calling: result = array_merge_recursive_distinct(a1, a2, ... aN)
 **/

 function array_merge_recursive_distinct () {
   $arrays = func_get_args();
   $base = array_shift($arrays);
   if(!is_array($base)) $base = empty($base) ? array() : array($base);
   foreach($arrays as $append) {
     if(!is_array($append)) $append = array($append);
     foreach($append as $key => $value) {
       if(!array_key_exists($key, $base) and !is_numeric($key)) {
         $base[$key] = $append[$key];
         continue;
       }
       if(is_array($value) or is_array($base[$key])) {
         $base[$key] = array_merge_recursive_distinct($base[$key], $append[$key]);
       } else if(is_numeric($key)) {
         if(!in_array($value, $base)) $base[] = $value;
       } else {
         $base[$key] = $value;
       }
     }
   }
   return $base;
 }
}
