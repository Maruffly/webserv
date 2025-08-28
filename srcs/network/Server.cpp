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


void Server::run() 
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLen = sizeof(clientAddress);
    
    INFO("Waiting for connections...");
    
    while (true) 
	{
        // Accepte une nouvelle connexion
        int clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddress, &clientAddrLen);
        if (clientSocket < 0) 
		{
            ERROR("Accept failed");
            continue; // Continue même en cas d'erreur
        }
        
        // Affiche les informations du client
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);
        LOG("New connection accepted from " + std::string(clientIP) + ":" + toString(ntohs(clientAddress.sin_port)));
        
        // Ferme la connexion client (pour l'instant)
        close(clientSocket);
        LOG("Connection closed");
    }
}

// Getters
int Server::getSocket() const { return _serverSocket; }
int Server::getPort() const { return _port; }
std::string Server::getHost() const { return _host; }