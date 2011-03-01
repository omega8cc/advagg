<?php

/**
 * @file
 * Admin page callbacks for the advanced CSS/JS aggregation module.
 */

/**
 * Page generation fucntion for admin/settings/advagg
 */
function imageinfo_cache_admin_page() {
  $output = '';
  return $output . drupal_get_form('advagg_admin_settings_from');
}

/**
 * Form builder; Configure advagg settings.
 *
 * @ingroup forms
 * @see system_settings_form()
 */
function advagg_admin_settings_from() {
  $form = array();

  $form['advagg_enabled'] = array(
    '#type'           => 'checkbox',
    '#title'          => t('Enable Advanced Aggregation'),
    '#default_value'  => variable_get('advagg_enabled', ADVAGG_ENABLED),
  );
  $form['advagg_gzip_compression'] = array(
    '#type'           => 'checkbox',
    '#title'          => t('Gzip CSS/JS files'),
    '#default_value'  => variable_get('advagg_gzip_compression', ADVAGG_GZIP_COMPRESSION),
  );
  module_load_install('advagg');
  $ret = advagg_check_missing_handler();
  $form['advagg_async_generation'] = array(
    '#type'           => 'checkbox',
    '#title'          => t('Generate CSS/JS files on request (async mode)'),
    '#default_value'  => variable_get('advagg_async_generation', ADVAGG_ASYNC_GENERATION),
    '#disabled'       => $ret['advagg_async_generation']['severity'] == REQUIREMENT_ERROR ? TRUE : FALSE,
    '#description'    => t('Current State: !value', array('!value' => $ret['advagg_async_generation']['value'])),
  );
  $form['advagg_server_addr'] = array(
    '#type'           => 'textfield',
    '#title'          => t('IP Address to send all asynchronous requests to'),
    '#default_value'  => variable_get('advagg_server_addr', FALSE),
    '#description'    => t('If left blank it will use the same server as the request.'),
  );
  $form['advagg_checksum_mode'] = array(
    '#type'           => 'radios',
    '#title'          => t('File Checksum Mode'),
    '#default_value'  => variable_get('advagg_checksum_mode', ADVAGG_CHECKSUM_MODE),
    '#options'        => array(
      'mtime' => 'mtime',
      'md5' => 'md5',
    ),
    '#description'    => t('If Drupal is on multiple servers and the file system is not shared;  using md5 is recommended. The file modificaiton time (mtime) could be different with this type of setup.'),
  );
  $form['advagg_debug'] = array(
    '#type'           => 'checkbox',
    '#title'          => t('Debug to watchdog.'),
    '#default_value'  => variable_get('advagg_debug', ADVAGG_DEBUG),
  );


  return system_settings_form($form);
}
