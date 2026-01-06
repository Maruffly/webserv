#pragma once

#include "Webserv.hpp"

namespace ParserUtils {
	std::string trim(const std::string& str); // trim \t\n\r
	std::vector<std::string> split(const std::string& str, char sep); // split string with sep
	std::string getInBetween(const std::string& str, const std::string& start, const std::string& end); // extract str between two str
	bool startsWith(const std::string& str, const std::string& prefix); // detect if str begins with prefix
	bool endsWith(const std::string& str, const std::string& suffix); // detect if str ends with suffix
	std::string checkBrace(std::string &line, std::vector<std::string> &lines, int &i); 
}
