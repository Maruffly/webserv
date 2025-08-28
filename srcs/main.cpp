#include "../include/Webserv.hpp"
#include "network/Server.hpp"

int main() {
    std::cout << "🚀 Starting webserv..." << std::endl;

    try 
    {
        // creates server listening on localhost:8080
        Server server(8080, "127.0.0.1");
        server.run();
        
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "💥 Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}