#include "epollManager.hpp"


epollManager::epollManager(int serverSocket) : _serverSocket(serverSocket), _epollFd(-1)
{
	_epollFd = epoll_create1(0);
	if (_epollFd == -1)
		throw std::runtime_error("epoll_create1 failed");
	LOG("epoll instance created");

	struct epoll_event	event;
	event.events = EPOLLIN; // monitoring readings
	event.data.fd = _serverSocket;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, _serverSocket, &event) == -1)
	{
		close(_epollFd);
		throw std::runtime_error("Failed to add server socket to epoll");
	}
	LOG("Server socket added to epoll");
}


epollManager::~epollManager()
{
	if (_epollFd != -1)
		close(_epollFd);

	// close clients connections
	for (std::map<int, std::string>::iterator it = _clientBuffers.begin(); it != _clientBuffers.end(); ++it)
		close(it->first);
}


void	epollManager::handleNewConnection()
{
	struct sockaddr_in	clientAddress;
	socklen_t clientAddrLen = sizeof(clientAddress);

	int	clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddress, &clientAddrLen);
	if (clientSocket == -1)
	{
		ERROR("accept failed");
		return;
	}

	int	flags = fcntl(clientSocket, F_GETFL, 0);
	fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

	char clientIP[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);
	int clientPort = ntohs(clientAddress.sin_port);

	LOG("New connection accepted from " + std::string(clientIP) + ":" + toString(clientPort));

	// adding client to epoll
	struct epoll_event	event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = clientSocket;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1)
	{
		ERROR("Failed to add client to epoll");
		close(clientSocket);
		return;
	}

	_clientBuffers[clientSocket] = ""; // init empty buffer
}



Response epollManager::createResponseForRequest(const Request& request) 
{
    Response response;
    
    // Validation de la version HTTP
    if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") 
    {
        response.setStatus(505, "HTTP Version Not Supported");
        response.setBody(createHtmlResponse("505 HTTP Version Not Supported", 
                                          "Unsupported HTTP version: " + request.getVersion()));
        response.setHeader("Content-Type", "text/html");
        return response;
    }

    // Validation de la méthode HTTP (seulement GET pour les fichiers)
    std::string method = request.getMethod();
    if (method != "GET") 
    {
        response.setStatus(405, "Method Not Allowed");
        response.setHeader("Allow", "GET");
        response.setBody(createHtmlResponse("405 Method Not Allowed", 
                                          "Method " + method + " is not allowed for static files"));
        response.setHeader("Content-Type", "text/html");
        return response;
    }

    std::string uri = request.getUri();
    std::string filePath = "./www" + uri; // Base directory

    // Gestion de la racine : sert index.html
    if (uri == "/")
        filePath = "./www/index.html";

    // Vérifie si le chemin est un dossier
    struct stat pathStat;
    if (stat(filePath.c_str(), &pathStat) == 0) 
    {
        if (S_ISDIR(pathStat.st_mode)) 
        {
            // C'EST UN DOSSIER - essaie de servir index.html dans le dossier
            std::string indexFile = filePath;
            if (indexFile[indexFile.length() - 1] != '/') 
                indexFile += "/";
            indexFile += "index.html";
            
            if (response.setFile(indexFile))
                response.setStatus(200, "OK");
            else 
            {
                // Dossier sans index.html - interdit le listing
                response.setStatus(403, "Forbidden");
                response.setHeader("Content-Type", "text/html");
                response.setBody(createHtmlResponse("403 Forbidden", 
                                                  "Directory listing not allowed for: " + uri));
            }
        } 
        else 
        {
            // C'EST UN FICHIER - essaie de le servir
            if (response.setFile(filePath)) {
                response.setStatus(200, "OK");
            } else 
            {
                // Fichier inaccessible (permissions)
                response.setStatus(403, "Forbidden");
                response.setHeader("Content-Type", "text/html");
                response.setBody(createHtmlResponse("403 Forbidden", 
                                                  "Access denied to: " + uri));
            }
        }
    } 
    else 
    {
        // FICHIER/DOSSIER N'EXISTE PAS
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("404 Not Found", 
                                          "The requested resource " + uri + " was not found"));
    }

    // Headers communs à toutes les réponses
    response.setHeader("Connection", "close");
    response.setHeader("Server", "webserv/1.0");
    response.setHeader("Date", getCurrentDate());

    return response;
}


void epollManager::sendErrorResponse(int clientFd, int code, const std::string& message) 
{
    Response response;
    response.setStatus(code, message);
    response.setHeader("Content-Type", "text/html");
    response.setHeader("Connection", "close");
    response.setHeader("Server", "webserv/1.0");
    
    std::string body = createHtmlResponse(toString(code) + " " + message, 
                                        "Error: " + message + "<br>Please try another URL.");
    response.setBody(body);
    
    std::string responseStr = response.getResponse();
    if (send(clientFd, responseStr.c_str(), responseStr.length(), 0) == -1) {
        ERROR("Failed to send error response to client " + toString(clientFd));
    }
}



void epollManager::handleClientData(int clientFd) 
{
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;

    while ((bytesRead = recv(clientFd, buffer, BUFFER_SIZE - 1, 0)) > 0) 
    {
        buffer[bytesRead] = '\0';
        _clientBuffers[clientFd] += buffer;

        // Vérifier la taille du buffer pour éviter les overflow
        if (_clientBuffers[clientFd].length() > MAX_REQUEST_SIZE) 
        {
            LOG("Request too large from client " + toString(clientFd));
            sendErrorResponse(clientFd, 413, "Request Entity Too Large");
            closeClient(clientFd);
            return;
        }

        if (_clientBuffers[clientFd].find("\r\n\r\n") != std::string::npos) 
        {
            LOG("Complete request received from client " + toString(clientFd));
            
            try {
                Request request(_clientBuffers[clientFd]);
                if (request.isComplete()) 
                {
                    request.print();
                    
                    Response response = createResponseForRequest(request);
                    std::string responseStr = response.getResponse();
                    
                    // Gestion d'erreur sur l'envoi
                    if (send(clientFd, responseStr.c_str(), responseStr.length(), 0) == -1) 
                        ERROR("Failed to send response to client " + toString(clientFd));
                    else 
                        LOG("Response sent to client " + toString(clientFd));
                } 
                else 
                {
                    LOG("Incomplete request from client " + toString(clientFd));
                    sendErrorResponse(clientFd, 400, "Bad Request");
                }
            } 
            catch (const std::exception& e) 
            {
                ERROR("Request parsing failed: " + std::string(e.what()));
                sendErrorResponse(clientFd, 400, "Bad Request");
            }

            _clientBuffers[clientFd].clear();
            closeClient(clientFd);
            return;
        }
    }

    if (bytesRead == 0) {
        LOG("Client " + toString(clientFd) + " disconnected");
        closeClient(clientFd);
    } else if (bytesRead == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ERROR("recv failed for client " + toString(clientFd) + ": " + std::string(strerror(errno)));
            closeClient(clientFd);
        }
    }
}


void epollManager::closeClient(int clientFd) 
{
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
    close(clientFd);
    _clientBuffers.erase(clientFd);
    LOG("Connection closed for client " + toString(clientFd));
}


void epollManager::run() 
{
    struct epoll_event events[MAX_EVENTS];
    
    INFO("Starting epoll event loop...");

    while (true) {
        int numEvents = epoll_wait(_epollFd, events, MAX_EVENTS, -1);
        if (numEvents == -1) {
            if (errno == EINTR) continue; // Interrupted system call
            ERROR("epoll_wait failed");
            break;
        }

        for (int i = 0; i < numEvents; ++i) {
            if (events[i].data.fd == _serverSocket) 
                handleNewConnection();
            else
                handleClientData(events[i].data.fd);
        }
    }
}
