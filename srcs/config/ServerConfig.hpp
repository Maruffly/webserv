#pragma once

#include "../utils/ParserUtils.hpp"
#include <string>
#include <vector>
#include <map>
#include <iostream>


class ServerConfig {
	private:
			std::string	_serverName;
			std::string _host;
			int			_port;
			std::string	_root;
			std::string _index;
			std::vector<std::string> _listen;
			size_t _clientMax;
			bool _autoindex;
			std::map<int, std::string> _errorPages;
			//std::vector<LocationConfig> _locations;
	public:
			ServerConfig();
			~ServerConfig();
			void setServerName(const std::string& serverName);
			void setHost(const std::string& host);
			void setPort(const int port);
			void setRoot(const std::string& root);
			void setIndex(const std::string& index);
			void setListen(const std::string& listenStr);
			void setClient(const size_t client);
			void setAutoindex(const bool autoindex);
};