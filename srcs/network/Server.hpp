#pragma once

#include "../../include/Webserv.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/ServerConfig.hpp"

class	Server
{
	private:
		int				_serverSocket;
		int				_port;
		std::string		_host;
		ServerConfig	_config;

		void		createSocket();
		void		setSocketOptions();
		void		bindSocket();
		void		startListening();


	public:
		Server(ServerConfig& config);
		// Server(int port = DEFAULT_PORT, const std::string &host = DEFAULT_HOST);
		~Server();

		void		run();

		int			getSocket() const;
		int			getPort() const;
		std::string	getHost() const;
};