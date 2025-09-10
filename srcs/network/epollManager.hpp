#pragma once

#include "../../include/Webserv.hpp"
#include "../http/Response.hpp"
#include "../http/Request.hpp"
#include "../utils/Utils.hpp"
#include "../config/ServerConfig.hpp"

class	epollManager
{
	private:
		int							_serverSocket;
		int							_epollFd;
		std::map<int, std::string>	_clientBuffers;
		const ServerConfig& 		_config;

		void		handleNewConnection();
		void		handleClientData(int clientFd);
		void		closeClient(int clientFd);
		void		sendErrorResponse(int clientFd, int code, const std::string& message);
		Response	createResponseForRequest(const Request& request);
		bool		isCgiRequest(const std::string& uri) const;
		bool        isMethodAllowed(const std::string& method, const std::string& uri) const;
		std::string resolveFilePath(const std::string& uri) const;
		const LocationConfig* findLocationConfig(const std::string& uri) const;


	public:
		epollManager(int serverSocket, const ServerConfig& config);
		// epollManager(int serverSocket);
		~epollManager();

		void	run();
};