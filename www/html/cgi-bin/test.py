#!/usr/bin/env python3
import os
import sys

# UN SEUL en-tête Content-Type, suivi de ligne vide
sys.stdout.write("Content-Type: text/html\r\n\r\n")

# Maintenant on peut écrire le contenu HTML
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
        <h1>🚀 CGI Test Script</h1>
        <p class='success'>✅ CGI script executed successfully!</p>
        <div class='info'>
            <h3>Script Information:</h3>
            <ul>
                <li><strong>Script:</strong> test.py</li>
                <li><strong>Interpreter:</strong> Python 3</li>
                <li><strong>Working dir:</strong> {}</li>
                <li><strong>Script path:</strong> {}</li>
            </ul>
        </div>
        <p>This demonstrates that your webserv can execute CGI scripts.</p>
        <a href='/'>← Back to Home</a>
    </div>
</body>
</html>""".format(os.getcwd(), os.path.abspath(__file__)))

# Debug sur stderr (ne perturbe pas la sortie CGI)
sys.stderr.write("=== CGI Debug Info ===\n")
sys.stderr.write(f"Python: {sys.executable}\n")
sys.stderr.write(f"Working dir: {os.getcwd()}\n")
sys.stderr.write(f"REQUEST_METHOD: {os.environ.get('REQUEST_METHOD', 'N/A')}\n")
sys.stderr.write(f"QUERY_STRING: {os.environ.get('QUERY_STRING', 'N/A')}\n")

# Flush obligatoire
sys.stdout.flush()
sys.stderr.flush()