#pragma once

#define GREEN = \033[32m
#define RED = \033[31m
#define BLINK = \033[5m
#define RESET = \033[0m

// system
#include <map>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <algorithm>

// sockets - network programming
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// const
#define SUCCESS 0
#define FAILURE -1
#define DEFAULT_PORT 8080
#define DEFAULT_HOST "127.0.0.1"
#define BACKLOG 10 // connections waiting in queue
#define BUFFER_SIZE 1024
#define MAX_EVENTS 64
#define MAX_REQUEST_SIZE 524288000
#define MAX_CLIENTS 100 
#define CONNECTION_TIMEOUT 30
#define READ_TIMEOUT 15
#define KEEP_ALIVE_TIMEOUT 10 
#define CLEANUP_INTERVAL 5

template <typename T>
std::string toString(const T &value) 
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

inline void LOG(const std::string& msg) 
{
    std::cout << "📝 " << msg << std::endl;
}

inline void ERROR(const std::string& msg) 
{
    std::cerr << "❌ ERROR: " << msg << " (" << strerror(errno) << ")" << std::endl;
}

inline void INFO(const std::string& msg) 
{
    std::cout << "ℹ️  " << msg << std::endl;
}