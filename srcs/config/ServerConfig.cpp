#include "ServerConfig.hpp"
#include "../../include/Webserv.hpp"
#include "ParseConfigException.hpp"
#include "../utils/ValidationUtils.hpp"
#include <algorithm>
#include <cstdlib>

ServerConfig::ServerConfig()
    : _host(DEFAULT_HOST)
    , _port(DEFAULT_PORT)
    , _root("")
    , _index("index.html")
    , _clientMax(0)
    , _autoindex(false)
{
}
ServerConfig::~ServerConfig(){}

ServerConfig ServerConfig::operator=(const ServerConfig& src)
{
    if (this != &src)
    {
        this->_serverNames = src._serverNames;
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
	std::vector<std::string> names = ParserUtils::split(serverName, ' ');
	for (size_t i = 0; i < names.size(); ++i) {
		std::string candidate = ParserUtils::trim(names[i]);
		if (candidate.empty())
			continue;
		if (candidate.size() > MAXLEN)
			throw ParseConfigException("Invalid name size", "server_name");
		if (std::find(_serverNames.begin(), _serverNames.end(), candidate) == _serverNames.end())
			_serverNames.push_back(candidate);
	}
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
    std::vector<std::string> tokens = ParserUtils::split(ParserUtils::trim(listenStr), ' ');
    if (tokens.empty())
        throw ParseConfigException("listen directive requires at least one parameter", "listen");

    std::string currentHost = _host.empty() ? "0.0.0.0" : _host;

    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string entry = ParserUtils::trim(tokens[i]);
        if (entry.empty())
            continue;
        if (entry == "default_server")
            continue;

        std::string hostCandidate = currentHost;
        int portCandidate = -1;

        size_t colon = entry.find(':');
        if (colon != std::string::npos) {
            hostCandidate = entry.substr(0, colon);
            std::string portPart = entry.substr(colon + 1);
            if (portPart.empty())
                throw ParseConfigException("listen directive missing port", "listen");
            char *endptr = NULL;
            long portVal = std::strtol(portPart.c_str(), &endptr, 10);
            if (*endptr != '\0' || !ValidationUtils::isValidPort(static_cast<int>(portVal)))
                throw ParseConfigException("Invalid port number", "listen");
            portCandidate = static_cast<int>(portVal);
        } else {
            char *endptr = NULL;
            long portVal = std::strtol(entry.c_str(), &endptr, 10);
            if (*endptr == '\0') {
                if (!ValidationUtils::isValidPort(static_cast<int>(portVal)))
                    throw ParseConfigException("Invalid port number", "listen");
                portCandidate = static_cast<int>(portVal);
            } else {
                hostCandidate = entry;
            }
        }

        if (hostCandidate.empty())
            hostCandidate = "0.0.0.0";
        if (portCandidate == -1)
            portCandidate = (_port > 0) ? _port : DEFAULT_PORT;

        std::string normalized = hostCandidate + std::string(":") + toString(portCandidate);
        if (std::find(_listen.begin(), _listen.end(), normalized) == _listen.end())
            _listen.push_back(normalized);

        if (_listen.size() == 1) {
            _host = hostCandidate;
            _port = portCandidate;
        }

        currentHost = hostCandidate;
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

std::string ServerConfig::getServerName() const{
	if (!_serverNames.empty())
		return _serverNames.front();
	return _host;
}

const std::vector<std::string>& ServerConfig::getServerNames() const {
	return _serverNames;
}

const std::string& ServerConfig::getHost() const{
	return _host;
}

int ServerConfig::getPort() const{
	return _port;
}

const std::string& ServerConfig::getRoot() const{
	return _root;
}

const std::string& ServerConfig::getIndex() const{
	return _index;
}

const std::vector<std::string>& ServerConfig::getListen() const{
	return _listen;
}

size_t ServerConfig::getClientMax() const{
	return _clientMax;
}

bool ServerConfig::getAutoindex() const{
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
	std::cout << "Server Names: ";
	if (_serverNames.empty()) std::cout << "(none)";
	else {
		for (size_t i = 0; i < _serverNames.size(); ++i) {
			if (i) std::cout << ", ";
			std::cout << _serverNames[i];
		}
	}
	std::cout << std::endl;
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
