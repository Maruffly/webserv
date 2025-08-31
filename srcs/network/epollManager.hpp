#pragma once

#include "../../include/Webserv.hpp"

class	epollManager
{
	private:
		int		_epollFd;
		int		_serverSocket;
		std::map<int, std::string>	_clientBuffers;

		void		handleNewConnection();
		void		handleClientData(int clientFd);
		void		closeClient(int clientFd);

		Response	createResponseForRequest(const Request& request) 

	public:
		epollManager(int serverSocket);
		~epollManager();

		void	run();
};