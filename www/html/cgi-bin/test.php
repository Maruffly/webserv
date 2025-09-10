#!/usr/bin/php-cgi
<?php
header("Content-Type: text/html");
?>
<html>
<head><title>PHP CGI Test</title></head>
<body>
    <h1>PHP CGI Working!</h1>
    <p>Time: <?php echo date('Y-m-d H:i:s'); ?></p>
    <p>PHP Version: <?php echo phpversion(); ?></p>
    <p>Request Method: <?php echo $_SERVER['REQUEST_METHOD']; ?></p>
</body>
</html>