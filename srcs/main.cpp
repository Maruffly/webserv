#include "../include/Webserv.hpp"
#include "network/Server.hpp"
#include "config/ParseConfig.hpp"
#include "network/epollManager.hpp"



int main(int argc, char** argv)
{
    try 
    {
        std::string configPath;
        if (argc < 2) {
            configPath = "./config/default.conf";
            LOG("Aucun fichier de configuration fourni, utilisation de la configuration par défaut: " + configPath);
        } else {
            configPath = argv[1];
        }

        std::ifstream test(configPath.c_str());
        if (!test.is_open()) {
            if (argc < 2) {
                ERROR("Impossible d'ouvrir le fichier de configuration par défaut: " + configPath);
            } else {
                ERROR("Impossible d'ouvrir le fichier de configuration: " + configPath);
            }
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

        // Grouper les serveurs par couple host:port (supporte plusieurs listen par server)
        std::map<std::string, std::vector<ServerConfig> > groups;
        for (size_t i = 0; i < serverConfigs.size(); ++i) {
            const std::vector<std::string>& listens = serverConfigs[i].getListen();
            if (!listens.empty()) {
                for (size_t j = 0; j < listens.size(); ++j) {
                    // listens[j] est normalisé sous forme host:port
                    std::string key = listens[j];
                    // cloner la config et forcer host/port pour ce listen précis
                    ServerConfig clone = serverConfigs[i];
                    size_t colon = key.find(':');
                    std::string host = key.substr(0, colon);
                    int port = std::atoi(key.substr(colon + 1).c_str());
                    clone.setHost(host);
                    clone.setPort(port);
                    groups[key].push_back(clone);
                }
            } else {
                std::string key = serverConfigs[i].getHost() + std::string(":") + toString(serverConfigs[i].getPort());
                groups[key].push_back(serverConfigs[i]);
            }
        }

        // Creer un socket d'ecoute par groupe (serveur par defaut = premier defini)
        std::vector<Server*> servers; servers.reserve(groups.size());
        std::vector<int> listenFds; listenFds.reserve(groups.size());
        std::vector< std::vector<ServerConfig> > serverGroups; serverGroups.reserve(groups.size());

        for (std::map<std::string, std::vector<ServerConfig> >::iterator it = groups.begin(); it != groups.end(); ++it) {
            const std::vector<ServerConfig>& group = it->second;
            if (group.empty()) continue;
            try {
                // Creer un Server seulement pour le premier (bind + listen)
                Server* srv = new Server(const_cast<ServerConfig&>(group[0]));
                servers.push_back(srv);
                listenFds.push_back(srv->getSocket());
                serverGroups.push_back(group);
                INFO("Groupe ecoute sur " + group[0].getHost() + ":" + toString(group[0].getPort()) + " avec " + toString(group.size()) + " vhost(s)");
            } catch (const std::exception& e) {
                ERROR("Echec creation socket pour groupe " + it->first + ": " + std::string(e.what()));
            }
        }

        if (listenFds.empty()) { ERROR("Aucun socket d'ecoute cree"); return 1; }

        // boucle epoll unique
        epollManager loop(listenFds, serverGroups);
        loop.run();

        for (size_t i = 0; i < servers.size(); ++i) delete servers[i];

    } 
    catch (const std::exception& e) 
    {
        ERROR("Erreur fatale: " + std::string(e.what()));
        return 1;
    }
    return 0;
}
