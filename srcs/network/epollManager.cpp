#include "epollManager.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/ServerConfig.hpp"
#include "../utils/Utils.hpp"
#include "../cgi/CgiHandler.hpp"

void epollManager::cleanupIdleConnections() {
    time_t now = time(NULL);
    
    // Nettoyer seulement toutes les 5 secondes
    if (now - _lastCleanup < CLEANUP_INTERVAL) {
        return;
    }
    _lastCleanup = now;
    
    LOG("Checking for idle connections...");
    int closedCount = 0;
    
    std::map<int, std::string>::iterator bufIt = _clientBuffers.begin();
    while (bufIt != _clientBuffers.end()) {
        int clientFd = bufIt->first;
        
        // Vérifier si le client existe dans les connexions
        std::map<int, ClientConnection>::iterator connIt = _clientConnections.find(clientFd);
        if (connIt != _clientConnections.end()) {
            double idleTime = difftime(now, connIt->second.lastActivity);
            
            if (idleTime > CONNECTION_TIMEOUT) {
                LOG("Closing idle connection (fd: " + toString(clientFd) + 
                    ", idle: " + toString(idleTime) + "s)");
                closeClient(clientFd);
                _clientConnections.erase(connIt);
                std::map<int, std::string>::iterator toErase = bufIt++;
                _clientBuffers.erase(toErase);
                closedCount++;
                continue;
            }
        }
        bufIt++;
    }
    
    if (closedCount > 0) {
        LOG("Closed " + toString(closedCount) + " idle connections");
    }
}

bool epollManager::isCgiRequest(const std::string& uri) const {
	// Get the location block for this URI
	const LocationConfig* location = findLocationConfig(uri);
	if (!location) return false;

	// Check if this location has CGI configured
	return !location->getCgiPass().empty();
}

epollManager::epollManager(int serverSocket, ServerConfig& config) 
	: _serverSocket(serverSocket), _epollFd(-1), _config(config)
{
	_lastCleanup = time(NULL);
	_epollFd = epoll_create1(0);
	if (_epollFd == -1)
		throw std::runtime_error("epoll_create1 failed");
	LOG("epoll instance created");

	struct epoll_event event;
	event.events = EPOLLIN;
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
	std::map<int, std::string>::iterator bufIt;
    for (bufIt = _clientBuffers.begin(); bufIt != _clientBuffers.end(); ++bufIt) {
        close(bufIt->first);
    }
    _clientBuffers.clear();
    _clientConnections.clear();
}


bool epollManager::isMethodAllowed(const std::string& method, const std::string& uri) const
{
	const LocationConfig* location = findLocationConfig(uri);
	if (location) {
		const std::vector<std::string>& allowedMethods = location->getAllowedMethods();
		if (!allowedMethods.empty()) {
			return std::find(allowedMethods.begin(), allowedMethods.end(), method) != allowedMethods.end();
		}
	}
	return true; // Par défaut, toutes les méthodes sont autorisées
}

const LocationConfig* epollManager::findLocationConfig(const std::string& uri) const
{
	const std::vector<LocationConfig>& locations = _config.getLocations();
	const LocationConfig* bestMatch = NULL;
	
	for (size_t i = 0; i < locations.size(); ++i) {
		const LocationConfig& loc = locations[i];
		if (uri.find(loc.getPath()) == 0) {
			if (!bestMatch || loc.getPath().length() > bestMatch->getPath().length()) {
				bestMatch = &loc;
			}
		}
	}
	return bestMatch;
}

// Modifier createResponseForRequest pour utiliser la config
// epollManager.cpp - IMPLÉMENTATION COMPLÈTE
Response epollManager::createResponseForRequest(const Request& request) {
	Response response;
	
	// 1. Vérifier la version HTTP
	if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") {
		response.setStatus(505, "HTTP Version Not Supported");
		response.setBody(createHtmlResponse("505 HTTP Version Not Supported", 
										  "Unsupported HTTP version: " + request.getVersion()));
		response.setHeader("Content-Type", "text/html");
		return response;
	}

	// 2. Vérifier les méthodes autorisées via config
	std::string method = request.getMethod();
	std::string uri = request.getUri();
	
	if (!isMethodAllowed(method, uri)) {
		response.setStatus(405, "Method Not Allowed");
		
		// Construire le header Allow avec les méthodes autorisées
		const LocationConfig* location = findLocationConfig(uri);
		 if (location && !location->getCgiPass().empty()) {
		std::string extension = getFileExtension(uri);
		std::map<std::string, std::string> cgiConfig = location->getCgiPass();
		
		if (cgiConfig.find(extension) != cgiConfig.end()) {
			std::string interpreter = cgiConfig[extension];
			std::string scriptPath = location->getRoot() + uri;
			
			CgiHandler cgi;
			return cgi.execute(request, scriptPath, interpreter);
		}
	}
		
		response.setBody(createHtmlResponse("405 Method Not Allowed", 
										  "Method " + method + " is not allowed for this resource"));
		response.setHeader("Content-Type", "text/html");
		return response;
	}

	// 3. Vérifier la taille du body pour POST
	if (method == "POST" && request.getBody().length() > _config.getClientMax()) {
		response.setStatus(413, "Request Entity Too Large");
		response.setBody(createHtmlResponse("413 Request Too Large", 
										  "Request body exceeds maximum allowed size of " + 
										  toString(_config.getClientMax()) + " bytes"));
		response.setHeader("Content-Type", "text/html");
		return response;
	}

	// 4. Trouver la configuration de location correspondante
	const LocationConfig* location = findLocationConfig(uri);
	
	// 5. Routing basé sur la configuration
	if (uri == "/" || uri == "/index.html") {
		// Servir la page d'accueil configurée
		std::string root = _config.getRoot();
		std::string indexFiles = _config.getIndex();
		
		// Parse multiple index files (space-separated)
		std::vector<std::string> indexList = ParserUtils::split(indexFiles, ' ');
		bool indexFound = false;
		
		for (size_t i = 0; i < indexList.size() && !indexFound; ++i) {
			std::string indexFile = ParserUtils::trim(indexList[i]);
			if (!indexFile.empty()) {
				std::string filePath = root + "/" + indexFile;
				
				if (fileExists(filePath)) {
					response.setStatus(200, "OK");
					response.setHeader("Content-Type", getContentType(indexFile));
					response.setBody(readFileContent(filePath));
					indexFound = true;
				}
			}
		}
		
		if (!indexFound) {
			response.setStatus(404, "Not Found");
			response.setHeader("Content-Type", "text/html");
			response.setBody(createHtmlResponse("404 Not Found", 
											  "Index file not found: " + indexFiles));
		}
	}
   else if (location && !location->getCgiPass().empty() && 
		 isCgiFile(uri, _config.getLocations())) {
	std::string extension = getFileExtension(uri);
	const std::map<std::string, std::string>& cgiConfig = location->getCgiPass();
	
	// Debug logging
	std::cout << "Processing CGI request for extension: " << extension << std::endl;
	std::cout << "Available CGI handlers: " << std::endl;
	for (std::map<std::string, std::string>::const_iterator it = cgiConfig.begin(); 
		 it != cgiConfig.end(); ++it) {
		std::cout << "  " << it->first << " => " << it->second << std::endl;
	}
	
	// Check for exact match or wildcard
	std::map<std::string, std::string>::const_iterator it = cgiConfig.find(extension);
	if (it == cgiConfig.end()) {
		it = cgiConfig.find(".*"); // Try wildcard match
	}
	
	if (it != cgiConfig.end()) {
		std::string interpreter = it->second;
		std::string scriptPath = location->getRoot() + uri;
		
		std::cout << "Executing CGI with:" << std::endl
				 << "  Interpreter: " << interpreter << std::endl
				 << "  Script path: " << scriptPath << std::endl;
		
		CgiHandler cgi;
		return cgi.execute(request, scriptPath, interpreter);
	} else {
		response.setStatus(400, "Bad Request");
		response.setBody(createHtmlResponse("400 Bad Request", 
					   "No CGI interpreter configured for extension: " + extension));
	}
}
	else {
		// Servir des fichiers statiques
		std::string filePath = _config.getRoot() + uri;
		
		if (fileExists(filePath)) {
			if (isDirectory(filePath)) {
				// Gestion des répertoires
				if (location && location->getAutoindex()) {
					// Autoindex activé
					response.setStatus(200, "OK");
					response.setHeader("Content-Type", "text/html");
					response.setBody(generateDirectoryListing(filePath, uri));
				} else {
					// Autoindex désactivé, chercher index files
					std::string indexFiles = _config.getIndex();
					std::vector<std::string> indexList = ParserUtils::split(indexFiles, ' ');
					bool indexFound = false;
					
					for (size_t i = 0; i < indexList.size() && !indexFound; ++i) {
						std::string indexFile = ParserUtils::trim(indexList[i]);
						if (!indexFile.empty()) {
							std::string indexFilePath = filePath + "/" + indexFile;
							if (fileExists(indexFilePath)) {
								response.setStatus(200, "OK");
								response.setHeader("Content-Type", getContentType(indexFile));
								response.setBody(readFileContent(indexFilePath));
								indexFound = true;
							}
						}
					}
					
					if (!indexFound) {
						response.setStatus(403, "Forbidden");
						response.setHeader("Content-Type", "text/html");
						response.setBody(createHtmlResponse("403 Forbidden", 
														  "Directory listing forbidden"));
					}
				}
			} else {
				// Fichier régulier
				response.setStatus(200, "OK");
				response.setHeader("Content-Type", getContentType(uri));
				response.setBody(readFileContent(filePath));
			}
		} else {
			// Fichier non trouvé - utiliser la page d'erreur configurée si disponible
			response.setStatus(404, "Not Found");
			response.setHeader("Content-Type", "text/html");
			response.setBody(createHtmlResponse("404 Not Found", 
											  "The requested URL " + uri + " was not found"));
		}
	}

	// 6. Headers communs à toutes les réponses
	response.setHeader("Connection", "close");
	response.setHeader("Server", "webserv/1.0");
	response.setHeader("Date", getCurrentDate());

	return response;
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
	if (_clientBuffers.size() >= MAX_CLIENTS) {
		LOG("Too many connections, rejecting new client");
		close(clientSocket);
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

	ClientConnection newConn;
    newConn.fd = clientSocket;
    newConn.lastActivity = time(NULL);
    newConn.isReading = false;
    
    _clientConnections[clientSocket] = newConn;
    _clientBuffers[clientSocket] = "";
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
	 std::map<int, ClientConnection>::iterator it = _clientConnections.find(clientFd);
    if (it != _clientConnections.end()) {
        it->second.lastActivity = time(NULL);
        it->second.isReading = true;
    }
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
					else{
						LOG("Response sent to client " + toString(clientFd));
						if (!isCgiRequest(request.getUri())) {
							closeClient(clientFd);
						}
					}
				} 
				else 
				{
					LOG("Incomplete request from client " + toString(clientFd));
					sendErrorResponse(clientFd, 400, "Bad Request");
					closeClient(clientFd);
				}
			} 
			catch (const std::exception& e) 
			{
				ERROR("Request parsing failed: " + std::string(e.what()));
				sendErrorResponse(clientFd, 400, "Bad Request");
				closeClient(clientFd);
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
	if (it != _clientConnections.end())
        it->second.isReading = false;
}


void epollManager::closeClient(int clientFd) 
{
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
	close(clientFd);
	_clientBuffers.erase(clientFd);
	LOG("Connection closed for client " + toString(clientFd));
}

/* void cleanupIdleConnections() {
    time_t now = time(NULL);
    
    for (auto it = _clientConnections.begin(); it != _clientConnections.end(); ) {
        ClientConnection& client = it->second;
        double idle = difftime(now, client.lastActivity);
        
        if (client.buffer.empty() && idle > CONNECTION_TIMEOUT) {
            // Connexion établie mais aucune donnée reçue
            closeClient(client.fd);
            it = _clientConnections.erase(it);
        }
        else if (!client.buffer.empty() && idle > READ_TIMEOUT) {
            // Données reçues mais requête incomplète
            closeClient(client.fd);
            it = _clientConnections.erase(it);
        }
        else if (idle > KEEP_ALIVE_TIMEOUT) {
            // Connexion keep-alive inactive
            closeClient(client.fd);
            it = _clientConnections.erase(it);
        }
        else {
            ++it;
        }
    }
}
void checkTimeouts() {
    time_t now = time(NULL);
    for (auto it = _clientConnections.begin(); it != _clientConnections.end(); ) {
        if (now - it->second.lastActivity > CONNECTION_TIMEOUT) {
            LOG("Closing idle connection: " + toString(it->first));
            closeClient(it->first);
            it = _clientConnections.erase(it);
        } else {
            ++it;
        }
    }
} */

void epollManager::run() 
{
	struct epoll_event events[MAX_EVENTS];
	
	INFO("Starting epoll event loop...");

	while (true) {
		int numEvents = epoll_wait(_epollFd, events, MAX_EVENTS, -1);
		if (numEvents == -1) {
			if (errno == EINTR) continue;
			ERROR("epoll_wait failed");
			break;
		}
		cleanupIdleConnections();
		for (int i = 0; i < numEvents; ++i) {
			if (events[i].data.fd == _serverSocket) 
				handleNewConnection();
			else
				handleClientData(events[i].data.fd);
		}
	}
}
