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
function advagg_css_compress_admin_settings_form($form, $form_state) {
  drupal_set_title(t('AdvAgg: CSS Compression Settings'));

  $form = array();
  $description = '';
  $options = array(
    0 => t('Disabled'),
    2 => t('YUI'),
  );
  $form['advagg_css_compressor'] = array(
    '#type' => 'radios',
    '#title' => t('File Compression: Select a Compressor'),
    '#default_value' => variable_get('advagg_css_compressor', ADVAGG_CSS_COMPRESSOR),
    '#options' => $options,
    '#description' => filter_xss($description),
  );
  $form['advagg_css_inline_compressor'] = array(
    '#type' => 'radios',
    '#title' => t('Inline Compression: Select a Compressor'),
    '#default_value' => variable_get('advagg_css_inline_compressor', ADVAGG_CSS_INLINE_COMPRESSOR),
    '#options' => $options,
    '#description' => filter_xss($description),
  );
  // Clear the cache bins on submit.
  $form['#submit'][] = 'advagg_css_compress_admin_settings_form_submit';

  return system_settings_form($form);
}

// Submit callback.
/**
 * Clear out the advagg cache bin when the save configuration button is pressed.
 */
function advagg_css_compress_admin_settings_form_submit($form, &$form_state) {
  $cache_bins = advagg_flush_caches();
  foreach ($cache_bins as $bin) {
    cache_clear_all('*', $bin, TRUE);
  }
}
