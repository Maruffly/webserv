#include "../../include/Webserv.hpp"

class ClientConnection {
public:
    int fd;
    std::string buffer;
    time_t lastActivity; // last activity timestamp
    bool isReading;      // connexion state

	
};