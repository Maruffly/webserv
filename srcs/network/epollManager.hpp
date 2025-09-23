#pragma once

#include "../../include/Webserv.hpp"
#include "../http/Response.hpp"
#include "../http/Request.hpp"
#include "../utils/Utils.hpp"
#include "../config/ServerConfig.hpp"
#include "ClientConnection.hpp"

class	epollManager
{
	private:
		int								_serverSocket;
		int								_epollFd;
		std::map<int, std::string>		_clientBuffers;
		std::map<int, ClientConnection> _clientConnections;
		ServerConfig& 					_config;
		time_t 							_lastCleanup;

		void		handleNewConnection();
		void		handleClientData(int clientFd);
		Response 	handleUpload(const Request& request, const LocationConfig& location);
		void		closeClient(int clientFd);
		void		sendErrorResponse(int clientFd, int code, const std::string& message);
		Response	createResponseForRequest(const Request& request);
		bool		isCgiRequest(const std::string& uri) const;
		bool        isMethodAllowed(const std::string& method, const std::string& uri) const;
		std::string resolveFilePath(const std::string& uri) const;
		Response    handleDelete(const Request& request, const LocationConfig* location);
		Response    handlePost(const Request& request, const LocationConfig* location);
		std::string buildAllowHeader(const LocationConfig* location) const;
		const LocationConfig* findLocationConfig(const std::string& uri) const;
		size_t      getEffectiveClientMax(const LocationConfig* location) const;
		bool        processConnectionData(int clientFd);
		bool        parseHeadersFor(int clientFd);
		bool        processFixedBody(int clientFd);
		bool        processChunkedBody(int clientFd);
		bool        parseMultipartAndSave(const std::string& body, const std::string& boundary,
		                                 const std::string& basePath, const std::string& uri,
		                                 size_t& savedCount, bool& anyCreated, std::string& lastSavedPath);
		std::string resolveCgiPath(const std::string& uri, const LocationConfig* location) const;

	public:
		void cleanupIdleConnections();
		epollManager(int serverSocket, ServerConfig& config);
		// epollManager(int serverSocket);
		~epollManager();

		void	run();
};
