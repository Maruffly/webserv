#include "../../include/Webserv.hpp"

#pragma once

class ClientConnection {
public:
    int fd;
    std::string buffer;
    time_t lastActivity; // last activity timestamp
    bool isReading;      // connexion state

	
};