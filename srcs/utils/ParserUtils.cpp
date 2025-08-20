#include "ParserUtils.hpp"
#include <iostream>

std::string ParserUtils::trim(const std::string& str) {
	size_t start = str.find_first_not_of(" \t\n\r");
	if (start == std::string::npos)
		return "";
	size_t end = str.find_last_not_of(" \t\n\r");
	return str.substr(start, end - start + 1);
}

std::vector<std::string> ParserUtils::split(const std::string& str, char sep) {
	std::vector<std::string> tokens;
	std::stringstream convert(str);
	std::string token;

	while(std::getline(convert, token, sep)){
		token = trim(token);
		if (!token.empty())
			tokens.push_back(token);
		}
		return tokens;
}

std::string ParserUtils::getInBetween(const std::string& str, const std::string& start, const std::string& end){

	size_t startPos = str.find(start);
	if (startPos == std::string::npos)
		return "";
	size_t endPos = str.find(end);
	if (endPos == std::string::npos)
		return "";
	return trim(str.substr(startPos + start.length(), endPos - startPos - start.length()));	
}

bool ParserUtils::startsWith(const std::string& str, const std::string& prefix) {
	return str.size() >= prefix.size() &&
	str.compare(0, prefix.size(), prefix) == 0;
}

bool ParserUtils::endsWith(const std::string& str, const std::string& suffix){
	return str.size() >= suffix.size() &&
	str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}