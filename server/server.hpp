#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>

class Server {
public:
	Server();
	~Server();
	Server(const Server& src);
	Server& operator=(const Server& src);
	int setupServer(void);

private:
	
};