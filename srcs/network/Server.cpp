#include "../../include/Webserv.hpp"
#include "Server.hpp"

// Initializes the listening socket according to the provided configuration.
Server::Server(const ServerConfig& config) : _listeningSocket(-1), _port(config.getPort()), _host(config.getHost()), _config(config)
{
    createSocket();
    setSocketOptions();
    bindSocket();
    startListening();
}


// Closes the listening socket when the server instance is destroyed.
Server::~Server()
{
    if (_listeningSocket != -1) {
        closeSocketIfOpen();
        LOG("Server socket closed");
    }
}


// Creates the listening socket file descriptor.
void Server::createSocket()
{
    _listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_listeningSocket == -1) {
        throw std::runtime_error("Failed to create socket");
    }
}


// Applies common socket options before binding the socket.
void Server::setSocketOptions()
{
    int opt = 1;
    if (setsockopt(_listeningSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        throwSocketError("Failed to set socket options (SO_REUSEADDR)");
    }
}


// Resolves the configured host and binds the listening socket.
void Server::bindSocket()
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    const char* hostPtr = _host.empty() ? NULL : _host.c_str();
    std::string service = toString(_port);
    struct addrinfo* results = NULL;

    int ret = getaddrinfo(hostPtr, service.c_str(), &hints, &results);
    if (ret != 0) {
        throwSocketError(std::string("getaddrinfo failed: ") + gai_strerror(ret));
    }
    if (results == NULL) {
        throwSocketError("getaddrinfo returned no address");
    }

    bool bound = false;
    for (struct addrinfo* entry = results; entry != NULL && !bound; entry = entry->ai_next) {
        if (bind(_listeningSocket, entry->ai_addr, entry->ai_addrlen) == 0) {
            bound = true;
        }
    }

    freeaddrinfo(results);

    if (!bound) {
        throwSocketError("Bind failed");
    }
}


// Places the listening socket in non-blocking mode and starts listening.
void Server::startListening()
{
    int flags = fcntl(_listeningSocket, F_GETFL, 0);
    if (flags != -1) {
        fcntl(_listeningSocket, F_SETFL, flags | O_NONBLOCK);
    }

    if (listen(_listeningSocket, BACKLOG) < 0) {
        throwSocketError("Listen failed");
    }
}


// Returns the listening socket file descriptor.
int Server::getListeningSocket() const
{
    return _listeningSocket;
}

// Returns the configured listening port.
int Server::getPort() const
{
    return _port;
}


// Returns the configured listening host.
const std::string& Server::getHost() const
{
    return _host;
}


// Closes the listening socket without logging if it is currently open.
void Server::closeSocketIfOpen()
{
    if (_listeningSocket != -1) {
        close(_listeningSocket);
        _listeningSocket = -1;
    }
}


// Ensures the socket is closed before throwing a runtime error.
void Server::throwSocketError(const std::string& message)
{
    closeSocketIfOpen();
    throw std::runtime_error(message);
}
