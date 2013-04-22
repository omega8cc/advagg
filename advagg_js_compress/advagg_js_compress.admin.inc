<?php

/**
 * @file
 * Admin page callbacks for the advagg JS compression module.
 */

/**
 * Form builder; Configure advagg settings.
 *
 * @ingroup forms
 * @see system_settings_form()
 */
function advagg_js_compress_admin_settings_form($form, $form_state) {
  drupal_set_title(t('AdvAgg: JS Compression'));

  $form = array();
  $description = '';
  $options = array(
    0 => t('Disabled'),
    1 => t('JSMin+'),
//     2 => t('Packer'),
  );
  if (function_exists('jsmin')) {
    $options[3] = t('JSMin');
    $description .= t('JSMin is the C complied version and is about 25 times faster. Recommend using it.');
  }
  else {
    $description .= t('You can use the much faster C version of JSMin by installing the <a href="@php_jsmin">JSMin PHP Extension</a> on this server.', array('@php_jsmin' => 'http://www.ypass.net/software/php_jsmin/'));
  }
  $form['advagg_js_compressor'] = array(
    '#type' => 'radios',
    '#title' => t('File Compression: Select a Compressor'),
    '#default_value' => variable_get('advagg_js_compressor', ADVAGG_JS_COMPRESSOR),
    '#options' => $options,
    '#description' => filter_xss($description),
  );

  $form['advagg_js_inline_compressor'] = array(
    '#type' => 'radios',
    '#title' => t('Inline Compression: Select a Compressor'),
    '#default_value' => variable_get('advagg_js_inline_compressor', ADVAGG_JS_INLINE_COMPRESSOR),
    '#options' => $options,
    '#description' => filter_xss($description),
  );
  $form['advagg_js_inline_compress_if_not_cacheable'] = array(
    '#type' => 'checkbox',
    '#title' => t('Inline Compression: Use even if this page is not cacheable'),
    '#default_value' => variable_get('advagg_js_inline_compress_if_not_cacheable', ADVAGG_JS_INLINE_COMPRESS_IF_NOT_CACHEABLE),
    '#description' => t('By checking this box, all Inline JavaScript will be compressed regardless of the state of <a href="@link">drupal_page_is_cacheable()</a>. If the C complied version of JSMin is enabled, this option should not slow down page generation that much; if you are using JSMin+ I recommend keeping this disabled.', array('@link' => 'http://api.drupal.org/api/drupal/includes!bootstrap.inc/function/drupal_page_is_cacheable/7')),
  );

  $form['advagg_js_compress_packer'] = array(
    '#type' => 'checkbox',
    '#title' => t('Use Packer on non GZip JS Aggregates'),
    '#default_value' => variable_get('advagg_js_compress_packer', ADVAGG_JS_COMPRESS_PACKER),
    '#description' => t('If enabled the non gzip version of JS files will be compressed using the JS Packer. Packer works similar to gzip, thus using packer on a gzipped file does not give a big improvement in terms of bytes transfered over the wire. WARNING: This has a high chance of breaking your JS. Only Enable on production after testing the non gzipped version locally.'),
  );

  // Clear the cache bins on submit.
  $form['#submit'][] = 'advagg_js_compress_admin_settings_form_submit';

  return system_settings_form($form);
}

// Submit callback.
/**
 * Clear out the advagg cache bin when the save configuration button is pressed.
 */
function advagg_js_compress_admin_settings_form_submit($form, &$form_state) {
  $cache_bins = advagg_flush_caches();
  foreach ($cache_bins as $bin) {
    cache_clear_all('*', $bin, TRUE);
  }
}
