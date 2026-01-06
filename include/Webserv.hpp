#pragma once

#define GREEN "\033[32m"
#define ORANGE "\033[38;5;208m"
#define RED "\033[31m"
#define RESET "\033[0m"

// C++ standard library
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <set>
#include <vector>

// C / POSIX headers
#include <dirent.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <unistd.h>

// sockets - network programming
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>


// const
#define SUCCESS 0
#define FAILURE -1
//#define DEFAULT_PORT 8080
//#define DEFAULT_HOST "127.0.0.1"
#define BACKLOG 256 // connections waiting in queue
#define BUFFER_SIZE 1024
#define MAX_EVENTS 64
#define MAX_REQUEST_SIZE 524288000
#define MAX_CLIENTS 512 
#define MAX_CGI_PROCESS 500
#define CONNECTION_TIMEOUT 30
#define READ_TIMEOUT 12
#define KEEP_ALIVE_TIMEOUT 10 
#define CLEANUP_INTERVAL 5
#define CGI_TIMEOUT 10
#define SESSION_MAX_IDLE 300

template <typename T>
std::string toString(const T &value) 
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

inline void LOG(const std::string& msg) { std::cout << GREEN << "[LOG]" << RESET << " " << msg << std::endl; }

inline void INFO(const std::string& msg) { std::cout << ORANGE << "[INF]" << RESET << " " << msg << std::endl; }

inline void ERROR(const std::string& msg) { std::cerr << RED << "[ERR]" << RESET << " " << msg << std::endl; }

inline void ERROR_SYS(const std::string& msg)
{
    std::cerr << RED << "[ERR]" << RESET << " " << msg << " (" << strerror(errno) << ")" << std::endl;
}
