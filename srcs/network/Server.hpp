#pragma once

#include "../../include/Webserv.hpp"
#include "../config/ServerConfig.hpp"

class	Server
{
	private:
		int				_listeningSocket;
		int				_port;
		std::string		_host;
		const ServerConfig	_config;

		void		createSocket();
		void		setSocketOptions();
		void		bindSocket();
		void		startListening();
		void		closeSocketIfOpen();
		void		throwSocketError(const std::string& message);


	public:
		Server(const ServerConfig& config);
		~Server();

		int			getListeningSocket() const;
		int			getPort() const;
		const std::string&	getHost() const;
};
