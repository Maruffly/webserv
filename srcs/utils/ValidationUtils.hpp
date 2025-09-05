#pragma once

#pragma once
#include <string>
#include <vector>
#include <cctype>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>

#define MAXLEN 1064

namespace ValidationUtils {
	bool isValidPort(int port);
	bool isValidPath(const std::string &path);
	bool isValidName(const std::string &name);
	bool isValidIP(const std::string &ip);
	bool isValidMethod(const std::string &method);
	bool isValidCIDR(const std::string& cidr);
	bool isHttpStatusCode(int code);
	bool isValidBodySize(const std::string& sizeStr);

};