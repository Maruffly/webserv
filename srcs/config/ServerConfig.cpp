#include "ServerConfig.hpp"
#include "ParseConfigException.hpp"
#include "../utils/ValidationUtils.hpp"

ServerConfig::ServerConfig(){}
ServerConfig::~ServerConfig(){}

// Definition manquante qui provoquait une erreur de l’éditeur de liens
ServerConfig ServerConfig::operator=(const ServerConfig& src)
{
    if (this != &src)
    {
        this->_serverName = src._serverName;
        this->_host = src._host;
        this->_port = src._port;
        this->_root = src._root;
        this->_index = src._index;
        this->_listen = src._listen;
        this->_clientMax = src._clientMax;
        this->_autoindex = src._autoindex;
        this->_errorPages = src._errorPages;
        this->_locations = src._locations;
    }
    return *this;
}

void ServerConfig::setServerName(const std::string& serverName){
	if (serverName.size() > MAXLEN)
		throw ParseConfigException("Invalid name size", "server_name");
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
    std::vector<std::string> token = ParserUtils::split(listenStr, ' ');
    
    std::string address = token[0];
        if (address.find(':') != std::string::npos) {
            // Format IP:PORT
            std::vector<std::string> addrtoken = ParserUtils::split(address, ':');
            if (addrtoken.size() == 2 || _port > 0) {
                _host = addrtoken[0];
                _port = std::atoi(addrtoken[1].c_str());
                if (_port < 0 || _port > 65535)
                    throw ParseConfigException("Invalid port number : must be inferior to 65535", "autoindex");
                // store normalized listen entry
                std::string key = _host + std::string(":") + toString(_port);
                if (std::find(_listen.begin(), _listen.end(), key) == _listen.end())
                    _listen.push_back(key);
            }
            } else {
                // Format PORT only
                _host = "0.0.0.0";
                _port = std::atoi(address.c_str());
                if (_port < 0 || _port > 65535)
                    throw ParseConfigException("Invalid port number - must be a positive integer between 0 and 65535", "listen");
                std::string key = _host + std::string(":") + toString(_port);
                if (std::find(_listen.begin(), _listen.end(), key) == _listen.end())
                    _listen.push_back(key);
            }
}

void ServerConfig::setClientMax(const size_t clientMax){
	_clientMax = clientMax;
}

void ServerConfig::setAutoindex(const std::string& autoindex){
	bool autoIndex;
	if (autoindex == "on")
		autoIndex = true;
	else
		autoIndex = false;
	_autoindex = autoIndex;
}

const std::string& ServerConfig::getServerName(){
	return _serverName;
}

const std::string& ServerConfig::getHost(){
	return _host;
}

int ServerConfig::getPort(){
	return _port;
}

const std::string& ServerConfig::getRoot() const{
	return _root;
}

const std::string& ServerConfig::getIndex() const{
	return _index;
}

const std::vector<std::string>& ServerConfig::getListen(){
	return _listen;
}

size_t ServerConfig::getClientMax() const{
	return _clientMax;
}

bool ServerConfig::getAutoindex(){
	return _autoindex;
}

void ServerConfig::addLocation(const LocationConfig& location) {
	_locations.push_back(location);
	std::cout << "Added location: " << location.getPath() << std::endl;
    std::cout << "Total locations now: " << _locations.size() << std::endl; // Debug
}

void ServerConfig::addErrorPage(int errorCode, const std::string& path) {
	_errorPages[errorCode] = path;
}

const std::vector<LocationConfig>& ServerConfig::getLocations() const {
    return _locations;
}

const std::map<int, std::string>& ServerConfig::getErrorPages() const {
    return _errorPages;
}

std::string ServerConfig::getErrorPagePath(int code) const {
    std::map<int, std::string>::const_iterator it = _errorPages.find(code);
    if (it != _errorPages.end()) return it->second;
    return std::string();
}

void ServerConfig::printConfig() const {
	std::cout << "=== Server Configuration ===" << std::endl;
	std::cout << "Server Name: " << _serverName << std::endl;
	std::cout << "Host: " << _host << std::endl;
	std::cout << "Port: " << _port << std::endl;
	std::cout << "Root: " << _root << std::endl;
	std::cout << "Index: " << _index << std::endl;
	std::cout << "Autoindex: " << (_autoindex ? "on" : "off") << std::endl;
	std::cout << "Client Max Body Size: " << _clientMax << std::endl;
	std::cout << "Listen Directives: ";
	for (size_t i = 0; i < _listen.size(); ++i) {
		std::cout << _listen[i];
		if (i < _listen.size() - 1) std::cout << ", ";
	}
	std::cout << std::endl;
	std::cout << "============================" << std::endl;
	for (size_t i = 0; i < _locations.size(); ++i) {
		const LocationConfig &loc = _locations[i];
		loc.printConfigLocation();
	}
}
