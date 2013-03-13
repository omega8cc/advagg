<?php

/**
 * @file
 * Advanced CSS/JS aggregation js compression module.
 */

/**
 * Implement hook_advagg_get_js_file_contents_alter().
 */
function advagg_js_compress_advagg_get_js_file_contents_alter(&$contents, $filename, $aggregate_settings) {
  if (empty($aggregate_settings['variables']['advagg_js_compress_agg_files'])) {
    return;
  }

  advagg_js_compress_prep($contents, $filename, $aggregate_settings);
}

/**
 * Implements hook_advagg_save_aggregate_alter().
 *
 * Used to add in a .gz file if none exits.
 *
 * @param $files_to_save
 *   array($uri => $contents)
 * @param $aggregate_settings
 *   array of settings.
 */
function advagg_js_compress_advagg_save_aggregate_alter(&$files_to_save, $aggregate_settings) {
  // Return if gzip is disabled.
  if (empty($aggregate_settings['variables']['advagg_gzip'])) {
    return;
  }
  // Return if packer is disabled.
  if (empty($aggregate_settings['variables']['advagg_js_compress_packer'])) {
    return;
  }

  // See if a .gz file already exists.
  $gzip_exists = FALSE;
  foreach ($files_to_save as $uri => $contents) {
    // See if this uri contains .gz near the end of it.
    $pos = strripos($uri, '.gz', 91 + strlen(ADVAGG_SPACE)*3);
    if (!empty($pos)) {
      $len = strlen($uri);
      // .gz file exists, exit loop.
      if ($pos == $len-3) {
        $gzip_exists = TRUE;
        break;
      }
    }
  }

  // Use the first file in the array.
  $data = reset($files_to_save);
  $uri = key($files_to_save);

  // If a .gz file does not exist, create one.
  if (!$gzip_exists) {
    // Compress it and add it to the $files_to_save array.
    $compressed = gzencode($data, 9, FORCE_GZIP);
    $files_to_save[$uri . '.gz'] = $compressed;
  }

  // Use packer on non gzip JS data.
  advagg_js_compress_jspacker($data);
  $files_to_save[$uri] = $data;
}

/**
 * Compress a JS string
 *
 * @param $contents
 *   Javascript string.
 * @param $filename
 *   filename.
 */
function advagg_js_compress_prep(&$contents, $filename, $aggregate_settings) {
  // Get the JS string length before the compression operation.
  $contents_before = $contents;
  $before = strlen($contents);

  // Use the compressor.
  $compressor = $aggregate_settings['variables']['advagg_js_compressor'];
  if ($compressor == 0) {
    advagg_js_compress_jsminplus($contents);
  }
  elseif ($compressor == 1) {
    $contents = jsmin($contents);
  }

  // Make sure compression ratios are good.
  $after = strlen($contents);
  $ratio = 0;
  if ($before != 0) {
    $ratio = ($before - $after) / $before;
  }
  // Make sure the returned string is not empty or has a VERY high
  // compression ratio.
  if (empty($contents) || empty($ratio) || $ratio > $aggregate_settings['variables']['advagg_js_max_compress_ratio']) {
    $contents = $contents_before;
  }
  else {
    $url = url($filename, array('absolute' => TRUE));
    $contents = "/* Source and licensing information for the line(s) below can be found at $url. */\n" . $contents . ";\n/* Source and licensing information for the above line(s) can be found at $url. */\n";
  }
}

/**
 * Compress a JS string using jsmin+
 *
 * @param $contents
 *   Javascript string.
 * @return
 *   array with the size before and after.
 */
function advagg_js_compress_jsminplus(&$contents) {
  $contents_before = $contents;

  // Only include jsminplus.inc if the JSMinPlus class doesn't exist.
  if (!class_exists('JSMinPlus')) {
    include(drupal_get_path('module', 'advagg_js_compress') . '/jsminplus.inc');
  }
  ob_start();
  try {
    // Strip Byte Order Marks (BOM's) from the file, JSMin+ cannot parse these.
    $contents = str_replace(pack("CCC", 0xef, 0xbb, 0xbf), "", $contents);
    // JSMin+ the contents of the aggregated file.
    $contents = JSMinPlus::minify($contents);
    $error = trim(ob_get_contents());
    if (!empty($error)) {
      throw new Exception($error);
    }
  }
  catch (Exception $e) {
    // Log the exception thrown by JSMin+ and roll back to uncompressed content.
    watchdog('advagg', $e->getMessage() . '<pre>' . $contents_before . '</pre>', NULL, WATCHDOG_WARNING);
    $contents = $contents_before;
    $after = $before;
  }
  ob_end_clean();
}

/**
 * Compress a JS string using packer.
 *
 * @param $contents
 *   Javascript string.
 */
function advagg_js_compress_jspacker(&$contents) {
  // Use Packer on the contents of the aggregated file.
  if (!class_exists('JavaScriptPacker')) {
    include(drupal_get_path('module', 'advagg_js_compress') . '/jspacker.inc');
  }

  // Strip Byte Order Marks (BOM's) from the file, packer cannot parse these.
  $contents = str_replace(pack("CCC", 0xef, 0xbb, 0xbf), "", $contents);

  // Add semicolons to the end of lines if missing.
  $contents = str_replace("}\n", "};\n", $contents);
  $contents = str_replace("\nfunction", ";\nfunction", $contents);

  // Remove char returns,
  $contents = str_replace("\n\r", "", $contents);
  $contents = str_replace("\r", "", $contents);
  $contents = str_replace("\n", "", $contents);

  $packer = new JavaScriptPacker($contents, 62, TRUE, FALSE);
  $contents = $packer->pack();
}
