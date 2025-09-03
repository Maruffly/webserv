#include "LocationConfig.hpp"

LocationConfig::LocationConfig(): _clientMax(0), _autoindex(false) {}

LocationConfig::~LocationConfig(){}

void LocationConfig::setPath(const std::string& path){
	_path = path;
}

void LocationConfig::setRoot(const std::string& root){
	_root = root;
}

void LocationConfig::setIndex(const std::string& index){
	_index = index;
}

void LocationConfig::setLimitExcept(const std::string& limit_except){
	_limit_except = limit_except;
}

void LocationConfig::setClientMax(const size_t clientMax){
	_clientMax = clientMax;
}

void LocationConfig::setAutoindex(const bool autoindex){
	_autoindex = autoindex;
}

void LocationConfig::setAllowedMethods(std::vector<std::string>& allowedMethods){
	_allowedMethods = allowedMethods;
}

void LocationConfig::setCgiParams(const std::map<std::string, std::string>& cgiParams){
	_cgiParams = cgiParams;
}

void LocationConfig::setCgiPass(const std::string& cgiPass) {
	_cgiPass = cgiPass; }

void LocationConfig::addAllowedMethod(const std::string& method) {
	_allowedMethods.push_back(method);
}

void LocationConfig::addCgiParam(const std::string& key, const std::string& value) {
	_cgiParams[key] = value;
}

void LocationConfig::addAllow(const std::string& ip) {
	_IPallow.push_back(ip);
}

void LocationConfig::addDeny(const std::string& ip) {
	_IPdeny.push_back(ip);
}

const std::string& LocationConfig::getPath()const{
	return _path;
}

const std::string& LocationConfig::getRoot()const{
	return _root;
}

const std::string& LocationConfig::getIndex()const{
	return _index;
}

const std::string& LocationConfig::getLimitExcept()const{
	return _limit_except;
}

size_t LocationConfig::getClientMax()const{
	return _clientMax;
}

bool LocationConfig::getAutoindex()const{
	return _autoindex;
}

const std::vector<std::string>& LocationConfig::getAllowedMethods()const{
	return _allowedMethods;
}

const std::map<std::string, std::string>& LocationConfig::getCgiParams()const{
	return _cgiParams;
}

const std::string& LocationConfig::getCgiPass()const{
	return _cgiPass;
}

const std::vector<std::string>& LocationConfig::getIPallow()const{
	return _IPallow;
}

const std::vector<std::string>& LocationConfig::getIPdeny()const{
	return _IPdeny;
}

void LocationConfig::printConfigLocation() const {
	std::cout << "  === Location: " << _path << " ===" << std::endl;
	std::cout << "  Root: " << _root << std::endl;
	std::cout << "  Index: " << _index << std::endl;
	std::cout << "  CGI Pass: " << _cgiPass << std::endl;
	std::cout << "  Client Max Body Size: " << _clientMax << std::endl;
	std::cout << "  Autoindex: " << (_autoindex ? "on" : "off") << std::endl;
	
	if (!_cgiParams.empty()) {
		std::cout << "  CGI Params:" << std::endl;
		for (std::map<std::string, std::string>::const_iterator it = _cgiParams.begin(); it != _cgiParams.end(); ++it) {
			std::cout << "    " << it->first << " = " << it->second << std::endl;
		}
	}
	
	if (!_IPallow.empty()) {
		std::cout << "  Allow: ";
		for (size_t i = 0; i < _IPallow.size(); ++i) {
			std::cout << _IPallow[i];
			if (i < _IPallow.size() - 1) std::cout << ", ";
		}
		std::cout << std::endl;
	}
	
	if (!_IPdeny.empty()) {
		std::cout << "  Deny: ";
		for (size_t i = 0; i < _IPdeny.size(); ++i) {
			std::cout << _IPdeny[i];
			if (i < _IPdeny.size() - 1) std::cout << ", ";
		}
		std::cout << std::endl;
	}
	std::cout << "  ===========================" << std::endl;
}