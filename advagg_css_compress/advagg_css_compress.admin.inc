<?php

/**
 * @file
 * Admin page callbacks for the advagg CSS compression module.
 */

/**
 * Page generation function for admin/settings/css-compress
 */
function advagg_css_compress_admin_page() {
  $output = '';
  return $output . drupal_get_form('advagg_css_compress_admin_settings_form');
}

/**
 * Form builder; Configure advagg settings.
 *
 * @ingroup forms
 * @see system_settings_form()
 */
function advagg_css_compress_admin_settings_form() {
  $form = array();

  $form['advagg_css_compress_preserve_css'] = array(
    '#type'           => 'checkbox',
    '#title'          => t('Preserve CSS'),
    '#default_value'  => variable_get('advagg_css_compress_preserve_css', ADVAGG_CSS_COMPRESS_PRESERVE_CSS),
    '#description'    => t('If disabled CSS selectors will try to be merged together, significantly reducing the css file size. May result in broken layouts is disabled.'),
  );

  return system_settings_form($form);
}
