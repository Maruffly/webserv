#pragma once

#include "../utils/ParserUtils.hpp"
#include <string>
#include <vector>
#include <map>
#include <iostream>

#include "LocationConfig.hpp"
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
			std::vector<LocationConfig> _locations;

	public:
			LocationConfig serverlocation;
			ServerConfig();
			~ServerConfig();
			ServerConfig operator=(const ServerConfig& src);
			void setServerName(const std::string& serverName);
			void setHost(const std::string& host);
			void setPort(const int port);
			void setRoot(const std::string& root);
			void setIndex(const std::string& index);
			void setListen(const std::string& listenStr);
			void setClientMax(const size_t clientMax);
			void setAutoindex(const bool autoindex);
			void addErrorPage(int errorCode, const std::string& path);
			void setLocation(const std::vector<LocationConfig> locationConfig);
			void addLocation(const LocationConfig& location);
			const std::vector<LocationConfig>& getLocations() const;
			const std::string& getServerName();
			const std::string& getHost();
			const int getPort();
			const std::string& getRoot();
			const std::string& getIndex();
			const std::vector<std::string>& getListen();
			const size_t getClientMax();
			const bool getAutoindex();
			const std::string& getLocation();

			void printConfig() const;

};