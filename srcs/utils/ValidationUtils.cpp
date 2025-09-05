#include "ValidationUtils.hpp"

bool ValidationUtils::isValidPort(int port){
	if (port < 1 || port > 65535)
		return false;
	return true;
}

bool ValidationUtils::isValidPath(const std::string &path){
	
	if (path.empty() || path.length() > 1024)
		return false;
	if (path.find("..") != std::string::npos)
		return false;
	if (path.find("//") != std::string::npos)
		return false;
	if (path.find('\0') != std::string::npos)
		return false;
	return path[0] == '/';
}

bool ValidationUtils::isValidName(const std::string &name){
	if (name.size() > MAXLEN)
		return false;
	return true;
}

bool ValidationUtils::isValidIP(const std::string &ip){
	 if (ip == "localhost")
	 	return true;
	struct sockaddr_in sa;
	return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) == 1;
}

bool ValidationUtils::isValidMethod(const std::string &method){
	static const std::vector<std::string> validMethods = 
	{
        "GET", "POST", "PUT", "DELETE", "HEAD", 
        "OPTIONS", "TRACE", "CONNECT", "PATCH"
    };
    return std::find(validMethods.begin(), validMethods.end(), method) != validMethods.end();
}

bool ValidationUtils::isValidCIDR(const std::string& cidr) {
    size_t slashPos = cidr.find('/');
    if (slashPos == std::string::npos)
		return false;
    
    std::string ip = cidr.substr(0, slashPos);
    std::string mask = cidr.substr(slashPos + 1);
    if (!isValidIP(ip))
		return false;
    try {
        int maskValue = std::stoi(mask);
        if (ip.find(':') != std::string::npos) { // IPv6 - to modify
            return maskValue >= 0 && maskValue <= 128;
        } else { // IPv4
            return maskValue >= 0 && maskValue <= 32;
        }
    } catch (...) {
        return false;
    }
}

bool ValidationUtils::isHttpStatusCode(int code) {
    return code >= 100 && code <= 599;
}

bool ValidationUtils::isValidBodySize(const std::string& sizeStr) {
    if (sizeStr.empty())
		return false;

    size_t multiplier = 1;
    std::string numberPart = sizeStr;
    
    if (!std::isdigit(sizeStr.back())) {
        char unit = std::tolower(sizeStr.back());
        numberPart = sizeStr.substr(0, sizeStr.size() - 1);
        
        if (unit == 'k') multiplier = 1024;
        else if (unit == 'm') multiplier = 1024 * 1024;
        else if (unit == 'g') multiplier = 1024 * 1024 * 1024;
        else return false;
    }
    if (numberPart.empty() || !std::all_of(numberPart.begin(), numberPart.end(), ::isdigit))
        return false;
    return true;
}