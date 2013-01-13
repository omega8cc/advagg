<?php

/**
 * Generate a checksum for a given filename.
 *
 * @param $filename
 *   filename
 * @return
 *   Checksum value.
 */
function advagg_checksum($filename) {
  advagg_clearstatcache(TRUE, $filename);
  if (file_exists($filename)) {
    $mode = variable_get('advagg_checksum_mode', ADVAGG_CHECKSUM_MODE);
    if ($mode == 'filemtime' && function_exists('filemtime')) {
      $checksum = @filemtime($filename);
      if ($checksum === FALSE) {
        touch($filename);
        advagg_clearstatcache(TRUE, $filename);
        $checksum = @filemtime($filename);
        // Use md5 as a last option.
        if ($checksum === FALSE) {
          $checksum = md5(file_get_contents($filename));
        }
      }
    }
    elseif ($mode = 'md5' && function_exists('md5')) {
      $checksum = md5(file_get_contents($filename));
    }
  }
  else {
    $checksum = '-1';
  }
  return $checksum;
}

/**
 * ***MODIFIED CORE FUNCTIONS BELOW***
 *
 * @see file_save_data()
 * @see file_move()
 * @see file_copy()
 */

/**
 * Save a string to the specified destination.
 *
 * @see file_save_data()
 *
 * @param $data A string containing the contents of the file.
 * @param $dest A string containing the destination location.
 * @param $replace Replace behavior when the destination file already exists.
 *   - FILE_EXISTS_REPLACE - Replace the existing file
 *   - FILE_EXISTS_RENAME - Append _{incrementing number} until the filename is unique
 *   - FILE_EXISTS_ERROR - Do nothing and return FALSE.
 *
 * @return A string containing the resulting filename or 0 on error
 */
function advagg_file_save_data($data, $dest, $replace = FILE_EXISTS_RENAME) {
  $temp = file_directory_temp();
  // On Windows, tempnam() requires an absolute path, so we use realpath().
  $file = tempnam(realpath($temp), 'file');
  if (!$fp = fopen($file, 'wb')) {
    drupal_set_message(t('The file could not be created.'), 'error');
    return 0;
  }
  fwrite($fp, $data);
  fclose($fp);

  if (!advagg_file_move($file, $dest, $replace)) {
    return 0;
  }

  return $file;
}

/**
 * Moves a file to a new location.
 *
 * @see file_move()
 *
 * - Checks if $source and $dest are valid and readable/writable.
 * - Performs a file move if $source is not equal to $dest.
 * - If file already exists in $dest either the call will error out, replace the
 *   file or rename the file based on the $replace parameter.
 *
 * @param $source
 *   Either a string specifying the file location of the original file or an
 *   object containing a 'filepath' property. This parameter is passed by
 *   reference and will contain the resulting destination filename in case of
 *   success.
 * @param $dest
 *   A string containing the directory $source should be copied to. If this
 *   value is omitted, Drupal's 'files' directory will be used.
 * @param $replace
 *   Replace behavior when the destination file already exists.
 *   - FILE_EXISTS_REPLACE: Replace the existing file.
 *   - FILE_EXISTS_RENAME: Append _{incrementing number} until the filename is
 *     unique.
 *   - FILE_EXISTS_ERROR: Do nothing and return FALSE.
 * @return
 *   TRUE for success, FALSE for failure.
 */
function advagg_file_move(&$source, $dest = 0, $replace = FILE_EXISTS_RENAME) {
  $path_original = is_object($source) ? $source->filepath : $source;

  if (advagg_file_copy($source, $dest, $replace)) {
    $path_current = is_object($source) ? $source->filepath : $source;

    if ($path_original == $path_current || file_unmanaged_delete($path_original)) {
      return 1;
    }
    drupal_set_message(t('The removal of the original file %file has failed.', array('%file' => $path_original)), 'error');
  }
  return 0;
}

/**
 * Copies a file to a new location.
 *
 * @see file_copy()
 *
 * This is a powerful function that in many ways performs like an advanced
 * version of copy().
 * - Checks if $source and $dest are valid and readable/writable.
 * - Performs a file copy if $source is not equal to $dest.
 * - If file already exists in $dest either the call will error out, replace the
 *   file or rename the file based on the $replace parameter.
 *
 * @param $source
 *   Either a string specifying the file location of the original file or an
 *   object containing a 'filepath' property. This parameter is passed by
 *   reference and will contain the resulting destination filename in case of
 *   success.
 * @param $dest
 *   A string containing the directory $source should be copied to. If this
 *   value is omitted, Drupal's 'files' directory will be used.
 * @param $replace
 *   Replace behavior when the destination file already exists.
 *   - FILE_EXISTS_REPLACE: Replace the existing file.
 *   - FILE_EXISTS_RENAME: Append _{incrementing number} until the filename is
 *     unique.
 *   - FILE_EXISTS_ERROR: Do nothing and return FALSE.
 * @return
 *   TRUE for success, FALSE for failure.
 */
function advagg_file_copy(&$source, $dest = 0, $replace = FILE_EXISTS_RENAME) {
  $directory = dirname($dest);

  // Process a file upload object.
  if (is_object($source)) {
    $file = $source;
    $source = $file->filepath;
    if (!$basename) {
      $basename = $file->filename;
    }
  }

  $source = realpath($source);
  advagg_clearstatcache(TRUE, $source);
  if (!file_exists($source)) {
    drupal_set_message(t('The selected file %file could not be copied, because no file by that name exists. Please check that you supplied the correct filename.', array('%file' => $source)), 'error');
    return 0;
  }

  // If the destination file is not specified then use the filename of the source file.
  $basename = basename($dest);
  $basename = $basename ? $basename : basename($source);
  $dest = $directory . '/' . $basename;

  // Make sure source and destination filenames are not the same, makes no sense
  // to copy it if they are. In fact copying the file will most likely result in
  // a 0 byte file. Which is bad. Real bad.
  if ($source != realpath($dest)) {
    if (!$dest = file_destination($dest, $replace)) {
      drupal_set_message(t('The selected file %file could not be copied, because a file by that name already exists in the destination.', array('%file' => $source)), 'error');
      return FALSE;
    }

    if (!@copy($source, $dest)) {
      drupal_set_message(t('The selected file %file could not be copied. ' . $dest, array('%file' => $source)), 'error');
      return 0;
    }

    // Give everyone read access so that FTP'd users or
    // non-webserver users can see/read these files,
    // and give group write permissions so group members
    // can alter files uploaded by the webserver.
    @chmod($dest, 0664);
  }

  if (isset($file) && is_object($file)) {
    $file->filename = $basename;
    $file->filepath = $dest;
    $source = $file;
  }
  else {
    $source = $dest;
  }

  return 1; // Everything went ok.
}
