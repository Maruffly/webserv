#pragma once

#include "Webserv.hpp"
#include "../http/Response.hpp"
#include "../http/Request.hpp"
#include "../utils/Utils.hpp"
#include "../config/ServerConfig.hpp"
#include "ClientConnection.hpp"

class epollManager
{
    private:
        int _epollFd;
        std::map<int, std::string> _clientBuffers;
        std::map<int, ClientConnection> _clientConnections;
        time_t _lastCleanup;
        bool _running;

        // Multi-listen support
        std::set<int> _listenSockets;                               // all listening fds
        std::map<int, std::vector<ServerConfig> > _serverGroups;    // listen fd -> group of ServerConfig (first is default)
        std::map<int, ServerConfig> _serverForClientFd;             // mapping client fd -> selected ServerConfig

        // CGI pipe fd -> client fd

        std::map<int,int> _cgiOutToClient;
        std::map<int,int> _cgiInToClient;

        std::map<pid_t, int> _pidToClientFd;
        // CGI count
        size_t _activeCgiCount;

        void acceptPendingConnections(int listenFd);
        void readClientData(int clientFd, uint32_t events);
        void flushClientBuffer(int clientFd, uint32_t events);
        void drainCgiOutput(int pipeFd, uint32_t events);
        void feedCgiInput(int pipeFd, uint32_t events);
        void closeClientSocket(int clientFd);
        void removeClientState(int clientFd);
        void queueErrorResponse(int clientFd, int code, const std::string& message);
        inline void sendErrorResponse(int clientFd, int code, const std::string& message) {
            queueErrorResponse(clientFd, code, message);
        }

        // Per-request helpers using selected config
        Response buildResponseForRequest(const Request& request, const ServerConfig& config);
        bool isCgiRequest(const std::string& uri, const ServerConfig& config) const;
        bool isMethodAllowed(const std::string& method, const std::string& uri, const ServerConfig& config) const;
        std::string resolveFilePath(const std::string& uri, const ServerConfig& config) const;
        Response handleDelete(const Request& request, const LocationConfig* location, const ServerConfig& config);
        Response handlePost(const Request& request, const LocationConfig* location, const ServerConfig& config);
        std::string buildAllowHeader(const LocationConfig* location) const;
        const LocationConfig* findLocationConfig(const std::string& uri, const ServerConfig& config) const;
        size_t getEffectiveClientMax(const LocationConfig* location, const ServerConfig& config) const;
        bool validatePostLengthHeader(const Request& request, Response& response, const ServerConfig& config) const;
        bool validatePostBodySize(const Request& request, const LocationConfig* location, const ServerConfig& config, Response& response) const;
        bool handleConfiguredRedirect(const LocationConfig* location, Response& response, const ServerConfig& config) const;
        bool tryServeRootIndex(const std::string& uri, const LocationConfig* location, const ServerConfig& config, Response& response) const;
        bool tryServeResourceFromFilesystem(const std::string& uri, const LocationConfig* location, const ServerConfig& config, Response& response) const;
        void addStandardHeaders(Response& response, const std::string& method) const;
        bool collectClientRequest(int clientFd);
        bool parseClientHeaders(int clientFd);
        bool consumeFixedBody(int clientFd);
        bool consumeChunkedBody(int clientFd);
        bool parseMultipartAndSave(const std::string& body, const std::string& boundary,
                                   const std::string& basePath, const std::string& uri,
                                   size_t& savedCount, bool& anyCreated, std::string& lastSavedPath);
        void handleReadyRequest(int clientFd);
        std::string resolveErrorPagePath(const std::string& candidate, const ServerConfig& config) const;
        bool loadErrorPage(int code, const ServerConfig* config, std::string& body, std::string& contentType) const;
        void buildErrorResponse(Response& response, int code, const std::string& message, const ServerConfig* config) const;

        void updateClientInterest(int clientFd, bool enableWrite);
        bool startCgiFor(int clientFd, const Request& request, const ServerConfig& config, const LocationConfig* location);
        void finalizeCgiResponse(int clientFd);

    public:
        void reapZombies();
        void cleanupInactiveConnections();
        epollManager(const std::vector<int>& listenFds, const std::vector< std::vector<ServerConfig> >& serverGroups);
        ~epollManager();

        pid_t pin[2];
        pid_t pout[2];
        void requestStop();
        void run();
        void gracefulShutdown();
        void saveConnInfo(ClientConnection &conn, pid_t pid);
        std::vector<char*>  buildEnv(std::string &scriptPath, const Request &request, const ServerConfig &config, const LocationConfig* location, ClientConnection &conn);
        void                execChild(std::string &scriptPath, const Request &request, 
        const ServerConfig &config, const LocationConfig* location, ClientConnection &conn);
        void    armWriteEvent(int clientFd, bool enable);
};
