<?php

/**
 * @file
 * CSS processing functions for the advagg module.
 */

/**
 * The CSS processor function for handling aggregation.
 *
 * @param array $variables
 *   The variables array provided by hook_process_html().
 *
 * @see advagg_process_html()
 * @see hook_process_html()
 */
function advagg_process_html_css(&$variables) {
  // @TODO Remove forced return to actually process the CSS.
  return;
  
  // Work from copied values.
  $css = $variables['css'];
  $styles = $variables['styles'];
  $conditional = !empty($variables['conditional_styles']) ? $variables['conditional_styles'] : '';
  // Merge in any remaining CSS not provided by $variables array.
  if (!variable_get('advagg_only_css_from_variables', ADVAGG_ONLY_CSS_FROM_VARIABLES)) {
    $css = array_merge_recursive_distinct($css, drupal_add_css());
  }
  // Merge in inline css.
  $css = array_merge_recursive_distinct($css, advagg_add_css_inline());
  
  // Filter out .less files.
  if (module_exists('less')) {
    advagg_array_filter_preg_match('/.less$/', $css, 1);
  }
      
  // Determine if caching should be used.
  $cache = variable_get('advagg_use_full_cache', ADVAGG_USE_FULL_CACHE);
  $cid = FALSE;
  if ($cache) {
    // Build the cache ID.
    $cid = 'advagg:css:' . md5(serialize(array(
      // Schema, http or https.
      advagg_get_server_schema(),
      // Hostname.
      $_SERVER['HTTP_HOST'],
      // CSS rendering function.
      variable_get('advagg_css_render_function', ADVAGG_CSS_RENDER_FUNCTION),
      // CSS array.
      $css,
      // Conditional CSS markup.
      $conditional,
      // CSS/JS query string.
      variable_get('css_js_query_string', base_convert(REQUEST_TIME, 10, 36)),
    )));
  }
  return;
  // Retrieve existing cache data.
  $existing_cache = $cid ? cache_get($cid, 'cache_advagg_bundle_reuse') : FALSE;
  // Use existing cache data if possible.
  if ($existing_cache && !empty($existing_cache->data)) {
    $variables['styles'] = $existing_cache->data;
  }
  // Otherwise, process the CSS.
  else {
    if ($processed = advagg_process_css($css)) {
      $variables['styles'] = $processed;
    }
    $variables['styles'] .= $conditional;
    // Cache output.
    if ($cache && lock_acquire($cid)) {
      cache_set($cid, $variables['styles'], 'cache_advagg_bundle_reuse', CACHE_TEMPORARY);
      lock_release($cid);
    }
  }
}

/**
 * Returns a themed representation of all stylesheets that should be attached to
 * the page.
 *
 * @see drupal_get_css()
 *
 * It loads the CSS in order, with 'module' first, then 'theme' afterwards.
 * This ensures proper cascading of styles so themes can easily override
 * module styles through CSS selectors.
 *
 * Themes may replace module-defined CSS files by adding a stylesheet with the
 * same filename. For example, themes/garland/system-menus.css would replace
 * modules/system/system-menus.css. This allows themes to override complete
 * CSS files, rather than specific selectors, when necessary.
 *
 * If the original CSS file is being overridden by a theme, the theme is
 * responsible for supplying an accompanying RTL CSS file to replace the
 * module's.
 *
 * @param $css
 *   (optional) An array of CSS files. If no array is provided, the default
 *   stylesheets array is used instead.
 * @param $noagg
 *   (optional) Bool indicating that aggregation should be disabled if TRUE.
 * @return
 *   A string of XHTML CSS tags.
 */
function advagg_process_css($css = NULL, $noagg = FALSE) {
  global $conf;
  $original_css = $css;
  if (!isset($css)) {
    $css = drupal_add_css(CSS_DEFAULT);
  }
  if (empty($css)) {
    return FALSE;
  }

  // Get useful info.
  list($preprocess_css, $scheme, $query_string) = advagg_process_prep($noagg, 'css');

  // Invoke hook_advagg_css_pre_alter() to give installed modules a chance to
  // modify the data in the $javascript array if necessary.
  drupal_alter('advagg_css_pre', $css, $preprocess_css, $scheme);

  // Set variables.
  $external_no_preprocess = array();
  $module_no_preprocess = array();
  $output_no_preprocess = array();
  $output_preprocess = array();
  $theme_no_preprocess = array();
  $inline_no_preprocess = array();
  $files_included = array();
  $files_aggregates_included = array();
  $inline_included = array();

  // Process input.
  foreach ($css as $media => $types) {
    // Setup some variables
    $files_included[$media] = array();
    $files_aggregates_included[$media] = array();
    $inline_included[$media] = array();

    // If CSS preprocessing is off, we still need to output the styles.
    // Additionally, go through any remaining styles if CSS preprocessing is on
    // and output the non-cached ones.
    foreach ($types as $type => $files) {
      if ($type == 'module') {
        // Setup theme overrides for module styles.
        $theme_styles = array();
        foreach (array_keys($css[$media]['theme']) as $theme_style) {
          $theme_styles[] = basename($theme_style);
        }
      }
      foreach ($types[$type] as $file => $preprocess) {
        // If the theme supplies its own style using the name of the module
        // style, skip its inclusion. This includes any RTL styles associated
        // with its main LTR counterpart.
        if ($type == 'module' && in_array(str_replace('-rtl.css', '.css', basename($file)), $theme_styles)) {
          // Unset the file to prevent its inclusion when CSS aggregation is enabled.
          unset($types[$type][$file]);
          continue;
        }
        // If a CSS file is not to be preprocessed and it's an inline CSS blob
        // it needs to *always* appear at the *very bottom*.
        if ($type == 'inline') {
          if (is_array($preprocess)) {
            foreach ($preprocess as $suffix => $blob) {
              $blob = advagg_drupal_load_stylesheet_content($blob, $preprocess);
              // Invoke hook_advagg_css_inline_alter() to give installed modules
              // a chance to modify the contents of $blob if necessary.
              drupal_alter('advagg_css_inline', $blob);

              $inline_no_preprocess[] = array(
                'media' => $media,
                'data' => $blob,
                'prefix' => $file,
                'suffix' => $suffix,
              );
              $inline_included[$media][] = $blob;
            }
          }
          // Unset to prevent its inclusion.
          unset($types[$type][$file]);
          continue;
        }

        $prefix = '';
        $suffix = '';
        // Invoke hook_advagg_css_extra_alter() to
        // give installed modules a chance to modify the prefix or suffix for a
        // given filename.
        $values = array($file, NULL, $prefix, $suffix);
        drupal_alter('advagg_css_extra', $values);
        list($file, $null, $prefix, $suffix) = $values;

        if ($type == 'inline') {
          $file = advagg_drupal_load_stylesheet_content($file, $preprocess);
          // Invoke hook_advagg_css_inline_alter() to give installed modules a
          // chance to modify the contents of $file if necessary.
          drupal_alter('advagg_css_inline', $file);

          $inline_no_preprocess[] = array(
            'media' => $media,
            'data' => $file,
            'prefix' => $prefix,
            'suffix' => $suffix,
          );
          $inline_included[$media][] = $file;
          // Unset to prevent its inclusion.
          unset($types[$type][$file]);
          continue;
        }

        // If a CSS file is not to be preprocessed and it's an external
        // CSS file, it needs to *always* appear at the *very top*,
        // regardless of whether preprocessing is on or off.
        if ($type == 'external') {
          $external_no_preprocess[] = array(
            'media' => $media,
            'href' => $file,
            'prefix' => $prefix,
            'suffix' => $suffix,
          );
          $files_included[$media][$file] = TRUE;
          // Unset the file to prevent its inclusion.
          unset($types[$type][$file]);
          continue;
        }

        // Only include the stylesheet if it exists.
        if (advagg_file_exists($file)) {
          if (!$preprocess || !($scheme === 'public' && $preprocess_css)) {


            // Create URI for file.
            $file_uri = advagg_build_uri($file) . $query_string;
            $files_included[$media][$file] = $preprocess;
            // If a CSS file is not to be preprocessed and it's a module CSS
            // file, it needs to *always* appear at the *top*, regardless of
            // whether preprocessing is on or off.
            if (!$preprocess && $type == 'module') {
              $module_no_preprocess[] = array(
                'media' => $media,
                'href' => $file_uri,
                'prefix' => $prefix,
                'suffix' => $suffix,
              );
            }
            // If a CSS file is not to be preprocessed and it's a theme CSS
            // file, it needs to *always* appear at the *bottom*, regardless of
            // whether preprocessing is on or off.
            elseif (!$preprocess && $type == 'theme') {
              $theme_no_preprocess[] = array(
                'media' => $media,
                'href' => $file_uri,
                'prefix' => $prefix,
                'suffix' => $suffix,
              );
            }

            else {
              $output_no_preprocess[] = array(
                'media' => $media,
                'href' => $file_uri,
                'prefix' => $prefix,
                'suffix' => $suffix,
              );
            }
          }
        }
      }
    }

    if ($scheme == 'public' && $preprocess_css) {
      $files_aggregates_included[$media] = $files_included[$media];
      $files = array();
      foreach ($types as $type) {
        foreach ($type as $file => $cache) {
          if ($cache) {
            $files[] = $file;
            $files_included[$media][$file] = TRUE;
            unset($files_aggregates_included[$file]);
          }
        }
      }
      if (!empty($files)) {
        $preprocess_files = advagg_css_js_file_builder('css', $files, $query_string);
        if (!empty($preprocess_files)) {
          $good = TRUE;
          foreach ($preprocess_files as $preprocess_file => $extra) {
            // Empty aggregate, skip
            if (empty($preprocess_file)) {
              continue;
            }

            if ($extra !== FALSE && is_array($extra)) {
              $prefix = $extra['prefix'];
              $suffix = $extra['suffix'];
              $output_preprocess[] = array(
                'media' => $media,
                'href' => advagg_build_uri($preprocess_file),
                'prefix' => $prefix,
                'suffix' => $suffix,
              );
              $files_aggregates_included[$media][$preprocess_file] = $extra;
            }
            else {
              $good = FALSE;
              break;
            }
          }
        }
        if (empty($good)) {
          // Redo with aggregation turned off and return the new value.
          watchdog('advagg', 'CSS aggregation failed. %filename could not be saved correctly.', array('%filename' => $preprocess_file), WATCHDOG_ERROR);
          $data = advagg_process_css($original_css, TRUE);
          return $data;
        }
      }
    }
  }

  // Default function called: advagg_unlimited_css_builder
  $function = variable_get('advagg_css_render_function', ADVAGG_CSS_RENDER_FUNCTION);
  return $function($external_no_preprocess, $module_no_preprocess, $output_no_preprocess, $output_preprocess, $theme_no_preprocess, $inline_no_preprocess, $inline_included, $files_included, $files_aggregates_included);
}

/**
 * Default callback to aggregate CSS files and inline content.
 *
 * Having the browser load fewer CSS files results in much faster page loads
 * than when it loads many small files. This function aggregates files within
 * the same group into a single file unless the site-wide setting to do so is
 * disabled (commonly the case during site development). To optimize download,
 * it also compresses the aggregate files by removing comments, whitespace, and
 * other unnecessary content. Additionally, this functions aggregates inline
 * content together, regardless of the site-wide aggregation setting.
 *
 * @param $css_groups
 *   An array of CSS groups as returned by drupal_group_css(). This function
 *   modifies the group's 'data' property for each group that is aggregated.
 *
 * @see drupal_group_css()
 * @see drupal_pre_render_styles()
 * @see system_element_info()
 */
function _advagg_aggregate_css(&$css_groups) {
  $preprocess_css = (variable_get('preprocess_css', FALSE) && (!defined('MAINTENANCE_MODE') || MAINTENANCE_MODE != 'update'));

  // For each group that needs aggregation, aggregate its items.
  foreach ($css_groups as $key => $group) {
    switch ($group['type']) {
      // If a file group can be aggregated into a single file, do so, and set
      // the group's data property to the file path of the aggregate file.
      case 'file':
        if ($group['preprocess'] && $preprocess_css) {
          $css_groups[$key]['data'] = drupal_build_css_cache($group['items']);
        }
        break;
      // Aggregate all inline CSS content into the group's data property.
      case 'inline':
        $css_groups[$key]['data'] = '';
        foreach ($group['items'] as $item) {
          $css_groups[$key]['data'] .= drupal_load_stylesheet_content($item['data'], $item['preprocess']);
        }
        break;
    }
  }
}

/**
 * Logic to figure out what kind of css tags to use.
 *
 * @param $external_no_preprocess
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param $module_no_preprocess
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param $output_no_preprocess
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param $output_preprocess
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param $theme_no_preprocess
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param $inline_no_preprocess
 *   array of css data to inline ($media, $data)
 * @param $inline_included
 *   array of inline css included. $a[$media][] = $datablob;
 * @param $files_included
 *   array of css files included. $a[$media][] = $filename
 * @param $files_aggregates_included
 *   array of css files & aggregates included. $a[$media][] = $filename
 * @return
 *   html for loading the css. html for the head.
 */
function advagg_unlimited_css_builder($external_no_preprocess, $module_no_preprocess, $output_no_preprocess, $output_preprocess, $theme_no_preprocess, $inline_no_preprocess, $files_included, $files_aggregates_included, $inline_included) {
  global $user;
  $styles = '';
  $files = array_merge($external_no_preprocess, $module_no_preprocess, $output_no_preprocess, $output_preprocess, $theme_no_preprocess, $inline_no_preprocess);

  // Select method for css html output
  if (count($files) < variable_get('advagg_css_count_threshold', ADVAGG_CSS_COUNT_THRESHOLD)) {
    advagg_unlimited_css_traditional($files, $styles);
  }
  elseif (variable_get('advagg_css_logged_in_ie_detect', ADVAGG_CSS_LOGGED_IN_IE_DETECT) && $user->uid != 0) {
    // Detect IE browsers here
    $is_ie = FALSE;
    if (isset($_SERVER['HTTP_USER_AGENT'])) {
      // Strings for testing found via
      // http://chrisschuld.com/projects/browser-php-detecting-a-users-browser-from-php/
      // Test for v1 - v1.5 IE
      // Test for versions > 1.5
      // Test for Pocket IE
      if (   stristr($_SERVER['HTTP_USER_AGENT'], 'microsoft internet explorer')
            || stristr($_SERVER['HTTP_USER_AGENT'], 'msie')
            || stristr($_SERVER['HTTP_USER_AGENT'], 'mspie')
          ) {
        $is_ie = TRUE;
      }
    }
    // Play Safe and treat as IE if user agent is not set
    else {
      $is_ie = TRUE;
    }

    if ($is_ie) {
      advagg_unlimited_css_import(array_merge($external_no_preprocess, $module_no_preprocess, $output_no_preprocess), $styles);
      advagg_unlimited_css_import($output_preprocess, $styles);
      advagg_unlimited_css_import($theme_no_preprocess, $styles);
      advagg_unlimited_css_traditional($inline_no_preprocess, $styles);
    }
    else {
      advagg_unlimited_css_traditional($files, $styles);
    }
  }
  else {
    advagg_unlimited_css_import(array_merge($external_no_preprocess, $module_no_preprocess, $output_no_preprocess), $styles);
    advagg_unlimited_css_import($output_preprocess, $styles);
    advagg_unlimited_css_import($theme_no_preprocess, $styles);
    advagg_unlimited_css_traditional($inline_no_preprocess, $styles);
  }

  return $styles;
}

/**
 * Use link tags for CSS
 *
 * @param $files
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param &$styles
 *   html string
 */
function advagg_unlimited_css_traditional($files, &$styles) {
  $last_prefix = '';
  $last_suffix = '';
  foreach ($files as $css_file) {
    $media = $css_file['media'];
    $prefix = empty($css_file['prefix']) ? '' : $css_file['prefix'] . "\n";
    $suffix = empty($css_file['suffix']) ? '' : $css_file['suffix'];

    // Group prefixes and suffixes.
    if (isset($css_file['href'])) {
      $href = $css_file['href'];
      if ($prefix != $last_prefix) {
        $styles .= $last_suffix . "\n" . $prefix . '<link type="text/css" rel="stylesheet" media="' . $media . '" href="' . $href . '" />' . "\n";
      }
      else {
        $styles .= '<link type="text/css" rel="stylesheet" media="' . $media . '" href="' . $href . '" />' . "\n";
      }
    }
    else {
      $data = $css_file['data'];
      if ($prefix != $last_prefix) {
        $styles .= $last_suffix . "\n" . $prefix . '<style type="text/css" media="' . $media . '">' . "\n" . $data . "\n" . '</style>' . "\n";
      }
      else {
        $styles .= '<style type="text/css" media="' . $media . '">' . "\n" . $data . "\n" . '</style>' . "\n";
      }
    }
    $last_prefix = $prefix;
    $last_suffix = $suffix;
  }
  $styles .= empty($last_suffix) ? '' : $last_suffix . "\n";
}

/**
 * Use import tags for CSS
 *
 * @param $files
 *   array of css files ($media, $href)
 * @param &$styles
 *   html string
 */
function advagg_unlimited_css_import($files, &$styles) {
  $counter = 0;
  $media = NULL;
  $import = '';
  foreach ($files as $css_file) {
    $media_new = $css_file['media'];
    $href = $css_file['href'];
    if ($media_new != $media || $counter > variable_get('advagg_css_count_threshold', ADVAGG_CSS_COUNT_THRESHOLD)) {
      if ($media && !empty($import)) {
        $styles .= "\n" . '<style type="text/css" media="' . $media . '">' . "\n" . $import . '</style>';
        $import = '';
      }
      $counter = 0;
      $media = $media_new;
    }
    $import .= '@import "' . $href . '";' . "\n";
    $counter++;
  }
  if ($media && !empty($import)) {
    $styles .= "\n" . '<style type="text/css" media="' . $media . '">' . "\n" . $import . '</style>';
  }
}

/**
 * Merge 2 css arrays together.
 *
 * @param $array1
 *   first array
 * @param $array2
 *   second array
 * @return
 *   combined array
 */
function advagg_merge_css($array1, $array2) {
  if (!empty($array2)) {
    foreach ($array2 as $media => $types) {
      foreach ($types as $type => $files) {
        foreach ($files as $file => $preprocess) {
          $array1[$media][$type][$file] = $preprocess;
        }
      }
    }
  }
  return $array1;
}

/**
 * Merge 2 css arrays together.
 *
 * @param $array1
 *   first array
 * @param $array2
 *   second array
 * @return
 *   combined array
 */
function advagg_merge_inline_css($array1, $array2) {
  foreach ($array2 as $media => $types) {
    foreach ($types as $type => $blobs) {
      foreach ($blobs as $prefix => $data) {
        foreach ($data as $suffix => $blob) {
          $array1[$media][$type][$prefix][$suffix] = $blob;
        }
      }
    }
  }
  return $array1;
}

/**
 * Adds a CSS file to the stylesheet queue.
 *
 * @param $data
 *   (optional) The CSS data that will be set. If not set then the inline CSS
 *   array will be passed back.
 * @param $media
 *   (optional) The media type for the stylesheet, e.g., all, print, screen.
 * @param $prefix
 *   (optional) prefix to add before the inlined css.
 * @param $suffix
 *   (optional) suffix to add after the inlined css.
 * @return
 *   An array of CSS files.
 */
function advagg_add_css_inline($data = NULL, $media = 'all', $prefix = NULL, $suffix = NULL) {
  static $css = array();

  // Store inline data in a static.
  if (isset($data)) {
    if (!isset($css[$media]['inline'][$prefix][$suffix])) {
      $css[$media]['inline'][$prefix][$suffix] = $data;
    }
    else {
      $css[$media]['inline'][$prefix][$suffix] .= "\n" . $data;
    }
    return;
  }
  else {
    return $css;
  }
}

/**
 * Process the contents of a stylesheet for aggregation.
 *
 * @param $contents
 *   The contents of the stylesheet.
 * @param $optimize
 *   (optional) Boolean whether CSS contents should be minified. Defaults to
 *   FALSE.
 * @return
 *   Contents of the stylesheet including the imported stylesheets.
 */
function advagg_drupal_load_stylesheet_content($contents, $optimize = FALSE) {
  // Remove multiple charset declarations for standards compliance (and fixing Safari problems).
  $contents = preg_replace('/^@charset\s+[\'"](\S*)\b[\'"];/i', '', $contents);

  if ($optimize) {
    // Perform some safe CSS optimizations.
    // Regexp to match comment blocks.
    $comment     = '/\*[^*]*\*+(?:[^/*][^*]*\*+)*/';
    // Regexp to match double quoted strings.
    $double_quot = '"[^"\\\\]*(?:\\\\.[^"\\\\]*)*"';
    // Regexp to match single quoted strings.
    $single_quot = "'[^'\\\\]*(?:\\\\.[^'\\\\]*)*'";
    // Strip all comment blocks, but keep double/single quoted strings.
    $contents = preg_replace(
      "<($double_quot|$single_quot)|$comment>Ss",
      "$1",
      $contents
    );
    // Remove certain whitespace.
    // There are different conditions for removing leading and trailing
    // whitespace.
    // @see http://php.net/manual/en/regexp.reference.subpatterns.php
    $contents = preg_replace('<
      # Strip leading and trailing whitespace.
        \s*([@{};,])\s*
      # Strip only leading whitespace from:
      # - Closing parenthesis: Retain "@media (bar) and foo".
      | \s+([\)])
      # Strip only trailing whitespace from:
      # - Opening parenthesis: Retain "@media (bar) and foo".
      # - Colon: Retain :pseudo-selectors.
      | ([\(:])\s+
    >xS',
      // Only one of the three capturing groups will match, so its reference
      // will contain the wanted value and the references for the
      // two non-matching groups will be replaced with empty strings.
      '$1$2$3',
      $contents
    );
    // End the file with a new line.
    $contents = trim($contents);
    $contents .= "\n";
  }

  // Replaces @import commands with the actual stylesheet content.
  // This happens recursively but omits external files.
  $contents = preg_replace_callback('/@import\s*(?:url\(\s*)?[\'"]?(?![a-z]+:)([^\'"\()]+)[\'"]?\s*\)?\s*;/', '_advagg_drupal_load_stylesheet', $contents);
  return $contents;
}

/**
 * Loads stylesheets recursively and returns contents with corrected paths.
 *
 * This function is used for recursive loading of stylesheets and
 * returns the stylesheet content with all url() paths corrected.
 */
function _advagg_drupal_load_stylesheet($matches) {
  $filename = $matches[1];
  // Load the imported stylesheet and replace @import commands in there as well.
  $file = advagg_build_css_bundle(array($filename));

  // Determine the file's directory.
  $directory = dirname($filename);
  // If the file is in the current directory, make sure '.' doesn't appear in
  // the url() path.
  $directory = $directory == '.' ? '' : $directory . '/';

  // Alter all internal url() paths. Leave external paths alone. We don't need
  // to normalize absolute paths here (i.e. remove folder/... segments) because
  // that will be done later.
  return preg_replace('/url\(\s*([\'"]?)(?![a-z]+:|\/+)/i', 'url(\1' . $directory, $file);
}

/**
 * Logic to figure out what kind of css tags to use.
 *
 * @param $external_no_preprocess
 *   array of css files ($media, $href)
 * @param $module_no_preprocess
 *   array of css files ($media, $href)
 * @param $output_no_preprocess
 *   array of css files ($media, $href)
 * @param $output_preprocess
 *   array of css files ($media, $href, $prefix, $suffix)
 * @param $theme_no_preprocess
 *   array of css files ($media, $href)
 * @param $inline_no_preprocess
 *   array of css data to inline ($media, $data)
 * @param $files_included
 *   array of css files included. $a[$media][] = $filename
 * @param $files_aggregates_included
 *   array of css files & aggregates included. $a[$media][] = $filename
 * @param $inline_included
 *   array of inline css included. $a[$media][] = $datablob;
 * @return
 *   html for loading the css. html for the head.
 */
function advagg_css_array($external_no_preprocess, $module_no_preprocess, $output_no_preprocess, $output_preprocess, $theme_no_preprocess, $inline_no_preprocess, $inline_included, $files_included, $files_aggregates_included) {
  return array(
    'inline' => $inline_included,
    'files' => $files_included,
    'files_aggregates' => $files_aggregates_included,
  );
}

/**
 * Given a list of files, grab their contents and glue it into one big string.
 *
 * @param $files
 *   array of filenames.
 * @return
 *   string containing all the files.
 */
function advagg_build_css_bundle($files) {
  $data = '';
  // Build aggregate CSS file.
  foreach ($files as $file) {
    $contents = drupal_load_stylesheet($file, TRUE);
    // Return the path to where this CSS file originated from.
    $base = base_path() . dirname($file) . '/';
    _drupal_build_css_path(NULL, $base);
    // Prefix all paths within this CSS file, ignoring external and absolute paths.
    $data .= preg_replace_callback('/url\([\'"]?(?![a-z]+:|\/+)([^\'")]+)[\'"]?\)/i', '_drupal_build_css_path', $contents);
  }

  // Per the W3C specification at http://www.w3.org/TR/REC-CSS2/cascade.html#at-import,
  // @import rules must proceed any other style, so we move those to the top.
  $regexp = '/@import[^;]+;/i';
  preg_match_all($regexp, $data, $matches);
  $data = preg_replace($regexp, '', $data);
  $data = implode('', $matches[0]) . $data;
  return $data;
}
