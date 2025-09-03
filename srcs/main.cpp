#include "../include/Webserv.hpp"
#include "network/Server.hpp"
#include "config/ParseConfig.hpp"


// int main() {
//     std::cout << "🚀 Starting webserv..." << std::endl;

//     try 
//     {
//         // creates server listening on localhost:8080
//         Server server(8080, "127.0.0.1");
//         server.run();
        
//     } 
//     catch (const std::exception& e) 
//     {
//         std::cerr << "💥 Fatal error: " << e.what() << std::endl;
//         return 1;
//     }
    
//     return 0;
// }


// ./webserv -> browser : http://localhost:8080



int main(int argc, char** argv)
{
    try {
        std::string configPath = (argc > 1) ? argv[1] : "config/default.conf";
        
        LOG("Loading configuration from: " + configPath);
        
        // Parser la configuration
        ParseConfig parser;
        std::vector<ServerConfig> serverConfigs = parser.parse(configPath);
        
        if (serverConfigs.empty()) {
            ERROR("No valid server configuration found in " + configPath);
            return 1;
        }
        
        LOG("Found " + toString(serverConfigs.size()) + " server configuration(s)");
        
        // Créer et lancer les serveurs
        for (size_t i = 0; i < serverConfigs.size(); ++i) {
            try {
                Server server(serverConfigs[i]);
                INFO("Server " + toString(i+1) + " created - listening on " + 
                     serverConfigs[i].getHost() + ":" + toString(serverConfigs[i].getPort()));
                
                // Pour l'instant, on lance le serveur (bloquant)
                // Plus tard: gestion non-bloquante multiple
                server.run();
                
            } catch (const std::exception& e) {
                ERROR("Failed to create server " + toString(i+1) + ": " + std::string(e.what()));
            }
        }
        
    } catch (const std::exception& e) {
        ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}