#include "LocationConfig.hpp"
#include "../utils/Utils.hpp"


// Note: respecter l'ordre de déclaration dans le header pour éviter -Wreorder (avec -Werror)
LocationConfig::LocationConfig()
    : _clientMax(0)
    , _autoindex(false)
    , _uploadCreateDirs(false)
    , _hasReturn(false)
    , _returnCode(0)
{}

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
std::string extension = getFileExtension(uri);
    
    // Extensions CGI courantes
    if (extension == ".py" || extension == ".php" || 
        extension == ".pl" || extension == ".cgi" ||
        extension == ".sh" || uri.find("/cgi-bin/") != std::string::npos) {
        return true;
    }
    
    return false;
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

	if (_hasReturn) {
		std::cout << "  Return: " << _returnCode << " " << _returnUrl << std::endl;
	}
	if (!_uploadStore.empty()) {
		std::cout << "  Upload store: " << _uploadStore << std::endl;
		std::cout << "  Upload create dirs: " << (_uploadCreateDirs?"on":"off") << std::endl;
	}
	std::cout << "  ===========================" << std::endl;
}

void LocationConfig::setReturn(int code, const std::string& url) { _hasReturn = true; _returnCode = code; _returnUrl = url; }
bool LocationConfig::hasReturn() const { return _hasReturn; }
int  LocationConfig::getReturnCode() const { return _returnCode; }
const std::string& LocationConfig::getReturnUrl() const { return _returnUrl; }

void LocationConfig::setUploadStore(const std::string& path) { _uploadStore = path; }
void LocationConfig::setUploadCreateDirs(const std::string& onoff) { _uploadCreateDirs = (onoff == "on"); }
const std::string& LocationConfig::getUploadStore() const { return _uploadStore; }
bool LocationConfig::getUploadCreateDirs() const { return _uploadCreateDirs; }
