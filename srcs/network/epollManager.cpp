#include "epollManager.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"


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


std::string getContentType(const std::string& uri) 
{
    size_t dotPos = uri.find_last_of('.');
    if (dotPos == std::string::npos)
        return "text/html";
    
    std::string extension = uri.substr(dotPos + 1);
    
    if (extension == "html" || extension == "htm") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "application/javascript";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "png") return "image/png";
    if (extension == "gif") return "image/gif";
    if (extension == "json") return "application/json";
    
    return "text/plain";
}


std::string getCurrentDate() {
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    char buf[100];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf);
}


std::string createHtmlResponse(const std::string& title, const std::string& content) {
    return "<!DOCTYPE html><html><head><title>" + title + 
           "</title><style>body{font-family: Arial, sans-serif; margin: 40px;}</style></head><body><h1>" + 
           title + "</h1><p>" + content + "</p></body></html>";
}


Response PollManager::createResponseForRequest(const Request& request) 
{
    Response response;
    
    if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") 
	{
        response.setStatus(505, "HTTP Version Not Supported");
        response.setBody("<h1>505 HTTP Version Not Supported</h1>");
        response.setHeader("Content-Type", "text/html");
        return response;
    }

    if (request.getMethod() != "GET" && request.getMethod() != "POST" && request.getMethod() != "DELETE") 
	{
        response.setStatus(405, "Method Not Allowed");
        response.setHeader("Allow", "GET, POST, DELETE");
        response.setBody("<h1>405 Method Not Allowed</h1>");
        response.setHeader("Content-Type", "text/html");
        return response;
    }

    std::string uri = request.getUri();
    
    if (uri == "/" || uri == "/index.html") 
	{
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><head><title>Home</title></head><body><h1>Welcome to webserv!</h1><p>URI: " + uri + "</p></body></html>");
    }
    else if (uri == "/hello") 
	{
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setBody("<h1>Hello World!</h1><p>This is a dynamic page</p>");
    }
    else if (uri == "/redirect") 
	{
        response.setStatus(302, "Found");
        response.setHeader("Location", "/hello");
        response.setBody("<h1>302 Redirect</h1>");
    }
    else 
	{
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "text/html");
        response.setBody("<html><head><title>404</title></head><body><h1>404 Not Found</h1><p>The requested URL " + uri + " was not found</p></body></html>");
    }

    response.setHeader("Connection", "close");
    response.setHeader("Server", "webserv/1.0");
    response.setHeader("Date", getCurrentDate());

    return response;
}



void PollManager::handleClientData(int clientFd) 
{
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;

    while ((bytesRead = recv(clientFd, buffer, BUFFER_SIZE - 1, 0)) > 0) 
	{
        buffer[bytesRead] = '\0';
        _clientBuffers[clientFd] += buffer;

        if (_clientBuffers[clientFd].find("\r\n\r\n") != std::string::npos) 
		{
            LOG("Complete request received from client " + toString(clientFd));
            
            Request request(_clientBuffers[clientFd]);
            if (request.isComplete()) 
			{
                request.print();
                
                Response response = createResponseForRequest(request);
                std::string responseStr = response.getResponse();
                send(clientFd, responseStr.c_str(), responseStr.length(), 0);
            }

            _clientBuffers[clientFd].clear();
            closeClient(clientFd);
            return;
        }
    }
}


void PollManager::closeClient(int clientFd) 
{
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
    close(clientFd);
    _clientBuffers.erase(clientFd);
    LOG("Connection closed for client " + toString(clientFd));
}


void PollManager::run() 
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
