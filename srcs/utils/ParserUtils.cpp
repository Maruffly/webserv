#include "ParserUtils.hpp"
#include <iostream>

// Erase \t\n\r at the start or end of a str
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

//Extract a string between two string
std::string ParserUtils::getInBetween(const std::string& str, const std::string& start, const std::string& end){

	size_t startPos = str.find(start);
	if (startPos == std::string::npos)
		return "";
	size_t endPos = str.find(end);
	if (endPos == std::string::npos)
		return "";
	return trim(str.substr(startPos + start.length(), endPos - startPos - start.length()));	
}

//Find a prefix match in a string
bool ParserUtils::startsWith(const std::string& str, const std::string& prefix) {
	return str.size() >= prefix.size() &&
	str.compare(0, prefix.size(), prefix) == 0;
}

//Find a suffix match in a string
bool ParserUtils::endsWith(const std::string& str, const std::string& suffix){
	return str.size() >= suffix.size() &&
	str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string ParserUtils::checkBrace(std::string &line, std::vector<std::string> &lines, int &i) {
	std::string locationBlock = line;
	size_t openBraces = 0;
	size_t closeBraces = 0;

	for (size_t j = 0; j < line.size(); ++j) {
		if (line[j] == '{') openBraces++;
		if (line[j] == '}') closeBraces++;
	}
	while (openBraces > closeBraces && i + 1 < static_cast<int>(lines.size())) {
		i++;
		locationBlock += "\n" + lines[i];
		for (size_t j = 0; j < lines[i].size(); ++j) {
			if (lines[i][j] == '{') openBraces++;
			if (lines[i][j] == '}') closeBraces++;
		}
	}
	return locationBlock;
}
