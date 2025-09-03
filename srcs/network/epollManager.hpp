#pragma once

#include "../../include/Webserv.hpp"
#include "../http/Response.hpp"
#include "../http/Request.hpp"
#include "../utils/Utils.hpp"

class	epollManager
{
	private:
		int		_serverSocket;
		int		_epollFd;
		std::map<int, std::string>	_clientBuffers;

		void		handleNewConnection();
		void		handleClientData(int clientFd);
		void		closeClient(int clientFd);
		void		sendErrorResponse(int clientFd, int code, const std::string& message);

		Response	createResponseForRequest(const Request& request);

	public:
		epollManager(int serverSocket);
		~epollManager();

		void	run();
};