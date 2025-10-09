#pragma once

#include "../../include/Webserv.hpp"
#include "../http/Response.hpp"
#include "../http/Request.hpp"
#include "../utils/Utils.hpp"
#include "../config/ServerConfig.hpp"
#include "ClientConnection.hpp"
#include <set>
#include <deque>

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

        // call back
      /*   std::deque<PendingCgiJob> _pendingCgiQueue; // job : clientFd, Request copy, etc.
        int _currentCgiCount;
        int _maxConcurrentCgi; 
 */
        void handleNewConnection(int listenFd);
        void handleClientRead(int clientFd, uint32_t events);
        void handleClientWrite(int clientFd, uint32_t events);
        void handleCgiOutEvent(int pipeFd, uint32_t events);
        void handleCgiInEvent(int pipeFd, uint32_t events);
        void closeClient(int clientFd);
        void purgeClient(int clientFd);
        void sendErrorResponse(int clientFd, int code, const std::string& message);

        // Per-request helpers using selected config
        Response createResponseForRequest(const Request& request, const ServerConfig& config);
        bool isCgiRequest(const std::string& uri, const ServerConfig& config) const;
        bool isMethodAllowed(const std::string& method, const std::string& uri, const ServerConfig& config) const;
        std::string resolveFilePath(const std::string& uri, const ServerConfig& config) const;
        Response handleDelete(const Request& request, const LocationConfig* location, const ServerConfig& config);
        Response handlePost(const Request& request, const LocationConfig* location, const ServerConfig& config);
        std::string buildAllowHeader(const LocationConfig* location) const;
        const LocationConfig* findLocationConfig(const std::string& uri, const ServerConfig& config) const;
        size_t getEffectiveClientMax(const LocationConfig* location, const ServerConfig& config) const;
        bool processConnectionData(int clientFd);
        bool parseHeadersFor(int clientFd);
        bool processFixedBody(int clientFd);
        bool processChunkedBody(int clientFd);
        bool parseMultipartAndSave(const std::string& body, const std::string& boundary,
                                   const std::string& basePath, const std::string& uri,
                                   size_t& savedCount, bool& anyCreated, std::string& lastSavedPath);
        void processReadyRequest(int clientFd);
        std::string resolveErrorPagePath(const std::string& candidate, const ServerConfig& config) const;
        bool loadErrorPage(int code, const ServerConfig* config, std::string& body, std::string& contentType) const;
        void buildErrorResponse(Response& response, int code, const std::string& message, const ServerConfig* config) const;

        void armWriteEvent(int clientFd, bool enable);
        bool startCgiFor(int clientFd, const Request& request, const ServerConfig& config, const LocationConfig* location);
        void finalizeCgiFor(int clientFd);

    public:
        void cleanupInactiveConnections();
        epollManager(const std::vector<int>& listenFds, const std::vector< std::vector<ServerConfig> >& serverGroups);
        ~epollManager();

        void requestStop();
        void run();
};
