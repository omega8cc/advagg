<?php

/**
 * @file
 * Admin page callbacks for the advanced CSS/JS aggregation module.
 */

/**
 * Page generation function for admin/settings/advagg
 */
function advagg_admin_page() {
  $output = '';
  return $output . drupal_get_form('advagg_admin_settings_form');
}

/**
 * Form builder; Configure advagg settings.
 *
 * @ingroup forms
 * @see system_settings_form()
 */
function advagg_admin_settings_form() {
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
  $form['info'] = array(
    '#type'          => 'fieldset',
    '#collapsible'   => TRUE,
    '#collapsed'     => TRUE,
    '#title'         => t('Hook Theme Info'),
  );
  init_theme();
  $hooks = theme_get_registry();
  $data = implode("\n", $hooks['page']['preprocess functions']);
  $form['info']['advagg_debug_info'] = array(
    '#type'           => 'textarea',
    '#default_value'  => $data,
    '#rows'           => count($hooks['page']['preprocess functions'])+1,
  );
  $form['flush'] = array(
    '#type'         => 'fieldset',
    '#title'        => t('Smart cache flush'),
    '#description'  => t('Scan all files referenced in aggregated files. If any of them have changed, increment the counters containing that file and rebuild the bundle.'),
  );
  $form['flush']['advagg_flush'] = array(
    '#type' => 'submit',
    '#value' => t('Flush AdvAgg Cache'),
    '#submit' => array('advagg_admin_flush_cache_button'),
  );
  $form['rebuild'] = array(
    '#type'         => 'fieldset',
    '#collapsible'   => TRUE,
    '#collapsed'     => TRUE,
    '#title'        => t('Cache Rebuild'),
    '#description'  => t('Recreate all aggregated files. Useful if JS or CSS compression was just enabled.'),
  );
  $form['rebuild']['advagg_rebuild'] = array(
    '#type' => 'submit',
    '#value' => t('Rebuild AdvAgg Cache'),
    '#submit' => array('advagg_admin_rebuild_bundles'),
  );
  $form['forced_rebuild'] = array(
    '#type'         => 'fieldset',
    '#collapsible'   => TRUE,
    '#collapsed'     => TRUE,
    '#title'        => t('Forced Cache Rebuild'),
    '#description'  => t('Recreate all aggregated files by incrementing internal counter for every bundle.'),
  );
  $form['forced_rebuild']['advagg_forced_build'] = array(
    '#type' => 'submit',
    '#value' => t('Force all counters to be increment by one'),
    '#submit' => array('advagg_admin_increment_rebuild_bundles'),
  );

  return system_settings_form($form);
}

/**
 * Validate form values. Used to unset variables before they get saved.
 */
function advagg_admin_settings_form_validate($form, &$form_state) {
  unset($form_state['values']['advagg_debug_info']);
}

/**
 * Cache clear button.
 */
function advagg_admin_flush_cache_button() {
  global $_advagg;
  _drupal_flush_css_js();
  advagg_flush_caches();

  drupal_set_message(t('Advanced CSS/JS Aggregation cache scanned and out of date bundles have been incremented and rebuilt. %count done.', array('%count' => count($_advagg['rebuilt']))));
}

/**
 * Cache clear callback for admin_menu/flush-cache/advagg.
 */
function advagg_admin_flush_cache() {
  advagg_admin_flush_cache_button();
  drupal_goto();
}

/**
 * Rebuild bundles.
 */
function advagg_admin_rebuild_bundles() {
  // Get list of all bundles.
  $bundles = array();
  $result = db_query("SELECT bundle_md5 FROM {advagg_bundles} GROUP BY bundle_md5");
  while ($bundle_md5 = db_result($result)) {
    $bundles[] = $bundle_md5;
  }

  // Rebuild each one.
  foreach ($bundles as $bundle_md5) {
    advagg_rebuld_bundle($bundle_md5, '', TRUE);
  }

  drupal_set_message(t('Advanced CSS/JS Aggregation cache rebuilt. %count files recreated.', array('%count' => count($bundles))));
}

/**
 * Rebuild bundles.
 */
function advagg_admin_increment_rebuild_bundles() {
  // Increment All Counters.
  db_query("UPDATE {advagg_bundles} SET counter = counter + 1");

  // Get list of all bundles.
  $bundles = array();
  $result = db_query("SELECT bundle_md5 FROM {advagg_bundles} GROUP BY bundle_md5");
  while ($bundle_md5 = db_result($result)) {
    $bundles[] = $bundle_md5;
  }

  // Rebuild each one.
  foreach ($bundles as $bundle_md5) {
    advagg_rebuld_bundle($bundle_md5, '', TRUE);
  }

  drupal_set_message(t('Advanced CSS/JS Aggregation cache incremented and rebuilt. %count files recreated.', array('%count' => count($bundles))));
}
