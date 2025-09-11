#!/usr/bin/php-cgi
<?php
header("Content-Type: text/html");

echo "<html>";
echo "<head><title>PHP CGI Test</title></head>";
echo "<body>";
echo "<h1>PHP CGI Working!</h1>";
echo "<p>Time: " . date('Y-m-d H:i:s') . "</p>";
echo "<p>PHP Version: " . phpversion() . "</p>";
echo "<p>Request Method: " . ($_SERVER['REQUEST_METHOD'] ?? 'Unknown') . "</p>";

// Afficher quelques variables d'environnement
echo "<h2>Environment Variables:</h2>";
echo "<p>GATEWAY_INTERFACE: " . ($_SERVER['GATEWAY_INTERFACE'] ?? 'Not set') . "</p>";
echo "<p>SCRIPT_NAME: " . ($_SERVER['SCRIPT_NAME'] ?? 'Not set') . "</p>";
echo "<p>QUERY_STRING: " . ($_SERVER['QUERY_STRING'] ?? 'None') . "</p>";

echo "</body>";
echo "</html>";
?>