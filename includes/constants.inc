<?php

/**
 * @file
 * Constants for the advagg module.
 */

/**
 * Default value to see if advanced CSS/JS aggregation is enabled.
 */
define('ADVAGG_ENABLED', TRUE);

/**
 * Default stale file threshold is 6 days for mtime.
 */
define('ADVAGG_STALE_FILE_THRESHOLD', 518400);

/**
 * Default stale file threshold is 3 days for atime.
 */
define('ADVAGG_STALE_FILE_LAST_USED_THRESHOLD', 259200);

/**
 * Default file last used check-in is 12 hours.
 */
define('ADVAGG_FILE_LAST_USED_INTERVAL', 1296000);

/**
 * Default gzip compression setting.
 */
define('ADVAGG_GZIP_COMPRESSION', FALSE);

/**
 * Default generate the aggregate async.
 */
define('ADVAGG_ASYNC_GENERATION', FALSE);

/**
 * How long to wait for the server to come back with an async opp.
 */
define('ADVAGG_SOCKET_TIMEOUT', 1);

/**
 * Default file checksum mode.
 */
define('ADVAGG_CHECKSUM_MODE', 'filemtime');

/**
 * Default value for writing debug info to watchdog.
 */
define('ADVAGG_DEBUG', FALSE);

/**
 * Default value for number of files that can be added before using @import.
 */
define('ADVAGG_CSS_COUNT_THRESHOLD', 25);

/**
 * Default value for not using @import if logged in and not IE.
 */
define('ADVAGG_CSS_LOGGED_IN_IE_DETECT', TRUE);

/**
 * Default value for creating a htaccess file in the advagg directories.
 */
define('ADVAGG_DIR_HTACCESS', TRUE);

/**
 * Default value for a custom files directory just for advagg directories.
 */
define('ADVAGG_CUSTOM_FILES_DIR', '');

/**
 * Default function used to render css output.
 */
define('ADVAGG_CSS_RENDER_FUNCTION', 'advagg_unlimited_css_builder');

/**
 * Default function used to render js output.
 */
define('ADVAGG_JS_RENDER_FUNCTION', 'advagg_js_builder');

/**
 * Default function used to save files.
 */
define('ADVAGG_FILE_SAVE_FUNCTION', 'advagg_file_saver');

/**
 * Default value for rebuilding the bundle on cache flush.
 */
define('ADVAGG_REBUILD_ON_FLUSH', FALSE);

/**
 * Default value to see if advanced CSS/JS aggregation is enabled.
 */
define('ADVAGG_CLOSURE', TRUE);

/**
 * Default value to see if JS bundle matching should be strict.
 */
define('ADVAGG_STRICT_JS_BUNDLES', TRUE);

/**
 * Default mode for aggregate creation.
 *
 * 0 - Wait for locks.
 * 1 - Do not wait for locks.
 * 2 - Only serve aggregated files if they are already built.
 */
define('ADVAGG_AGGREGATE_MODE', 2);

/**
 * Default mode of advagg in regards to the page cache.
 *
 * FALSE  - Cache all pages.
 * TRUE   - Don't cache page if aggregate could be included on that page & it is
 *          not.
 */
define('ADVAGG_PAGE_CACHE_MODE', TRUE);

/**
 * Default mode of advagg_bundle_built() in regards to how file_exists is used.
 *
 * FALSE  - use file_exists.
 * TRUE   - use cache_get instead of file_exists if possible.
 */
define('ADVAGG_BUNDLE_BUILT_MODE', FALSE);

/**
 * Default value to see if we can use the STREAM_CLIENT_ASYNC_CONNECT flag.
 */
define('ADVAGG_ASYNC_SOCKET_CONNECT', FALSE);

/**
 * Default value to see if we removed old files/bundles from the database.
 */
define('ADVAGG_PRUNE_ON_CRON', TRUE);

/**
 * Default value to see if we cache at the advagg_processor level.
 */
define('ADVAGG_USE_FULL_CACHE', TRUE);

/**
 * Default value to see if preprocess JavaScript files.
 */
define('ADVAGG_PREPROCESS_JS', TRUE);

/**
 * Default value to see if preprocess CSS files.
 */
define('ADVAGG_PREPROCESS_CSS', TRUE);

/**
 * Default value to see if preprocess CSS files.
 */
define('ADVAGG_ONLY_CSS_FROM_VARIABLES', FALSE);

/**
 * Default value for server address.
 */
define('ADVAGG_SERVER_ADDR', FALSE);
