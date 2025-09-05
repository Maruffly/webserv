#include "ParseConfig.hpp"
#include "LocationConfig.hpp"
#include "../utils/ParserUtils.hpp"


ParseConfig::ParseConfig() : _pos(0) {}

ParseConfig::~ParseConfig(){}

bool parseBodySize(const std::string& sizeStr, size_t& result, std::string& errorDetail) {
	errorDetail = "";
	const size_t MAX_BODY_SIZE = 4UL * 1024UL * 1024UL * 1024UL;
	if (sizeStr.empty()) {
		errorDetail = "\nValue cannot be empty";
		return false;
	}
	
	std::string numberStr = sizeStr;
	size_t multiplier = 1;
	
	// Check for unit
	if (!isdigit(sizeStr[sizeStr.size() - 1])) {
		char unit = tolower(sizeStr[sizeStr.size() - 1]);
		numberStr = sizeStr.substr(0, sizeStr.size() - 1);

		if (unit == 'k') multiplier = 1024;
		else if (unit == 'm') multiplier = 1024 * 1024;
		else if (unit == 'g') multiplier = 1024 * 1024 * 1024;
		else {
			errorDetail = "\nInvalid unit";
			return false;
		}
	}
	
	if (numberStr.empty()) {
		errorDetail = "Missing numeric value";
		return false;
	}
	char* endptr = NULL;
	unsigned long value = strtoul(numberStr.c_str(), &endptr, 10);
	if (*endptr != '\0') {
		errorDetail = "\nInvalid number format";
		return false;
	}

	unsigned long total = value * multiplier;

	// Protection contre les dépassements de size_t
	if (total > static_cast<unsigned long>(-1)) {
		errorDetail = "\nValue too large (overflow)";
		return false;
	}

	result = static_cast<size_t>(total);
	if (result > MAX_BODY_SIZE){
		errorDetail = "\nValue too large (exceeds 4G limit)";
		return false;
	}
	return true;
}

std::vector<std::string> ParseConfig::parseBlock(const std::string& blockName) {
	std::vector<std::string> blocks;
	std::string searchPattern = blockName + " {";
	
	size_t pos = 0;
	while ((pos = _configContent.find(searchPattern, pos)) != std::string::npos) {
		size_t braceStart = _configContent.find('{', pos);
		if (braceStart == std::string::npos) break;
		
		size_t openBraces = 1;
		size_t closeBraces = 0;
		size_t endPos = braceStart + 1;
		
		// Parse jusqu'à trouver l'accolade fermante correspondante
		while (endPos < _configContent.length() && openBraces > closeBraces) {
			if (_configContent[endPos] == '{') {
				openBraces++;
			}
			else if (_configContent[endPos] == '}') {
				closeBraces++;
			}
			endPos++;
		}
		if (openBraces == closeBraces) {
			std::string fullBlock = _configContent.substr(pos, endPos - pos);
			blocks.push_back(fullBlock);
		}
		pos = endPos;
	}
	return blocks;
}


void ParseConfig::parseLocationDirectives(const std::string& blockContent, LocationConfig& location){
	std::string trimmedContent = ParserUtils::trim(blockContent);
	if (trimmedContent.back() == ';') {
		trimmedContent = trimmedContent.substr(0, trimmedContent.length() - 1);
	}
	std::vector<std::string> directives = ParserUtils::split(blockContent, ';');

	for (size_t i = 0; i < directives.size(); ++i)
	{
		std::string line = ParserUtils::trim(directives[i]);
		if (line.empty())
			continue;
		std::vector<std::string> parts = ParserUtils::split(line, ' ');
		if (parts.empty())
			continue;
		const std::string& directive_name = parts[0];
		 std::string value;
		for (size_t i = 1; i < parts.size(); i++) {
			if (!value.empty())
				value += " ";
			value += parts[i];
		}
		value = ParserUtils::trim(value);

		if (directive_name == "root") {
			if (!ValidationUtils::isValidPath(value))
				throw ParseConfigException("' - Invalid root path, it must be an absolut path.", "root", directives[i]);
			location.setRoot(value);
		}
		else if (directive_name == "index") {
			location.setIndex(value);
		}
		else if (directive_name == "cgi_pass") {
			if (!ValidationUtils::isValidPath(value))
				throw ParseConfigException("' - Invalid CGI pass path, it must be an absolut path.", "cgi_pass", directives[i]);
			location.setCgiPass(value);
		}
		else if (directive_name == "cgi_param" && parts.size() >= 3) {
			std::string paramName = parts[1];
			std::string paramValue = value.substr(paramName.size() + 1);
/* 			std::cout << paramValue << std::endl; */
			/* paramValue = ParserUtils::trim(paramValue); */
			if (!ValidationUtils::isValidPath(paramValue))
				throw ParseConfigException("' - Invalid CGI param path, it must be an absolut path.", "cgi_param", directives[i]);
			location.addCgiParam(paramName, paramValue);
}
		 else if (directive_name == "client_max_body_size") {
			size_t bodySize;
			std::string errorDetail;
			if (!parseBodySize(value, bodySize, errorDetail))
				throw ParseConfigException("' - Invalid client_max_body_size: " + errorDetail, "client_max_body_size", directives[i]);
			location.setClientMax(bodySize);
		}
		else if (directive_name == "autoindex") {
			 if (value != "on" && value != "off")
				throw ParseConfigException("' - Autoindex must be 'on' or 'off'", "autoindex", directives[i]);
			location.setAutoindex(value);
		}
		else if (directive_name == "allow") {
			if (value != "all" && !ValidationUtils::isValidIP(value) && !ValidationUtils::isValidCIDR(value))
				throw ParseConfigException("' - Invalid IP address or CIDR", "allow", directives[i]);
			location.addAllow(value);
		}
		else if (directive_name == "deny") {
			if (value != "all" && !ValidationUtils::isValidIP(value) && !ValidationUtils::isValidCIDR(value))
				throw ParseConfigException("' - Invalid IP address or CIDR", "deny", directives[i]);
			location.addDeny(value);
		}
		else if (directive_name == "limit_except") {
			std::vector<std::string> methods = ParserUtils::split(value, ' ');
			if (methods.empty())
					throw ParseConfigException("limit_except requires at least one method", "limit_except", directives[i]);
			for (size_t j = 0; j < methods.size(); ++j) {
				if (!ValidationUtils::isValidMethod(methods[j]))
					throw ParseConfigException("' - Invalid HTTP method: " + methods[j], directives[i]);
				location.addAllowedMethod(methods[j]);
				}
		}
		else
			 throw ParseConfigException("Unknown location directive: " + directive_name, directives[i]);
		}
}

void ParseConfig::parseLocationBlock(const std::string& locationBlock, ServerConfig& server) {
	size_t locationStart = locationBlock.find("location");
	size_t braceStart = locationBlock.find('{');

	if (locationStart == std::string::npos || braceStart == std::string::npos) {
		std::cerr << "Invalid location block format" << std::endl;
		return;
	}
	// Extract path
	std::string path = locationBlock.substr(locationStart + 8, braceStart - (locationStart + 8));
	path = ParserUtils::trim(path);
	if (path.empty()) {
		std::cerr << "Warning: Empty location path" << std::endl;
		return;
	}

	// Extract content between braces
	std::string content;
	size_t openBraces = 1;
	size_t closeBraces = 0;
	size_t pos = braceStart + 1;
	
	while (pos < locationBlock.size() && openBraces > closeBraces) {
		if (locationBlock[pos] == '{') openBraces++;
		if (locationBlock[pos] == '}') closeBraces++;
		
		if (openBraces > closeBraces) {
			content += locationBlock[pos];
		}
		pos++;
	}
	// Clean brace excess
	content = ParserUtils::trim(content);

	LocationConfig location;
	ParseConfig parse;
	location.setPath(path);
	parseLocationDirectives(content, location);
	server.addLocation(location);
}

void ParseConfig::parseServerDirectives(const std::string& blockContent, ServerConfig& server) {
	std::vector<std::string> lines = ParserUtils::split(blockContent, '\n');
	
	for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = ParserUtils::trim(lines[i]);

		if (line.empty() || line == "{" || line == "}" || line == "server {")
			continue;
		
		if (ParserUtils::startsWith(line, "location")) {
			std::string locationBlock = line;
			size_t openBraces = 0;
			size_t closeBraces = 0;

			for (size_t j = 0; j < line.size(); ++j) {
				if (line[j] == '{') openBraces++;
				if (line[j] == '}') closeBraces++;
			}

			while (++i < lines.size() && openBraces > closeBraces) {
				locationBlock += "\n" + lines[i];
				for (size_t j = 0; j < lines[i].size(); ++j) {
					if (lines[i][j] == '{') openBraces++;
					if (lines[i][j] == '}') closeBraces++;
				}
			}
			parseLocationBlock(locationBlock, server);
			if (i < lines.size())
				i--;
			continue;
		}
		std::vector<std::string> parts = ParserUtils::split(line, ' ');
		if (parts.empty())
			continue;
		if (ParserUtils::startsWith(line, "server_name")){
			std::string value = ParserUtils::getInBetween(line, "server_name", ";");
			server.setServerName(ParserUtils::trim(value));
		}
		else if (ParserUtils::startsWith(line, "root")){
			std::string value = ParserUtils::getInBetween(line, "root", ";");
			if (!ValidationUtils::isValidPath(value))
				throw ParseConfigException("Location : Invalid root path", "root");
			server.setRoot(ParserUtils::trim(value));
		}
		else if (ParserUtils::startsWith(line,"index")){
			std::string value = ParserUtils::getInBetween(line, "index", ";");
			server.setIndex(ParserUtils::trim(value));
		}
		else if (ParserUtils::startsWith(line,"listen")){
			std::string value = ParserUtils::getInBetween(line, "listen", ";");
			server.setListen(ParserUtils::trim(value));
		}
		else if (ParserUtils::startsWith(line,"autoindex")){
			std::string value = ParserUtils::getInBetween(line, "autoindex", ";");
			if (value != "on" && value != "off")
				throw ParseConfigException("' - Autoindex must be 'on' or 'off'", "autoindex");
			server.setAutoindex(value);
			/* server.setAutoindex(ParserUtils::trim(value) == "on"); */
		}
		else if (ParserUtils::startsWith(line,"client_max_body_size")) {
			std::string value = ParserUtils::getInBetween(line, "client_max_body_size", ";");
			size_t bodySize;
			std::string errorDetail;

			if (!parseBodySize(ParserUtils::trim(value), bodySize, errorDetail)) {
				throw ParseConfigException("' - Invalid client_max_body_size: " + errorDetail, "client_max_body_size");
  			}
   			server.setClientMax(bodySize);
		}
		else if (ParserUtils::startsWith(line, "error_page")) {
			std::string value = ParserUtils::getInBetween(line, "error_page", ";");
	   		std::vector<std::string> parts = ParserUtils::split(value, ' ');
			if (parts.size() >= 2) {
				std::string errorPath = parts.back();
				for (size_t j = 0; j < parts.size() - 1; ++j) {
					int errorCode = std::atoi(parts[j].c_str());
					if (errorCode > 0) {
						server.addErrorPage(errorCode, errorPath);
					}
				}
			}
		}
		else
			std::cerr << "Unknown directive: " << line << std::endl;
	}
}

std::vector<ServerConfig> ParseConfig::parse(const std::string& configPath){
	std::ifstream file(configPath.c_str());
	if (!file.is_open()) {
		throw std::runtime_error("Error: Cannot open config file: " + configPath);
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	_configContent = buffer.str(); //convert stream to str
	/* for (size_t i = 0; i < _configContent.size(); ++i)
		std::cout << _configContent[i]; */
	std::vector<ServerConfig> servers;
	std::vector<std::string> serverBlock = parseBlock("server");
	for (size_t i = 0; i < serverBlock.size(); ++i){
		ServerConfig server;
		parseServerDirectives(serverBlock[i], server);
		servers.push_back(server);
		// std::cout << "BLOCK [" << i << "] \n" << serverBlock[i] << std::endl;
	}
	if (servers.empty())
		std::cerr << "No server blocks found in config file" << std::endl;
	return servers;
}

int main(){
	try {
		ParseConfig parser;
		std::vector<ServerConfig> servers = parser.parse("linux.conf");
		
		for (size_t i = 0; i < servers.size(); ++i) {
			std::cout << "\nServer #" << i + 1 << ":" << std::endl;
			servers[i].printConfig();
		}
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}