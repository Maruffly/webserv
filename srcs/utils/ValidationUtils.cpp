#include "Webserv.hpp"
#include "ValidationUtils.hpp"


// Vérifie que l'octet IPv4 est constitué uniquement de chiffres et reste dans [0, 255].
bool isValidIPv4(const std::string& part) {
	if (part.empty() || part.size() > 3)
		return false;

	int value = 0;
	for (std::string::const_iterator it = part.begin(); it != part.end(); ++it) {
		unsigned char c = static_cast<unsigned char>(*it);
		if (!std::isdigit(c))
			return false;
		value = value * 10 + (c - '0');
		if (value > 255)
			return false;
	}
	return true;
}


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
	struct stat info;
	if (stat(path.c_str(), &info) != 0)
		return false;
    if (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)) // check if accessible
		return false;
    return true;
}

bool ValidationUtils::isValidName(const std::string &name){
	if (name.size() > MAXLEN)
		return false;
	return true;
}

bool ValidationUtils::isValidIP(const std::string &ip){
	 if (ip == "localhost")
	 	return true;

	size_t start = 0;
	int segments = 0;
	while (true) {
		size_t dot = ip.find('.', start);
		std::string part = ip.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
		if (!isValidIPv4(part))
			return false;
		++segments;
		if (dot == std::string::npos)
			break;
		start = dot + 1;
		if (start > ip.size())
			return false;
	}
	return segments == 4;
}

bool ValidationUtils::isValidMethod(const std::string &method) {
    static const std::string validMethods[] = {
        "GET", "POST", "PUT", "DELETE", "HEAD", 
        "OPTIONS", "TRACE", "CONNECT", "PATCH"
    };
    static const std::vector<std::string> validMethodsVec(
        validMethods, 
        validMethods + sizeof(validMethods) / sizeof(validMethods[0])
    );
    return std::find(validMethodsVec.begin(), validMethodsVec.end(), method) != validMethodsVec.end();
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
        int maskValue = std::atoi(mask.c_str());
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

    std::string numberPart = sizeStr;

    if (!std::isdigit(sizeStr[sizeStr.length() - 1])) {
        char unit = std::tolower(sizeStr[sizeStr.length() - 1]);
        numberPart = sizeStr.substr(0, sizeStr.size() - 1);

        if (unit != 'k' && unit != 'm' && unit != 'g') return false;
    }
    bool allDigits = true;
    for (std::string::const_iterator it = numberPart.begin(); it != numberPart.end(); ++it) {
        if (!std::isdigit(*it)) {
            allDigits = false;
            break;
        }
    }
    if (numberPart.empty() || !allDigits)
        return false;
    return true;
}
