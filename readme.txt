
Fast 404 issue:
  Assuming that you followed a guide like this http://2bits.com/drupal-planet/reducing-server-resource-utilization-busy-sites-implementing-fast-404s-drupal.html Advanced CSS/JS
  Aggragetor works similar to imagecache. We need to add in 2 exceptions for
  the 2 directories that advagg uses. Replace this if statement

if (!strpos($_SERVER['QUERY_STRING'], 'imagecache')) {

  with one like this:

if (!strpos($_SERVER['QUERY_STRING'], 'imagecache') && !strpos($_SERVER['QUERY_STRING'], '/advagg_')) {

  If you are still having issues