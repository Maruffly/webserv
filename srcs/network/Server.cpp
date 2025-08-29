#include "Server.hpp"
#include <iostream>

Server::Server(int port, const std::string& host) : _serverSocket(-1), _port(port), _host(host) 
{
	// convert int into string
	std::ostringstream portStr;
	portStr << _port;
	
	LOG("Initializing server on " + _host + ":" + portStr.str());
	
	createSocket();
	setSocketOptions();
	bindSocket();
	startListening();
	
	INFO("Server ready and listening on " + _host + ":" + portStr.str());
}


Server::~Server() 
{
	if (_serverSocket != -1) {
		close(_serverSocket);
		LOG("Server socket closed");
	}
}


void Server::createSocket() 
{
	_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (_serverSocket == -1) {
		throw std::runtime_error("Failed to create socket");
	}
	LOG("Socket created successfully");
}


void Server::setSocketOptions() 
{
	int opt = 1;
	if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		close(_serverSocket);
		throw std::runtime_error("Failed to set socket options (SO_REUSEADDR)");
	}
	LOG("Socket options set (SO_REUSEADDR)");
}


void Server::bindSocket() 
{
	struct sockaddr_in serverAddress;
	
	// Configuration de l'adresse
	std::memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(_port);
	
	// Conversion de l'adresse IP
	if (inet_pton(AF_INET, _host.c_str(), &serverAddress.sin_addr) <= 0) {
		close(_serverSocket);
		throw std::runtime_error("Invalid address or address not supported");
	}
	
	// Liaison
	if (bind(_serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
		close(_serverSocket);
		throw std::runtime_error("Bind failed");
	}
	LOG("Socket bound to " + _host + ":" + toString(_port));
}


void Server::startListening() 
{
	if (listen(_serverSocket, BACKLOG) < 0) {
		close(_serverSocket);
		throw std::runtime_error("Listen failed");
	}
	LOG("Socket is now listening");
}


// Getters
int Server::getSocket() const { return _serverSocket; }
int Server::getPort() const { return _port; }
std::string Server::getHost() const { return _host; }



void Server::run() 
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLen = sizeof(clientAddress);
    char buffer[BUFFER_SIZE];
    
    INFO("Waiting for connections...");
    
    while (true) 
	{
        int clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddress, &clientAddrLen);
        if (clientSocket < 0) 
		{
            ERROR("Accept failed");
            continue;
        }
        
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);
        int clientPort = ntohs(clientAddress.sin_port);
        
        LOG("New connection accepted from " + std::string(clientIP) + ":" + toString(clientPort));
        
        // reading request
        std::string requestData;
        ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytesRead > 0) 
		{
            buffer[bytesRead] = '\0';
            requestData = buffer;
            
            std::cout << "📨 Raw request received:" << std::endl;
            std::cout << "------------------------" << std::endl;
            std::cout << requestData << std::endl;
            std::cout << "------------------------" << std::endl;
            
            // parse request
            Request request(requestData);
            if (request.isComplete()) 
			{
                request.print(); // print full parsing
                
                // print parsed infos
                std::cout << "🔍 Parsed information:" << std::endl;
                std::cout << "Méthode: [" << request.getMethod() << "]" << std::endl;
                std::cout << "Chemin: [" << request.getUri() << "]" << std::endl;
                std::cout << "Version: [" << request.getVersion() << "]" << std::endl;
                std::cout << "Host: [" << request.getHeader("host") << "]" << std::endl;
                
                // print headers
                std::string userAgent = request.getHeader("user-agent");
                if (!userAgent.empty()) 
                    std::cout << "User-Agent: [" << userAgent << "]" << std::endl;
                
                std::string contentType = request.getHeader("content-type");
                if (!contentType.empty())
                    std::cout << "Content-Type: [" << contentType << "]" << std::endl;
                
                std::cout << "------------------------" << std::endl;
                
            } 
			else 
                LOG("Incomplete request received");
            
            // http response
            std::string response = "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: text/html\r\n";
            response += "Content-Length: 25\r\n";
            response += "Connection: close\r\n";
            response += "\r\n";
            response += "<h1>Hello from webserv!</h1>";
            
            send(clientSocket, response.c_str(), response.length(), 0);
            LOG("Response sent to client");
            
        } 
		else if (bytesRead == 0) 
            LOG("Client disconnected");
		else 
            ERROR("recv failed");
        
        close(clientSocket);
        LOG("Connection closed");
    }
}
