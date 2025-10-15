#include "ServerConfig.hpp"
#include "../../include/Webserv.hpp"
#include "ParseConfigException.hpp"
#include "../utils/ValidationUtils.hpp"
#include <algorithm>
#include <cstdlib>

ServerConfig::ServerConfig()
    : _index("index.html")
    , _clientMax(0)
    , _autoindex(false)
    , _errorPageDirectory("")
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
        this->_errorPageDirectory = src._errorPageDirectory;
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

void ServerConfig::setListen(const std::string& listenStr) {
    std::vector<std::string> tokens = ParserUtils::split(ParserUtils::trim(listenStr), ' ');
    if (tokens.empty())
        throw ParseConfigException("listen directive requires a parameter (ip:port, ip or port)", "listen");

    for (size_t i = 0; i < tokens.size(); ++i) {
        std::string entry = ParserUtils::trim(tokens[i]);
        if (entry.empty())
            continue;
        if (entry == "default_server")
            continue;

        std::string hostCandidate;
        int portCandidate = -1;

        size_t colon = entry.find(':');
        if (colon != std::string::npos) {
            // format: host:port
            hostCandidate = entry.substr(0, colon);
            std::string portPart = entry.substr(colon + 1);
            if (portPart.empty())
                throw ParseConfigException("listen directive missing port after ':'", "listen");

            char *endptr = NULL;
            long portVal = std::strtol(portPart.c_str(), &endptr, 10);
            if (*endptr != '\0' || !ValidationUtils::isValidPort(static_cast<int>(portVal)))
                throw ParseConfigException("Invalid port number in listen directive", "listen");
            portCandidate = static_cast<int>(portVal);
        } else {
            // single token (could be port or host)
            char *endptr = NULL;
            long portVal = std::strtol(entry.c_str(), &endptr, 10);
            if (*endptr == '\0') {
                // it's a port
                if (!ValidationUtils::isValidPort(static_cast<int>(portVal)))
                    throw ParseConfigException("Invalid port number in listen directive", "listen");
                portCandidate = static_cast<int>(portVal);
                hostCandidate = "0.0.0.0";  // host par défaut uniquement si le port est valide
            } else {
                // it's a host only (ex: "127.0.0.1") → alors il faut un port déjà connu
                hostCandidate = entry;
                if (_port <= 0)
                    throw ParseConfigException("listen directive with host requires a port", "listen");
                portCandidate = _port;
            }
        }

        if (hostCandidate.empty())
            throw ParseConfigException("listen directive requires a valid host", "listen");
        if (portCandidate == -1)
            throw ParseConfigException("listen directive requires a valid port", "listen");

        std::string normalized = hostCandidate + ":" + toString(portCandidate);
        if (std::find(_listen.begin(), _listen.end(), normalized) != _listen.end())
            throw ParseConfigException("Duplicate listen directive");
        _listen.push_back(normalized);

        // Première directive listen → définit host/port principaux
        if (_listen.size() == 1) {
            _host = hostCandidate;
            _port = portCandidate;
        }
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

void ServerConfig::addLocation(const LocationConfig& location) 
{
	_locations.push_back(location);
}

void ServerConfig::addErrorPage(int errorCode, const std::string& path) {
	_errorPages[errorCode] = path;
}

void ServerConfig::setErrorPageDirectory(const std::string& directory) {
	_errorPageDirectory = directory;
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

const std::string& ServerConfig::getErrorPageDirectory() const {
	return _errorPageDirectory;
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
	if (!_errorPageDirectory.empty())
		std::cout << "Error Page Directory: " << _errorPageDirectory << std::endl;
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
