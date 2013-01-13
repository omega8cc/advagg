<?php

/**
 * @file
 * CTools support for the advagg module.
 */

/**
 * Implements hook_advagg_js_header_footer_alter().
 */
function advagg_advagg_js_header_footer_alter(&$master_set, $preprocess_js, $scheme) {
  // Don't run the code below if ctools ajax is not loaded.
  if (!defined('CTOOLS_AJAX_INCLUDED')) {
    return;
  }

  // Get all JS files set to be loaded.
  $js_files = array();
  foreach ($master_set as $scope => $scripts) {
    if (empty($scripts)) {
      continue;
    }
    advagg_ctools_process_js_files($js_files, $scope, $scripts);
  }

  // Add list of CSS & JS files loaded to the settings in the footer.
  $loaded = array('CToolsAJAX' => array('scripts' => $js_files));
  // Save to the js settings array even though we do not reload it in advagg.
  drupal_add_js($loaded, array('type' => 'setting', 'scope' => 'footer'));

  // Add it to the settings array in the footer.
  if (!isset($master_set['footer']['setting']) || !is_array($master_set['footer']['setting'])) {
    $master_set['footer']['setting'] = array();
  }
  $master_set['footer']['setting'][] = $loaded;
}

/**
 * Implements hook_advagg_css_pre_alter().
 */
function advagg_advagg_css_pre_alter(&$css, $preprocess_css, $scheme) {
  // Don't run the code below if ctools ajax is not loaded.
  if (!defined('CTOOLS_AJAX_INCLUDED')) {
    return;
  }

  // Get all CSS files set to be loaded.
  $css_files = array();
  ctools_process_css_files($css_files, $css);

  // Save to the js settings array.
  drupal_add_js(array('CToolsAJAX' => array('css' => $css_files)), array('type' => 'setting', 'scope' => 'footer'));
}

/**
 * Create a list of javascript files that are on the page.
 *
 * @param $js_files
 *   Array of js files that are loaded on this page.
 * @param $scope
 *   String usually containing header or footer.
 * @param $scripts
 *   (Optional) array returned from drupal_add_js(). If NULL then it will load
 *   the array from drupal_add_js for the given scope.
 * @return array $settings
 *   The JS 'setting' array for the given scope.
 */
function advagg_ctools_process_js_files(&$js_files, $scope, $scripts = NULL) {
  // Automatically extract any 'settings' added via drupal_add_js() and make
  // them the first command.
  $scripts = drupal_add_js(NULL, array('type' => NULL, 'scope' => $scope));
  if (empty($scripts)) {
    $scripts = drupal_add_js(NULL, array('type' => NULL, 'scope' => $scope));
  }

  // Get replacements that are going to be made by contrib modules and take
  // them into account so we don't double-load scripts.
  static $replacements = NULL;
  if (!isset($replacements)) {
    $replacements = module_invoke_all('js_replacements');
  }

  $settings = array();
  foreach ($scripts as $type => $data) {
    switch ($type) {
      case 'setting':
        $settings = $data;
        break;
      case 'inline':
      case 'theme':
        // Presently we ignore inline javascript.
        // Theme JS is already added and because of admin themes, this could add
        // improper JS to the page.
        break;
      default:
        // If JS preprocessing is off, we still need to output the scripts.
        // Additionally, go through any remaining scripts if JS preprocessing is on and output the non-cached ones.
        foreach ($data as $path => $info) {
          // If the script is being replaced, take that replacement into account.
          $final_path = isset($replacements[$type][$path]) ? $replacements[$type][$path] : $path;
          $js_files[base_path() . $final_path] = TRUE;
        }
    }
  }
  return $settings;
}
