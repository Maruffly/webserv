#!/usr/bin/perl
use strict;
use warnings;
use CGI;

print "Content-Type: text/html\n\n";
print "<html><head><title>Perl CGI Test</title></head>";
print "<body>";
print "<h1>Perl CGI Working!</h1>";
print "<p>Time: " . localtime() . "</p>";
print "<p>Perl Version: $]</p>";
print "<p>Request Method: $ENV{REQUEST_METHOD}</p>";
print "</body></html>";