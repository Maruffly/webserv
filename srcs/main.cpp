#include "../include/Webserv.hpp"
#include "network/Server.hpp"
#include "config/ParseConfig.hpp"
#include "network/epollManager.hpp"



int main(int argc, char** argv)
{
    try 
    {
        if (argc < 2) 
        {
            ERROR("Aucun fichier de configuration fourni. Utilisation: ./webserv linux.conf");
            return 1;
        }
        std::string configPath = argv[1];
        if (configPath != "linux.conf")
        {
            ERROR("Le fichier de configuration attendu est 'linux.conf'. Utilisation: ./webserv linux.conf");
            return 1;
        }

        LOG("Chargement du fichier de configuration: " + configPath);

        ParseConfig parser;
        std::vector<ServerConfig> serverConfigs = parser.parse(configPath);

        if (serverConfigs.empty()) 
        {
            ERROR("Aucune configuration de serveur valide trouvée dans " + configPath);
            return 1;
        }

        LOG("Configurations detectees: " + toString(serverConfigs.size()));

        // Créer tous les sockets d'écoute et les conserver vivants
        std::vector<Server*> servers;
        std::vector<int> listenFds;
        servers.reserve(serverConfigs.size());
        listenFds.reserve(serverConfigs.size());

        for (size_t i = 0; i < serverConfigs.size(); ++i) 
        {
            try 
            {
                Server* srv = new Server(serverConfigs[i]);
                servers.push_back(srv);
                listenFds.push_back(srv->getSocket());
                INFO("Serveur " + toString(i+1) + " pret sur " + serverConfigs[i].getHost() + ":" + toString(serverConfigs[i].getPort()));
            } catch (const std::exception& e) 
            {
                ERROR("Echec creation serveur " + toString(i+1) + ": " + std::string(e.what()));
            }
        }

        if (listenFds.empty()) {
            ERROR("Aucun socket d'ecoute cree");
            return 1;
        }

        // single epoll loop for all I/O
        epollManager loop(listenFds, serverConfigs);
        loop.run();

        // cleaning
        for (size_t i = 0; i < servers.size(); ++i) delete servers[i];

    } 
    catch (const std::exception& e) 
    {
        ERROR("Erreur fatale: " + std::string(e.what()));
        return 1;
    }
    return 0;
}
