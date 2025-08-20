#include "ServerConfig.hpp"

ServerConfig::ServerConfig(){}
ServerConfig::~ServerConfig(){}

void ServerConfig::setServerName(const std::string& serverName){
	_serverName = serverName;
}

void ServerConfig::setHost(const std::string& host){
	_host = host;
}

void ServerConfig::setPort(const int port){
	_port = port;
}

void ServerConfig::setRoot(const std::string& root){
	_root = root;
}

void ServerConfig::setIndex(const std::string& index){
	_index = index;
}

void ServerConfig::setListen(const std::string& listenStr){
	std::vector<std::string> parts = ParserUtils::split(listenStr, ' ');
	
	std::string address = parts[0];
	
	if (address.find(':') != std::string::npos) {
		// Format IP:PORT
		std::vector<std::string> addrParts = ParserUtils::split(address, ':');
		if (addrParts.size() == 2) {
			_host = addrParts[0];
			_port = std::atoi(addrParts[1].c_str());
		}
	} else {
		// Format PORT only
		_host = "0.0.0.0";
		_port = std::atoi(address.c_str());
	}
}

void ServerConfig::setClient(const size_t clientMax){
	_clientMax = clientMax;
}

void ServerConfig::setAutoindex(const bool autoindex){
	_autoindex = autoindex;
}