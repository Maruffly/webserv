#!/usr/bin/python3
import sys
import os

# Debug output to stderr
sys.stderr.write("CGI Script Starting\n")

# Print environment variables for debugging
sys.stderr.write("Environment Variables:\n")
for key, value in os.environ.items():
    sys.stderr.write(f"{key}: {value}\n")

# Print headers with proper line endings
sys.stdout.write("Content-Type: text/html\r\n")
sys.stdout.write("\r\n")

# Print HTML content
sys.stdout.write("""<!DOCTYPE html>
<html>
<head>
    <title>CGI Test Script</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .container { max-width: 600px; margin: 0 auto; }
        h1 { color: #2c3e50; }
        .success { color: #27ae60; font-weight: bold; }
        .info { background: #ecf0f1; padding: 15px; border-radius: 5px; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>🐍 CGI Test Script</h1>
        <p class='success'>✅ CGI script executed successfully!</p>
        <div class='info'>
            <h3>Script Information:</h3>
            <ul>
                <li><strong>Script:</strong> test.py</li>
                <li><strong>Interpreter:</strong> Python 3</li>
                <li><strong>Location:</strong> /cgi-bin/</li>
            </ul>
        </div>
        <p>This demonstrates that your webserv can execute CGI scripts.</p>
        <a href='/'>← Back to Home</a>
    </div>
</body>
</html>""")

# Force flush of both streams
sys.stdout.flush()
sys.stderr.flush()