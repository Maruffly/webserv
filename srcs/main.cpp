#include "../include/Webserv.hpp"
#include "network/Server.hpp"
#include "config/ParseConfig.hpp"



int main(int argc, char** argv)
{
    try {
		ParseConfig parser;
		std::vector<ServerConfig> servers = parser.parse("linux.conf");
		
		for (size_t i = 0; i < servers.size(); ++i) {
			std::cout << "\nServer #" << i + 1 << ":" << std::endl;
			servers[i].printConfig();
		}
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
    try {
        std::string configPath = (argc > 1) ? argv[1] : "linux.conf";
        
        LOG("Loading config from: " + configPath);
        
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
