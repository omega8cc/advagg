<?php

/**
 * @file
 * Advanced aggregation css compression module.
 */

/**
 * Implement hook_advagg_get_css_aggregate_contents_alter().
 */
function advagg_css_compress_advagg_get_css_aggregate_contents_alter(&$data, $files, $optimize, $aggregate_settings) {
  if (   empty($aggregate_settings['variables']['advagg_css_compress_agg_files'])
      || !isset($aggregate_settings['variables']['advagg_css_compressor'])
        ) {
    return;
  }

  if ($aggregate_settings['variables']['advagg_css_compressor'] == 2) {
    advagg_css_compress_yui_cssmin($data);
  }
}

/**
 * Use the CSSmin library from YUI to compress the CSS.
 */
function advagg_css_compress_yui_cssmin(&$data) {
  // Include CSSmin from YUI.
  $filename = drupal_get_path('module', 'advagg_css_compress') . '/yui/CSSMin.inc';
  include_once($filename);
  $cssmin = new CSSmin(FALSE);
  // Compress the CSS splitting lines after 4k of text
  $data = $cssmin->run($data, 4096);
}


