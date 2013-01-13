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