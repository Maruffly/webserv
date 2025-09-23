#include "LocationConfig.hpp"

LocationConfig::LocationConfig(): _clientMax(0), _autoindex(false) {}

LocationConfig::~LocationConfig(){}

void LocationConfig::setPath(const std::string& path){
	_path = path;
}

void LocationConfig::setPathUpload(const std::string& path){
	_upload = path;
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

void LocationConfig::setAllowedMethods(std::vector<std::string>& allowedMethods){
	_allowedMethods = allowedMethods;
}

void LocationConfig::setCgiParams(const std::map<std::string, std::string>& cgiParams){
	_cgiParams = cgiParams;
}

void LocationConfig::addCgiPass(const std::string& extension, const std::string& interpreter) {
	_cgiPass[extension] = interpreter;
}

void LocationConfig::addAllowedMethod(const std::string& method) {
	_allowedMethods.push_back(method);
}

void LocationConfig::addCgiParam(const std::string& key, const std::string& value) {
	_cgiParams[key] = value;
}

void LocationConfig::setAutoindex(const std::string& autoindex){
	bool autoIndex;
	if (autoindex == "on")
		autoIndex = true;
	else
		autoIndex = false;
	_autoindex = autoIndex;
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

const std::string& LocationConfig::getUploadPath()const{
	return _upload;
}

bool LocationConfig::hasUploadPath() const {
        return !_upload.empty();
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

const std::map<std::string, std::string> &LocationConfig::getCgiPass()const{
	return _cgiPass;
}

const std::vector<std::string>& LocationConfig::getIPallow()const{
	return _IPallow;
}

const std::vector<std::string>& LocationConfig::getIPdeny()const{
	return _IPdeny;
}

std::string LocationConfig::getCgiInterpreter(const std::string& extension) const {
	std::map<std::string, std::string>::const_iterator it = _cgiPass.find(extension);
	if (it != _cgiPass.end()) {
		return it->second;
	}
	return "";
}

bool LocationConfig::isCgiRequest(const std::string& uri) const {
	if (_cgiPass.empty()) return false;
	
	size_t lastDot = uri.find_last_of('.');
	if (lastDot == std::string::npos) return false;
	
	std::string extension = uri.substr(lastDot);
	return _cgiPass.find(extension) != _cgiPass.end();
}

void LocationConfig::printConfigLocation() const {
	std::cout << "  === Location: " << _path << " ===" << std::endl;
	std::cout << "  Root: " << _root << std::endl;
	std::cout << "  Index: " << _index << std::endl;
	std::cout << "  CGI Pass: ";
	if (_cgiPass.empty()) {
		std::cout << "(none)";
	} else {
		for (std::map<std::string, std::string>::const_iterator it = _cgiPass.begin(); it != _cgiPass.end(); ++it) {
			if (it != _cgiPass.begin()) std::cout << ", ";
			std::cout << it->first << " => " << it->second;
		}
	}
	std::cout << std::endl;
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
	if (!_upload.empty())
		std::cout << " Upload: " << _upload << std::endl;

	std::cout << "  ===========================" << std::endl;
}