<?php

function advagg_process_html_js(&$variables) {
  $js = drupal_add_js();
  drupal_alter('js', $js);
  $variables['js'] = $js;
  // dpm($js);
  return;
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
