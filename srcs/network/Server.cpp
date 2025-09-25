#include "Server.hpp"
#include "epollManager.hpp"

// Server::Server(int port, const std::string& host) : _serverSocket(-1), _port(port), _host(host) 
// {
// 	// convert int into string
// 	std::ostringstream portStr;
// 	portStr << _port;
	
// 	LOG("Initializing server on " + _host + ":" + portStr.str());
	
// 	createSocket();
// 	setSocketOptions();
// 	bindSocket();
// 	startListening();
	
// 	INFO("Server ready and listening on " + _host + ":" + portStr.str());
// }


Server::Server(ServerConfig& config) : _serverSocket(-1), _config(config)
{
    _port = config.getPort();
    _host = config.getHost();
    
    std::ostringstream portStr;
    portStr << _port;
    
    LOG("Initializing server from config: " + _host + ":" + portStr.str());
    
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
	// Met le socket d'écoute en non-bloquant pour respecter la boucle I/O non bloquante
	int flags = fcntl(_serverSocket, F_GETFL, 0);
	if (flags != -1) {
		fcntl(_serverSocket, F_SETFL, flags | O_NONBLOCK);
	}

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
    // Dans l'architecture actuelle, la boucle epoll unifiée est lancée depuis main
    // pour gérer tous les sockets d'écoute simultanément. Cette méthode est donc
    // conservée pour compatibilité mais ne lance plus de boucle propre.
    INFO("Server::run inutilise: la boucle epoll est lancee depuis main");
}
