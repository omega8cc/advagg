<?php

/**
 * @file
 * Drush commands for Advanced CSS/JS Aggregation.
 */

/**
 * Implement hook_drush_cache_clear.
 *
 * @param $types
 *   List of cache types that can be cleared.
 */
function advagg_drush_cache_clear(&$types) {
  // Add in Adcanced CSS/JS Aggregation
  $types['advagg'] = 'drush_advagg_cache_scan';
}

/**
 * Rescan bundles and rebuild if needed.
 */
function drush_advagg_cache_scan() {
  global $_advagg;
  _drupal_flush_css_js();
  advagg_flush_caches();

  drush_log(dt('Advanced CSS/JS Aggregation: Rebuilt !count bundles', array('!count' => count($_advagg['rebuilt']))), 'ok');
}
