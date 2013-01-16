<?php

/**
 * @file
 * JS processing functions for the advagg module.
 */


function advagg_process_html_js(&$variables) {
  // @TODO Remove forced return to actually process the JS.
  return;

  $js = drupal_add_js();
  drupal_alter('js', $js);
  $variables['js'] = $js;
  $schema = advagg_get_server_schema();
  $js_code = array();
  $js_code['header'] = drupal_add_js(NULL, array('type' => NULL));
  if (variable_get('advagg_closure', ADVAGG_CLOSURE) && !empty($_advagg['closure'])) {
    $js_code['footer'] = drupal_add_js(NULL, array('type' => NULL, 'scope' => 'footer'));
  }
  $skip_keys = variable_get('advagg_region_skip_keys', array('styles', 'scripts', 'zebra', 'id', 'directory', 'layout', 'head_title', 'base_path', 'front_page', 'head', 'body_classes', 'header', 'footer', 'closure'));
  foreach ($variables as $key => $value) {
    if (!in_array($key, $skip_keys) && is_string($value) && !empty($value) && !isset($js_code[$key])) {
      $js_code[$key] = drupal_add_js(NULL, array('type' => NULL, 'scope' => $key));
    }
  }
  $js_code_orig = $js_code;

  // Try cache.
  if (variable_get('advagg_use_full_cache', ADVAGG_USE_FULL_CACHE)) {
    // Build the cache ID
    // md5 of the JS input
    // http or https
    // hostname
    // the js rendering function
    // css/js query string
    $cid = 'advagg_processor:js:'
      . md5(serialize($js_code)) . ':'
      . $schema . ':'
      . $_SERVER['HTTP_HOST'] . ':'
      . variable_get('advagg_js_render_function', ADVAGG_JS_RENDER_FUNCTION) . ':'
      . substr(variable_get('css_js_query_string', '0'), 0, 1);
    $cache = cache_get($cid, 'cache_advagg_bundle_reuse');
  }
  elseif (isset($cid)) {
    unset($cid);
  }
  if (!empty($cache->data)) {
    $js_code = $cache->data;
  }
  else {
    // Build HTML code.
    advagg_jquery_updater($js_code['header']);
    $js_code = advagg_process_js($js_code);

    // Cache array.
    if (isset($cid) && variable_get('advagg_use_full_cache', ADVAGG_USE_FULL_CACHE) && lock_acquire($cid)) {
      cache_set($cid, $js_code, 'cache_advagg_bundle_reuse', CACHE_TEMPORARY);
      lock_release($cid);
    }
  }

  // Place JS in the correct places.
  foreach ($js_code as $key => $value) {
    if ($key == 'header') {
      $variables['scripts'] = $value;
    }
    elseif ($key == 'footer' && variable_get('advagg_closure', ADVAGG_CLOSURE) && !empty($_advagg['closure'])) {
      $variables['closure'] .= $value;
    }
    else {
      $variables[$key] .= $value;
    }
  }
}

/**
 * Get full JS array.
 *
 * Note that hook_js_alter(&$javascript) is called during this function call
 * to allow alterations of the JavaScript during its presentation. Calls to
 * drupal_add_js() from hook_js_alter() will not be added to the output
 * presentation. The correct way to add JavaScript during hook_js_alter()
 * is to add another element to the $javascript array, deriving from
 * drupal_js_defaults(). See locale_js_alter() for an example of this.
 *
 * @param $javascript
 *   (optional) An array with all JavaScript code. Defaults to the default
 *   JavaScript array for the given scope.
 * @param $skip_alter
 *   (optional) If set to TRUE, this function skips calling drupal_alter() on
 *   $javascript, useful when the calling function passes a $javascript array
 *   that has already been altered.
 *
 * @return
 *   The raw JavaScript array.
 *
 * @see drupal_add_js()
 * @see locale_js_alter()
 * @see drupal_js_defaults()
 */
function advagg_get_full_js($javascript = NULL, $skip_alter = FALSE) {
  if (!isset($javascript)) {
    $javascript = drupal_add_js();
  }
  if (empty($javascript)) {
    return FALSE;
  }

  // Allow modules to alter the JavaScript.
  if (!$skip_alter) {
    drupal_alter('js', $javascript);
  }
  return $javascript;
}

/**
 * Get all javascript scopes set in the $javascript array.
 *
 * @param $javascript
 *   An array with all JavaScript code.
 *
 * @return
 *   Array of scopes that are currently being used.
 */
function advagg_get_js_scopes($javascript) {
  // Return if nothing given to us.
  if (empty($javascript) || !is_array($javascript)) {
    return FALSE;
  }

  // Filter out elements of the given scope.
  $scopes = array();
  foreach ($javascript as $key => $item) {
    // skip if the scope is not set.
    if (!is_array($item) || empty($item['scope'])) {
      continue;
    }
    if (!isset($scopes[$item['scope']])) {
      $scopes[$item['scope']] = TRUE;
    }
  }
  return $scopes;
}

/**
 * Returns a themed presentation of all JavaScript code for the current page.
 *
 * References to JavaScript files are placed in a certain order: first, all
 * 'core' files, then all 'module' and finally all 'theme' JavaScript files
 * are added to the page. Then, all settings are output, followed by 'inline'
 * JavaScript code. If running update.php, all preprocessing is disabled.
 *
 * @param $scope
 *   (optional) The scope for which the JavaScript rules should be returned.
 *   Defaults to 'header'.
 * @param $javascript
 *   (optional) An array with all JavaScript code. Defaults to the default
 *   JavaScript array for the given scope.
 *
 * @return
 *   All JavaScript code segments and includes for the scope as HTML tags.
 */
function advagg_get_js($scope, $javascript) {
  // Filter out elements of the given scope.
  $items = array();
  foreach ($javascript as $key => $item) {
    if ($item['scope'] == $scope) {
      $items[$key] = $item;
    }
  }

  $output = '';
  // The index counter is used to keep aggregated and non-aggregated files in
  // order by weight.
  $index = 1;
  $processed = array();
  $files = array();
  $preprocess_js = (variable_get('preprocess_js', FALSE) && (!defined('MAINTENANCE_MODE') || MAINTENANCE_MODE != 'update'));

  // A dummy query-string is added to filenames, to gain control over
  // browser-caching. The string changes on every update or full cache
  // flush, forcing browsers to load a new copy of the files, as the
  // URL changed. Files that should not be cached (see drupal_add_js())
  // get REQUEST_TIME as query-string instead, to enforce reload on every
  // page request.
  $default_query_string = variable_get('css_js_query_string', '0');

  // For inline JavaScript to validate as XHTML, all JavaScript containing
  // XHTML needs to be wrapped in CDATA. To make that backwards compatible
  // with HTML 4, we need to comment out the CDATA-tag.
  $embed_prefix = "\n<!--//--><![CDATA[//><!--\n";
  $embed_suffix = "\n//--><!]]>\n";

  // Since JavaScript may look for arguments in the URL and act on them, some
  // third-party code might require the use of a different query string.
  $js_version_string = variable_get('drupal_js_version_query_string', 'v=');

  // Sort the JavaScript so that it appears in the correct order.
  uasort($items, 'drupal_sort_css_js');

  // Provide the page with information about the individual JavaScript files
  // used, information not otherwise available when aggregation is enabled.
  $setting['ajaxPageState']['js'] = array_fill_keys(array_keys($items), 1);
  unset($setting['ajaxPageState']['js']['settings']);
  drupal_add_js($setting, 'setting');

  // If we're outputting the header scope, then this might be the final time
  // that drupal_get_js() is running, so add the setting to this output as well
  // as to the drupal_add_js() cache. If $items['settings'] doesn't exist, it's
  // because drupal_get_js() was intentionally passed a $javascript argument
  // stripped off settings, potentially in order to override how settings get
  // output, so in this case, do not add the setting to this output.
  if ($scope == 'header' && isset($items['settings'])) {
    $items['settings']['data'][] = $setting;
  }

  // Loop through the JavaScript to construct the rendered output.
  $element = array(
    '#tag' => 'script',
    '#value' => '',
    '#attributes' => array(
      'type' => 'text/javascript',
    ),
  );
  foreach ($items as $item) {
    $query_string =  empty($item['version']) ? $default_query_string : $js_version_string . $item['version'];

    switch ($item['type']) {
      case 'setting':
        $js_element = $element;
        $js_element['#value_prefix'] = $embed_prefix;
        $js_element['#value'] = 'jQuery.extend(Drupal.settings, ' . drupal_json_encode(drupal_array_merge_deep_array($item['data'])) . ");";
        $js_element['#value_suffix'] = $embed_suffix;
        $output .= theme('html_tag', array('element' => $js_element));
        break;

      case 'inline':
        $js_element = $element;
        if ($item['defer']) {
          $js_element['#attributes']['defer'] = 'defer';
        }
        $js_element['#value_prefix'] = $embed_prefix;
        $js_element['#value'] = $item['data'];
        $js_element['#value_suffix'] = $embed_suffix;
        $processed[$index++] = theme('html_tag', array('element' => $js_element));
        break;

      case 'file':
        $js_element = $element;
        if (!$item['preprocess'] || !$preprocess_js) {
          if ($item['defer']) {
            $js_element['#attributes']['defer'] = 'defer';
          }
          $query_string_separator = (strpos($item['data'], '?') !== FALSE) ? '&' : '?';
          $js_element['#attributes']['src'] = file_create_url($item['data']) . $query_string_separator . ($item['cache'] ? $query_string : REQUEST_TIME);
          $processed[$index++] = theme('html_tag', array('element' => $js_element));
        }
        else {
          // By increasing the index for each aggregated file, we maintain
          // the relative ordering of JS by weight. We also set the key such
          // that groups are split by items sharing the same 'group' value and
          // 'every_page' flag. While this potentially results in more aggregate
          // files, it helps make each one more reusable across a site visit,
          // leading to better front-end performance of a website as a whole.
          // See drupal_add_js() for details.
          $key = 'aggregate_' . $item['group'] . '_' . $item['every_page'] . '_' . $index;
          $processed[$key] = '';
          $files[$key][$item['data']] = $item;
        }
        break;

      case 'external':
        $js_element = $element;
        // Preprocessing for external JavaScript files is ignored.
        if ($item['defer']) {
          $js_element['#attributes']['defer'] = 'defer';
        }
        $js_element['#attributes']['src'] = $item['data'];
        $processed[$index++] = theme('html_tag', array('element' => $js_element));
        break;
    }
  }

  // Aggregate any remaining JS files that haven't already been output.
  if ($preprocess_js && count($files) > 0) {
    foreach ($files as $key => $file_set) {
      $uri = drupal_build_js_cache($file_set);
      // Only include the file if was written successfully. Errors are logged
      // using watchdog.
      if ($uri) {
        $preprocess_file = file_create_url($uri);
        $js_element = $element;
        $js_element['#attributes']['src'] = $preprocess_file;
        $processed[$key] = theme('html_tag', array('element' => $js_element));
      }
    }
  }

  // Keep the order of JS files consistent as some are preprocessed and others are not.
  // Make sure any inline or JS setting variables appear last after libraries have loaded.
  return implode('', $processed) . $output;
}

/**
 * Special handling for jquery update.
 *
 * @param $js
 *   List of files in the header
 */
function advagg_jquery_updater(&$js) {
  if (!module_exists('jquery_update') || !variable_get('jquery_update_replace', TRUE) || empty($js)) {
    return;
  }

  // Replace jquery.js first.
  $new_jquery = array(jquery_update_jquery_path() => $js['core']['misc/jquery.js']);
  $js['core'] = array_merge($new_jquery, $js['core']);
  unset($js['core']['misc/jquery.js']);

  // Loop through each of the required replacements.
  $replacement_path = drupal_get_path('module', 'jquery_update') . '/replace/';
  foreach (jquery_update_get_replacements() as $type => $replacements) {
    foreach ($replacements as $find => $replace) {
      // If the file to replace is loaded on this page...
      if (isset($js[$type][$find])) {
        // Create a new entry for the replacement file, and unset the original one.
        $replace = $replacement_path . $replace;
        $js[$type][$replace] = $js[$type][$find];
        unset($js[$type][$find]);
      }
    }
  }
}

/**
 * Build and theme JS output for header.
 *
 * @param $external_no_preprocess
 *   array(array($src, $defer))
 * @param $output_preprocess
 *   array(array($src, $prefix, $suffix))
 * @param $output_no_preprocess
 *   array(array(array($src, $defer)))
 * @param $setting_no_preprocess
 *   array(array($code))
 * @param $inline_no_preprocess
 *   array(array($code, $defer))
 * @param $scope
 *   header or footer.
 * @param $js_settings_array
 *   array of settings used.
 * @param $inline_included
 *   array of inline scripts used.
 * @param $files_included
 *   array of files used.
 * @param $files_aggregates_included
 *   array of files and aggregates used.
 * @return
 *   String of themed JavaScript.
 */
function advagg_js_array($external_no_preprocess, $output_preprocess, $output_no_preprocess, $setting_no_preprocess, $inline_no_preprocess, $scope, $js_settings_array, $inline_included, $files_included, $files_aggregates_included) {
  return array(
    'settings' => $js_settings_array,
    'inline' => $inline_included,
    'files' => $files_included,
    'files_aggregates' => $files_aggregates_included,
  );
}

/**
 * Returns a themed presentation of all JavaScript code for the current page.
 *
 * @see drupal_get_js()
 *
 * References to JavaScript files are placed in a certain order: first, all
 * 'core' files, then all 'module' and finally all 'theme' JavaScript files
 * are added to the page. Then, all settings are output, followed by 'inline'
 * JavaScript code. If running update.php, all preprocessing is disabled.
 *
 * @param $js_code
 *   An array with all JavaScript code. Key it the region
 * @param $noagg
 *   (optional) Bool indicating that aggregation should be disabled if TRUE.
 * @return
 *   All JavaScript code segments and includes for the scope as HTML tags.
 */
function advagg_process_js($master_set, $noagg = FALSE) {
  global $conf;
  if ((!defined('MAINTENANCE_MODE') || MAINTENANCE_MODE != 'update') && function_exists('locale_update_js_files')) {
    locale_update_js_files();
  }

  // Get useful info.
  list($preprocess_js, $scheme, $query_string) = advagg_process_prep($noagg, 'js');
  $advagg_json_encode_function = variable_get('advagg_json_encode_function', 'drupal_json_encode');

  $output = array();
  foreach ($master_set as $scope => $javascript) {
    if ($scope != 'header' && $scope != 'footer' && empty($javascript)) {
      continue;
    }

    // Invoke hook_advagg_js_pre_alter() to give installed modules a chance to
    // modify the data in the $javascript array if necessary.
    drupal_alter('advagg_js_pre', $javascript, $preprocess_js, $scheme, $scope);
    $master_set[$scope] = $javascript;
  }

  // Invoke hook_advagg_js_header_footer_alter() to give installed modules a chance to
  // modify the data in the header and footer JS if necessary.
  drupal_alter('advagg_js_header_footer', $master_set, $preprocess_js, $scheme);

  foreach ($master_set as $scope => $javascript) {
    if (empty($javascript)) {
      continue;
    }

    // Set variables.
    $setting_no_preprocess = array();
    $inline_no_preprocess = array();
    $external_no_preprocess = array();
    $output_no_preprocess = array(
      'core' => array(),
      'module' => array(),
      'theme' => array(),
    );
    $output_preprocess = array();
    $preprocess_list = array();
    $js_settings_array = array();
    $inline_included = array();
    $files_included = array();
    $files_aggregates_included = array();

    // Process input.
    foreach ($javascript as $type => $data) {
      if (empty($data)) {
        continue;
      }

      // Add the prefix and suffix to the info array if not there.
      if ($type != 'setting') {
        foreach ($data as &$info) {
          $info['prefix'] = isset($info['prefix']) ? $info['prefix'] : '';
          $info['suffix'] = isset($info['suffix']) ? $info['suffix'] : '';
        }
        // $info needs to be unset, otherwise foreach loops below will break.
        unset($info);
      }

      switch ($type) {
        case 'setting':
          $data = call_user_func_array('array_merge_recursive', $data);
          $js_settings_array[] = $data;
          $js_settings = $advagg_json_encode_function($data);
          $js_settings = preg_replace(array('/"DRUPAL_JS_RAW\:/', '/\:DRUPAL_JS_RAW"/'), array('', ''), $js_settings);
          $setting_no_preprocess[] = 'jQuery.extend(Drupal.settings, ' . $js_settings . ");";
          break;

        case 'inline':
          foreach ($data as $info) {
            // Invoke hook_advagg_js_inline_alter() to give installed modules a
            // chance to modify the contents of $info['code'] if necessary.
            drupal_alter('advagg_js_inline', $info['code']);

            $inline_no_preprocess[] = array($info['code'], $info['defer'], $info['prefix'], $info['suffix']);
            $inline_included[] = $info['code'];
          }
          break;

        case 'external':
          foreach ($data as $path => $info) {
            $external_no_preprocess[] = array($path, $info['defer'], $info['prefix'], $info['suffix']);
            $files_included[$path] = TRUE;
          }
          break;

        default:
          // If JS preprocessing is off, we still need to output the scripts.
          // Additionally, go through any remaining scripts if JS preprocessing is on and output the non-cached ones.
          foreach ($data as $path => $info) {
            if (!$info['preprocess'] || $scheme !== 'public' || !$preprocess_js) {
              $output_no_preprocess[$type][] = array(advagg_build_uri($path) . ($info['cache'] ? $query_string : '?' . REQUEST_TIME), $info['defer'], $info['prefix'], $info['suffix']);
              $files_included[$path] = $info['preprocess'];
            }
            else {
              $preprocess_list[$path] = $info;
            }
          }
          break;

      }
    }

    // Aggregate any remaining JS files that haven't already been output.
    if ($scheme === 'public' && $preprocess_js && count($preprocess_list) > 0) {
      $files_aggregates_included = $files_included;
      $files = array();
      foreach ($preprocess_list as $path => $info) {
        if ($info['preprocess']) {
          $files[] = $path;
          $files_included[$path] = TRUE;
        }
      }
      if (!empty($files)) {
        $preprocess_files = advagg_css_js_file_builder('js', $files, $query_string);
        if (!empty($preprocess_files)) {
          $good = TRUE;
          foreach ($preprocess_files as $preprocess_file => $extra) {
            // Empty aggregate, skip
            if (empty($preprocess_file)) {
              continue;
            }

            if ($extra !== FALSE && is_array($extra)) {
              $prefix = $extra['prefix'];
              $suffix = $extra['suffix'];
              $output_preprocess[] = array(advagg_build_uri($preprocess_file), $prefix, $suffix);
              $files_aggregates_included[$preprocess_file] = $extra;
            }
            else {
              $good = FALSE;
              break;
            }
          }
        }
        if (empty($good)) {
          // Redo with aggregation turned off and return the new value.
          watchdog('advagg', 'JS aggregation failed. %filename could not be saved correctly.', array('%filename' => $preprocess_file), WATCHDOG_ERROR);
          $data = advagg_process_js($master_set, TRUE);
          return $data;
        }
      }
    }

    // Default function called: advagg_js_builder
    $function = variable_get('advagg_js_render_function', ADVAGG_JS_RENDER_FUNCTION);
    $output[$scope] = $function($external_no_preprocess, $output_preprocess, $output_no_preprocess, $setting_no_preprocess, $inline_no_preprocess, $scope, $js_settings_array, $inline_included, $files_included, $files_aggregates_included);
  }
  return $output;
}

/**
 * Build and theme JS output for header.
 *
 * @param $external_no_preprocess
 *   array(array($src, $defer, $prefix, $suffix))
 * @param $output_preprocess
 *   array(array($src, $prefix, $suffix))
 * @param $output_no_preprocess
 *   array(array(array($src, $defer, $prefix, $suffix)))
 * @param $setting_no_preprocess
 *   array(array($code))
 * @param $inline_no_preprocess
 *   array(array($code, $defer, $prefix, $suffix))
 * @param $scope
 *   header or footer.
 * @param $js_settings_array
 *   array of settings used.
 * @param $inline_included
 *   array of inline scripts used.
 * @param $files_included
 *   array of files used.
 * @param $files_aggregates_included
 *   array of files and aggregates used.
 * @return
 *   String of themed JavaScript.
 */
function advagg_js_builder($external_no_preprocess, $output_preprocess, $output_no_preprocess, $setting_no_preprocess, $inline_no_preprocess, $js_settings_array, $inline_included, $files_included, $files_aggregates_included) {
  $output = '';

  // For inline Javascript to validate as XHTML, all Javascript containing
  // XHTML needs to be wrapped in CDATA. To make that backwards compatible
  // with HTML 4, we need to comment out the CDATA-tag.
  $embed_prefix = "\n<!--//--><![CDATA[//><!--\n";
  $embed_suffix = "\n//--><!]]>\n";

  // Keep the order of JS files consistent as some are preprocessed and others are not.
  // Make sure any inline or JS setting variables appear last after libraries have loaded.

  if (!empty($external_no_preprocess)) {
    foreach ($external_no_preprocess as $values) {
      list($src, $defer, $prefix, $suffix) = $values;
      $output .= $prefix . '<script type="text/javascript"' . ($defer ? ' defer="defer"' : '') . ' src="' . $src . '"></script>' . $suffix . "\n";
    }
  }

  if (!empty($output_preprocess)) {
    foreach ($output_preprocess as $values) {
      list($src, $prefix, $suffix) = $values;
      $output .= $prefix . '<script type="text/javascript" src="' . $src . '"></script>' . $suffix . "\n";
    }
  }

  foreach ($output_no_preprocess as $type => $list) {
    if (!empty($list)) {
      foreach ($list as $values) {
        list($src, $defer, $prefix, $suffix) = $values;
        $output .= $prefix . '<script type="text/javascript"' . ($defer ? ' defer="defer"' : '') . ' src="' . $src . '"></script>' . $suffix . "\n";
      }
    }
  }

  if (!empty($setting_no_preprocess)) {
    foreach ($setting_no_preprocess as $code) {
      $output .= '<script type="text/javascript">' . $embed_prefix . $code . $embed_suffix . "</script>\n";
    }
  }

  if (!empty($inline_no_preprocess)) {
    foreach ($inline_no_preprocess as $values) {
      list($code, $defer, $prefix, $suffix) = $values;
      $output .= $prefix . '<script type="text/javascript"' . ($defer ? ' defer="defer"' : '') . '>' . $embed_prefix . $code . $embed_suffix . '</script>' . $suffix . "\n";
    }
  }

  return $output;
}

/**
 * Given a list of files, grab their contents and glue it into one big string.
 *
 * @param $files
 *   array of filenames.
 * @return
 *   string containing all the files.
 */
function advagg_build_js_bundle($files) {
  if (empty($files)) {
    return '';
  }
  $data = '';
  // Build aggregate JS file.
  foreach ($files as $file) {
    // Append a ';' and a newline after each JS file to prevent them from running together.
    if (advagg_file_exists($file)) {
      $data .= file_get_contents($file) . ";\n";
    }
  }
  return $data;
}
