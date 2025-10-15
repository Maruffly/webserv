#include "../include/Webserv.hpp"
#include "network/Server.hpp"
#include "config/ParseConfig.hpp"
#include "network/epollManager.hpp"
#include <csignal>

namespace 
{
    epollManager* g_activeLoop = NULL;

    void handleSignal(int)
    {
        if (g_activeLoop)
            g_activeLoop->requestStop();
    }

    void destroyServers(std::vector<Server*>& servers)
    {
        for (size_t i = 0; i < servers.size(); ++i) 
            delete servers[i];
        servers.clear();
    }

    void selectConfiguration(int argc, char** argv, std::string& configPath)
    {
        std::string configLabel;
        if (argc < 2) 
        {
            configPath = "./config/default.conf";
            configLabel = "default configuration";
        } 
        else 
        {
            configPath = argv[1];
            std::string filename = configPath;
            size_t sep = filename.find_last_of("/\\");
            if (sep != std::string::npos)
                filename = filename.substr(sep + 1);
            size_t dot = filename.rfind('.');
            if (dot != std::string::npos)
                filename = filename.substr(0, dot);
            if (!filename.empty())
                configLabel = filename + " configuration";
            else
                configLabel = "custom configuration";
        }
        LOG("Using " + configLabel + ": " + configPath);
    }
}

void groupHostPort(std::vector<ServerConfig> &serverConfigs, std::map<std::string, std::vector<ServerConfig> > &groups) {
    for (size_t i = 0; i < serverConfigs.size(); ++i) {
            const std::vector<std::string>& listens = serverConfigs[i].getListen();
            if (!listens.empty()) 
            {
                for (size_t j = 0; j < listens.size(); ++j) {
                    // listens[j] is normalized in the form host:port
                    std::string key = listens[j];
                    // Clone the configuration and force host/port for this specific listen
                    ServerConfig clone = serverConfigs[i];
                    size_t colon = key.find(':');
                    std::string host = key.substr(0, colon);
                    int port = std::atoi(key.substr(colon + 1).c_str());
                    clone.setHost(host);
                    clone.setPort(port);
                    groups[key].push_back(clone);
                }
            } 
            else 
            {
                std::string key = serverConfigs[i].getHost() + std::string(":") + toString(serverConfigs[i].getPort());
                groups[key].push_back(serverConfigs[i]);
            }
        }
}

int createGroupSocket(std::vector<Server*> &servers, std::map<std::string, std::vector<ServerConfig> > &groups,
    std::vector< std::vector<ServerConfig> > &serverGroups, std::vector<int> &listenFds) 
    {
        servers.reserve(groups.size());
        listenFds.reserve(groups.size());
        serverGroups.reserve(groups.size());

        for (std::map<std::string, std::vector<ServerConfig> >::iterator it = groups.begin(); it != groups.end(); ++it) 
        {
            const std::vector<ServerConfig>& group = it->second;
            if (group.empty())
                continue;
            try 
            {
                // Create a server only for the first one (bind + listen)
                Server* srv = new Server(group[0]);
                servers.push_back(srv);
                listenFds.push_back(srv->getListeningSocket());
                serverGroups.push_back(group);
            } catch (const std::exception& e) 
            {
                ERROR("Failed to create socket for " + it->first + ": " + std::string(e.what()));
            }
        }
        if (listenFds.empty()) 
        {
            ERROR("No listening socket created");
            destroyServers(servers);
            return 1;
        }
        return 0;
}

int main(int argc, char** argv)
{
    std::vector<Server*> servers;
    try
    {
        std::string configPath;
        ParseConfig parser;
        std::map<std::string, std::vector<ServerConfig> > groups;

        selectConfiguration(argc, argv, configPath);
        std::vector<ServerConfig> serverConfigs = parser.parse(configPath);

        if (serverConfigs.empty())
        {
            ERROR("No valid server configuration found in " + configPath);
            return 1;
        }
        LOG("Number of server blocks detected: " + toString(serverConfigs.size()));
        // Group servers by host:port pair (supports multiple listens per server)
        groupHostPort(serverConfigs, groups);

        // Create a listening socket per group (first defined = default server)
        std::vector< std::vector<ServerConfig> > serverGroups;
        std::vector<int> listenFds;
        if (createGroupSocket(servers, groups, serverGroups, listenFds))
            return 1;
        // single epoll loop
        epollManager loop(listenFds, serverGroups);
        g_activeLoop = &loop;
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        loop.run();
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
        g_activeLoop = NULL;

        destroyServers(servers);
    }
    catch (const std::exception& e)
    {
        destroyServers(servers);
        ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }
    return 0;
}
