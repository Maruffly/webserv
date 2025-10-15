#include "../../include/Webserv.hpp"
#include "epollManager.hpp"
#include "Cookie.hpp"

 std::vector<char*> epollManager::buildEnv(std::string &scriptPath, const Request &request, 
    const ServerConfig &config, const LocationConfig* location, ClientConnection &conn){
     
        std::vector<std::string> envStore;
        std::string rawUri = request.getUri();
        std::string pathInfo = rawUri;
        std::string queryString;
        size_t qPos = rawUri.find('?');
        if (qPos != std::string::npos) {
            queryString = rawUri.substr(qPos + 1);
            pathInfo = rawUri.substr(0, qPos);
        }

        std::string documentRoot = config.getRoot();
        if (location && !location->getRoot().empty())
            documentRoot = location->getRoot();

        envStore.push_back("GATEWAY_INTERFACE=CGI/1.1");
        envStore.push_back("SERVER_PROTOCOL=HTTP/1.1");
        envStore.push_back("SERVER_SOFTWARE=webserv/1.0");
        envStore.push_back(std::string("REQUEST_METHOD=") + request.getMethod());
        envStore.push_back(std::string("SCRIPT_FILENAME=") + scriptPath);
        envStore.push_back(std::string("SCRIPT_NAME=") + pathInfo);
        envStore.push_back(std::string("REQUEST_URI=") + rawUri);
        envStore.push_back(std::string("PATH_INFO=") + pathInfo);
        envStore.push_back(std::string("QUERY_STRING=") + queryString);
        envStore.push_back(std::string("SERVER_NAME=") + config.getServerName());
        envStore.push_back(std::string("SERVER_PORT=") + toString(config.getPort()));
        envStore.push_back(std::string("REMOTE_ADDR=") + conn.remoteAddr);
        envStore.push_back(std::string("DOCUMENT_ROOT=") + documentRoot);

        std::vector<char*> envp;
        for (size_t i=0;i<envStore.size();++i)
			envp.push_back(strdup(envStore[i].c_str()));
        envp.push_back(NULL);
        return envp;
}


void epollManager::execChild(std::string &scriptPath, const Request &request, 
    const ServerConfig &config, const LocationConfig* location, ClientConnection &conn){
        // chdir to script directory
        std::string dir = dirnameOf(scriptPath);
        chdir(dir.c_str());
        // dup stdio
        dup2(pin[0], STDIN_FILENO);
		dup2(pout[1], STDOUT_FILENO);
		dup2(pout[1], STDERR_FILENO);
        safeClose(pin);
		safeClose(pout);

        // Build env
        std::vector<char*> envp;
        envp = buildEnv(scriptPath, request, config, location, conn);

        // Args (interpreter optional)
        std::vector<char*> args;
        std::string ext = getFileExtension(scriptPath);
        std::string interpreter;
        const std::map<std::string,std::string>& cgiPass = location->getCgiPass();
        std::map<std::string,std::string>::const_iterator it = cgiPass.find(ext);
        if (it == cgiPass.end()) {
			std::map<std::string,std::string>::const_iterator it2 = cgiPass.find(".*");
			if (it2 != cgiPass.end())
			interpreter = it2->second;
		}
        else interpreter = it->second;
        if (!interpreter.empty()) {
			args.push_back(strdup(interpreter.c_str()));
			args.push_back(strdup(scriptPath.c_str()));
        }
        else 
			args.push_back(strdup(scriptPath.c_str()));
        args.push_back(NULL);
    if (!interpreter.empty())
        execve(interpreter.c_str(), args.data(), envp.data());
    else
        execve(scriptPath.c_str(), args.data(), envp.data());
    // If execve fails
    std::exit(EXIT_FAILURE);
}

void epollManager::saveConnInfo(ClientConnection &conn, pid_t pid)
{
    conn.cgiRunning = true;
	conn.cgiPid = pid;
	conn.cgiInFd = pin[1];
	conn.cgiOutFd = pout[0];
	conn.cgiInOffset = 0;
	conn.cgiStart = time(NULL);
}

bool epollManager::startCgiFor(int clientFd, const Request& request, const ServerConfig& config, const LocationConfig* location)
{
    ClientConnection &conn = _clientConnections[clientFd];

   std::string scriptPath = resolveFilePath(conn.uri, config);
    if (scriptPath.empty() || !fileExists(scriptPath)) {
        sendErrorResponse(clientFd, 404, "Not Found");
        return false;
    }
	if (_activeCgiCount >= MAX_CGI_PROCESS) {
        LOG("CGI refused : limite of " + toString(MAX_CGI_PROCESS) + " reached");
        sendErrorResponse(clientFd, 503, "Server Busy");
        return false;
    }
	if (pipe(pin) == -1 || pipe(pout) == -1) {
		sendErrorResponse(clientFd, 502, "Bad Gateway");
		return false;
	}
    // nonblocking
    fcntl(pin[1], F_SETFL, fcntl(pin[1], F_GETFL, 0) | O_NONBLOCK);
    fcntl(pout[0], F_SETFL, fcntl(pout[0], F_GETFL, 0) | O_NONBLOCK);

    pid_t pid = fork();
	if (pid == -1) {
		safeClose(pin);
		safeClose(pout);
		sendErrorResponse(clientFd, 502, "Bad Gateway");
		return false;
	}
    // child
    if (pid == 0)
        execChild(scriptPath, request, config, location, conn);

    // parent
    close(pin[0]);
	close(pout[1]);

    // register fds to epoll
    struct epoll_event ev;
    ev.data.fd = pout[0];
	ev.events = EPOLLIN;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, pout[0], &ev) == -1)
		ERROR_SYS("epoll_ctl add cgi out");

    _cgiOutToClient[pout[0]] = clientFd;
	 _activeCgiCount++;

    // register input if there is a body to send
    if (!conn.body.empty()) {
		ev.data.fd = pin[1];
		ev.events = EPOLLOUT;
		if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, pin[1], &ev) == -1)
			ERROR_SYS("epoll_ctl add cgi in");
		_cgiInToClient[pin[1]] = clientFd;
	}
    // collect connection info
    saveConnInfo(conn, pid);
    return true;
}

// Reads CGI stdout and appends it to the client connection buffer.
void epollManager::drainCgiOutput(int pipeFd, uint32_t events)
{
    (void)events;
    int clientFd = _cgiOutToClient[pipeFd];
    ClientConnection &conn = _clientConnections[clientFd];
    char buf[BUFFER_SIZE]; 
    ssize_t n = read(pipeFd, buf, sizeof(buf));

    if (n > 0) {
		conn.cgiOutBuffer.append(buf, n);
		conn.lastActivity = time(NULL);
		return;
	}
    if (n == 0) 
    {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
		close(pipeFd);
		_cgiOutToClient.erase(pipeFd);
		conn.cgiOutFd = -1;
        finalizeCgiResponse(clientFd);
        return;
    }
    // n == -1: EAGAIN or transient; do nothing
}

// Writes the request body to the CGI stdin pipe when ready.
void epollManager::feedCgiInput(int pipeFd, uint32_t events)
{
    (void)events;
    int clientFd = _cgiInToClient[pipeFd];
    ClientConnection &conn = _clientConnections[clientFd];

    if (conn.body.empty() || conn.cgiInFd == -1)
    {
        // nothing to send
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        close(pipeFd);
        _cgiInToClient.erase(pipeFd);
        conn.cgiInFd = -1;
        return;
    }

    size_t remaining = conn.body.size() - conn.cgiInOffset;
    if (remaining == 0)
    {
        // Terminé
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        close(pipeFd);
        _cgiInToClient.erase(pipeFd);
        conn.cgiInFd = -1;
        return;
    }

    size_t toWrite = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;
    ssize_t w = write(pipeFd, conn.body.data() + conn.cgiInOffset, toWrite);

    if (w > 0)
    {
        conn.cgiInOffset += static_cast<size_t>(w);
        conn.lastActivity = time(NULL);
        if (conn.cgiInOffset >= conn.body.size())
        {
            // tout envoyé, on retire le fd
            epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
            close(pipeFd);
            _cgiInToClient.erase(pipeFd);
            conn.cgiInFd = -1;
        }
        return;
    }
    if (w == -1){
        // Pipe plein temporairement — attendre le prochain EPOLLOUT.
        return ;
    }
    // Si on arrive ici -> erreur non-récupérable
    LOG("Fatal write to CGI stdin, errno=" + toString(errno));
    if (conn.cgiPid > 0)
    {
        kill(conn.cgiPid, SIGKILL);
        conn.cgiPid = -1;
    }
    if (conn.cgiOutFd != -1)
    {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, conn.cgiOutFd, NULL);
        close(conn.cgiOutFd);
        _cgiOutToClient.erase(conn.cgiOutFd);
        conn.cgiOutFd = -1;
    }
    if (pipeFd != -1)
    {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        close(pipeFd);
        _cgiInToClient.erase(pipeFd);
    }
    conn.cgiInFd = -1;
    conn.cgiRunning = false;
    queueErrorResponse(clientFd, 500, "Internal Server Error");
}

// Parses the CGI stdout and fills the Response headers/body.
static void parseCgiOutputToResponse(const std::string& cgiOutput, Response& response)
{
    size_t headerEnd = cgiOutput.find("\r\n\r\n");

    if (headerEnd != std::string::npos)
    {
        std::string headersPart = cgiOutput.substr(0, headerEnd);
        std::string body = cgiOutput.substr(headerEnd + 4);
        std::vector<std::string> headerLines = ParserUtils::split(headersPart, '\n');
        int statusCode = 200;
        std::string statusText = "OK";
        for (size_t i = 0; i < headerLines.size(); ++i) {
            if (!headerLines[i].empty() && headerLines[i][headerLines[i].size()-1] == '\r') headerLines[i].erase(headerLines[i].size()-1);
            size_t colonPos = headerLines[i].find(':');
            if (colonPos != std::string::npos) {
                std::string name = ParserUtils::trim(headerLines[i].substr(0, colonPos));
                std::string value = ParserUtils::trim(headerLines[i].substr(colonPos + 1));
                if (toUpperCase(name) == "STATUS") 
                {
                    // Format: "Status: 302 Found"
                    std::istringstream iss(value); iss >> statusCode; std::string rest; std::getline(iss, rest); if (!rest.empty() && rest[0]==' ') rest.erase(0,1); statusText = rest.empty()?"":rest;
                } 
                else 
                    response.setHeader(name, value);
            }
        }
        response.setStatus(statusCode, statusText.empty()?"OK":statusText);
        response.setBody(body);
        if (response.getHeaders().find("Content-Type") == response.getHeaders().end()) response.setHeader("Content-Type", "text/html");
    } else
    {
        response.setStatus(200, "OK"); response.setHeader("Content-Type", "text/html"); response.setBody(cgiOutput);
    }
}

// Converts the CGI output into an HTTP response once the script ends.
void epollManager::finalizeCgiResponse(int clientFd)
{
    ClientConnection &conn = _clientConnections[clientFd];
    conn.keepAlive = false;
    
    // Clear fds
    if (conn.cgiInFd != -1) {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, conn.cgiInFd, NULL);
        close(conn.cgiInFd);
        _cgiInToClient.erase(conn.cgiInFd);
        conn.cgiInFd = -1;
    }
    
    if (conn.cgiOutFd != -1) {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, conn.cgiOutFd, NULL);
        close(conn.cgiOutFd);
        _cgiOutToClient.erase(conn.cgiOutFd);
        conn.cgiOutFd = -1;
    }

    // Reap child non blocking
    if (conn.cgiPid > 0) {
        int st;
        pid_t result = waitpid(conn.cgiPid, &st, WNOHANG);
        if (result > 0) {
            // Ended process
            LOG("CGI PID=" + toString(conn.cgiPid) + " finished (active left=" + toString(_activeCgiCount) + ")");
        }
        else if (result == 0) {
            // Process still running -> kill it
            kill(conn.cgiPid, SIGKILL);
            waitpid(conn.cgiPid, &st, 0);
        }
        conn.cgiPid = -1;
    }

    // Decrement cgi count if running
    if (conn.cgiRunning) {
        if (_activeCgiCount > 0) {
            _activeCgiCount--;
        }
        conn.cgiRunning = false;
    }

    if (conn.cgiOutBuffer.empty()) {
        queueErrorResponse(clientFd, 502, "Bad Gateway");
        return;
    }
    
    // build response from CGI output
    Response resp; 
    parseCgiOutputToResponse(conn.cgiOutBuffer, resp);
    if (conn.keepAlive) {
        resp.setHeader("Connection", "keep-alive");
        resp.setHeader("Keep-Alive", "timeout=5, max=100");
    } else {
        resp.setHeader("Connection", "close");
    }
    attachSessionCookie(resp, conn);
    std::string out = resp.getResponse();
    conn.outBuffer = out;
    conn.outOffset = 0;
    conn.hasResponse = true;
    armWriteEvent(clientFd, true);
    conn.cgiOutBuffer.clear();
}
