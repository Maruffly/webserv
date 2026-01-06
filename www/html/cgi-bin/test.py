#!/usr/bin/env python3
import os
import sys
import json
import time

# Construire une petite réponse JSON pour limiter la charge
payload = {
    "status": "ok",
    "script": "test.py",
    "pid": os.getpid(),
    "timestamp": time.time(),
    "cwd": os.getcwd()
}
body = json.dumps(payload) + "\n"
body_bytes = body.encode("utf-8")

sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Type: application/json\r\n")
sys.stdout.write(f"Content-Length: {len(body_bytes)}\r\n")
sys.stdout.write("Connection: close\r\n\r\n")
sys.stdout.write(body)
sys.stdout.flush()

# Quelques informations de debug sur stderr (ne perturbe pas la réponse)
sys.stderr.write("=== CGI Debug ===\n")
sys.stderr.write(f"PID: {os.getpid()}\n")
sys.stderr.write(f"Request method: {os.environ.get('REQUEST_METHOD', 'N/A')}\n")
sys.stderr.write(f"Query: {os.environ.get('QUERY_STRING', 'N/A')}\n")
sys.stderr.flush()
