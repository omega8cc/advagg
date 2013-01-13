<?php

/**
 * @file
 * Async functions for the advagg module.
 */

/**
 * Perform an HTTP request; does not wait for reply & you will never get it
 * back.
 *
 * @see drupal_http_request()
 *
 * This is a flexible and powerful HTTP client implementation. Correctly
 * handles GET, POST, PUT or any other HTTP requests.
 *
 * @param $url
 *   A string containing a fully qualified URI.
 * @param array $options
 *   (optional) An array that can have one or more of the following elements:
 *   - headers: An array containing request headers to send as name/value pairs.
 *   - method: A string containing the request method. Defaults to 'GET'.
 *   - data: A string containing the request body, formatted as
 *     'param=value&param=value&...'. Defaults to NULL.
 *   - max_redirects: An integer representing how many times a redirect
 *     may be followed. Defaults to 3.
 *   - timeout: A float representing the maximum number of seconds the function
 *     call may take. The default is 30 seconds. If a timeout occurs, the error
 *     code is set to the HTTP_REQUEST_TIMEOUT constant.
 *   - context: A context resource created with stream_context_create().
 * @return bool
 *   return value from advagg_async_send_http_request().
 */
function advagg_async_connect_http_request($url, array $options = array()) {
  $result = new stdClass();

  // Parse the URL and make sure we can handle the schema.
  $uri = @parse_url($url);

  if (empty($uri)) {
    $result->error = 'unable to parse URL';
    $result->code = -1001;
    return $result;
  }

  if (!isset($uri['scheme'])) {
    $result->error = 'missing schema';
    $result->code = -1002;
    return $result;
  }

  // Merge the default options.
  $options += array(
    'headers' => array(),
    'method' => 'GET',
    'data' => NULL,
    'max_redirects' => 3,
    'timeout' => 30.0,
    'context' => NULL,
  );
  // stream_socket_client() requires timeout to be a float.
  $options['timeout'] = (float) $options['timeout'];

  switch ($uri['scheme']) {
    case 'http':
    case 'feed':
      $port = isset($uri['port']) ? $uri['port'] : 80;
      $socket = 'tcp://' . $uri['host'] . ':' . $port;
      // RFC 2616: "non-standard ports MUST, default ports MAY be included".
      // We don't add the standard port to prevent from breaking rewrite rules
      // checking the host that do not take into account the port number.
      if (empty($options['headers']['Host'])) {
        $options['headers']['Host'] = $uri['host'];
      }
      if ($port != 80) {
        $options['headers']['Host'] .= ':' . $port;
      }
      break;
    case 'https':
      // Note: Only works when PHP is compiled with OpenSSL support.
      $port = isset($uri['port']) ? $uri['port'] : 443;
      $socket = 'ssl://' . $uri['host'] . ':' . $port;
      if (empty($options['headers']['Host'])) {
        $options['headers']['Host'] = $uri['host'];
      }
      if ($port != 443) {
        $options['headers']['Host'] .= ':' . $port;
      }
      break;
    default:
      $result->error = 'invalid schema ' . $uri['scheme'];
      $result->code = -1003;
      return $result;
  }

  $flags = STREAM_CLIENT_CONNECT;
  if (variable_get('advagg_async_socket_connect', ADVAGG_ASYNC_SOCKET_CONNECT)) {
    $flags = STREAM_CLIENT_ASYNC_CONNECT | STREAM_CLIENT_CONNECT;
  }
  if (empty($options['context'])) {
    $fp = @stream_socket_client($socket, $errno, $errstr, $options['timeout'], $flags);
  }
  else {
    // Create a stream with context. Allows verification of a SSL certificate.
    $fp = @stream_socket_client($socket, $errno, $errstr, $options['timeout'], $flags, $options['context']);
  }

  // Make sure the socket opened properly.
  if (!$fp) {
    // When a network error occurs, we use a negative number so it does not
    // clash with the HTTP status codes.
    $result->code = -$errno;
    $result->error = trim($errstr) ? trim($errstr) : t('Error opening socket @socket', array('@socket' => $socket));

    return $result;
  }

  // Non blocking stream.
  stream_set_blocking($fp, 0);

  // Construct the path to act on.
  $path = isset($uri['path']) ? $uri['path'] : '/';
  if (isset($uri['query'])) {
    $path .= '?' . $uri['query'];
  }

  // Merge the default headers.
  $options['headers'] += array(
    'User-Agent' => 'Drupal (+http://drupal.org/)',
  );

  // Only add Content-Length if we actually have any content or if it is a POST
  // or PUT request. Some non-standard servers get confused by Content-Length in
  // at least HEAD/GET requests, and Squid always requires Content-Length in
  // POST/PUT requests.
  $content_length = strlen($options['data']);
  if ($content_length > 0 || $options['method'] == 'POST' || $options['method'] == 'PUT') {
    $options['headers']['Content-Length'] = $content_length;
  }

  // If the server URL has a user then attempt to use basic authentication.
  if (isset($uri['user'])) {
    $options['headers']['Authorization'] = 'Basic ' . base64_encode($uri['user'] . (!empty($uri['pass']) ? ":" . $uri['pass'] : ''));
  }

  // If the database prefix is being used by SimpleTest to run the tests in a copied
  // database then set the user-agent header to the database prefix so that any
  // calls to other Drupal pages will run the SimpleTest prefixed database. The
  // user-agent is used to ensure that multiple testing sessions running at the
  // same time won't interfere with each other as they would if the database
  // prefix were stored statically in a file or database variable.
  $test_info = &$GLOBALS['drupal_test_info'];
  if (!empty($test_info['test_run_id'])) {
    $options['headers']['User-Agent'] = drupal_generate_test_ua($test_info['test_run_id']);
  }

  $request = $options['method'] . ' ' . $path . " HTTP/1.0\r\n";
  foreach ($options['headers'] as $name => $value) {
    $request .= $name . ': ' . trim($value) . "\r\n";
  }
  $request .= "\r\n" . $options['data'];
  $result->request = $request;

  return advagg_async_send_http_request($fp, $request, $options['timeout']);
}

/**
 * Perform an HTTP request; does not wait for reply & you never will get it
 * back.
 *
 * @see drupal_http_request()
 *
 * This is a flexible and powerful HTTP client implementation. Correctly
 * handles GET, POST, PUT or any other HTTP requests.
 *
 * @param $fp
 *   (optional) A file pointer.
 * @param $request
 *   (optional) A string containing the request headers to send to the server.
 * @param $timeout
 *   (optional) An integer holding the stream timeout value.
 * @return bool
 *   TRUE if function worked as planed.
 */
function advagg_async_send_http_request($fp = NULL, $request = '', $timeout = 30) {
  static $requests = array();
  static $registered = FALSE;

  // Store data in a static, and register a shutdown function.
  $args = array($fp, $request, $timeout);
  if (!empty($fp)) {
    $requests[] = $args;
    if (!$registered) {
      register_shutdown_function(__FUNCTION__);
      $registered = TRUE;
    }
    return TRUE;
  }

  // Shutdown function run.
  if (empty($requests)) {
    return FALSE;
  }

  $streams = array();
  foreach ($requests as $id => $values) {
    list($fp, $request, $timeout) = $values;
    $streams[$id] = $fp;
  }

  $retry_count = 2;
  // Run the loop as long as we have a stream to write to.
  while (!empty($streams)) {
    // Set the read and write vars to the streams var.
    $read = $write = $streams;
    $except = array();

    // Do some voodoo and open all streams at once.
    $n = @stream_select($read, $write, $except, $timeout);

    // We have some streams to write to.
    if (!empty($n)) {
      // Write to each stream if it is available.
      foreach ($write as $id => $w) {
        fwrite($w, $requests[$id][1]);
        fclose($w);
        unset($streams[$id]);
      }
    }
    // Timed out waiting or all $streams are closed at this point.
    elseif (!empty($retry_count)) {
      $retry_count--;
    }
    else {
      break;
    }
  }
  // Free memory.
  $requests = array();
  if ($n !== FALSE && empty($streams)) {
    return TRUE;
  }
  else {
    return FALSE;
  }
}
