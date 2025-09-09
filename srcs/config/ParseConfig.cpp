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

void parseCgiPass(std::string &value, LocationConfig& location){
	std::vector<std::string> cgitoken = ParserUtils::split(value, ' ');
	
	if (cgitoken.size() == 1) {
		if (!ValidationUtils::isValidPath(value))
			throw ParseConfigException("Invalid CGI pass path - must be an absolute path", "cgi_pass");
		location.addCgiPass(".*", value); // default extension for all files
	} 
	else if (cgitoken.size() >= 2) {
		// multi cgi
		std::string extension = cgitoken[0];
		std::string interpreter;
		for (size_t j = 1; j < cgitoken.size(); ++j) {
			if (!interpreter.empty()) interpreter += " ";
			interpreter += cgitoken[j];
		}
		if (!ValidationUtils::isValidPath(interpreter))
			throw ParseConfigException("Invalid CGI interpreter path - must be an absolute path", "cgi_pass");
		location.addCgiPass(extension, interpreter);
	}
	else
		throw ParseConfigException("Invalid cgi_pass format", "cgi_pass");
}

void parseCgiParam(Directive &directive, LocationConfig& location, std::vector<std::string> directives, int i)
{
	std::vector<std::string> parts = ParserUtils::split(directive.value, ' ');
			if (parts.size() < 2) {
				throw ParseConfigException("' - cgi_param requires a name and a value",
										"cgi_param", directives[i]);
			}
			std::string paramName = parts[0];
			std::string paramValue = parts[1];
			for (size_t j = 2; j < parts.size(); ++j) {
				paramValue += " " + parts[j];
			}
			if (!ValidationUtils::isValidPath(paramValue)) {
				throw ParseConfigException("' - Invalid CGI param path, it must be an absolute path.",
										"cgi_param", directives[i]);
			}
			location.addCgiParam(paramName, paramValue);
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
		
		// parse until corresponding brace closing
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

Directive ParseConfig::parseDirectiveLine(const std::string &rawLine) {
    Directive result;
    std::string line = ParserUtils::trim(rawLine);

    if (line.empty())
        return result;
    std::vector<std::string> token = ParserUtils::split(line, ' ');
    if (token.empty())
        return result;
    result.name = token[0];
    for (size_t i = 1; i < token.size(); ++i) {
        if (!result.value.empty())
            result.value += " ";
        result.value += token[i];
    }
    result.value = ParserUtils::trim(result.value);
    return result;
}

void ParseConfig::parseLocationDirectives(const std::string& blockContent, LocationConfig& location){
	std::string trimmedContent = ParserUtils::trim(blockContent);
    if (!trimmedContent.empty() && trimmedContent[trimmedContent.length() - 1] == ';') {
        trimmedContent = trimmedContent.substr(0, trimmedContent.length() - 1);
    }
	std::vector<std::string> directives = ParserUtils::split(blockContent, ';');

	for (size_t i = 0; i < directives.size(); ++i)
	{
		Directive directive = parseDirectiveLine(directives[i]);

    	if (directive.name.empty())
			continue;
		try {
			if (directive.name == "root") {
				if (!ValidationUtils::isValidPath(directive.value))
					throw ParseConfigException("' - Invalid root path, it must be an absolut path.", "root", directives[i]);
				location.setRoot(directive.value);
			}
			else if (directive.name == "index") {
				location.setIndex(directive.value);
			}
			else if (directive.name == "cgi_pass") {
				parseCgiPass(directive.value, location);
			}
			else if (directive.name == "cgi_param") {
				parseCgiParam(directive, location, directives, i);
			}
			else if (directive.name == "client_max_body_size") {
				size_t bodySize;
				std::string errorDetail;
				if (!parseBodySize(directive.value, bodySize, errorDetail))
					throw ParseConfigException("' - Invalid client_max_body_size: " + errorDetail, "client_max_body_size", directives[i]);
				location.setClientMax(bodySize);
			}
			else if (directive.name == "autoindex") {
				if (directive.value != "on" && directive.value != "off")
					throw ParseConfigException("' - Autoindex must be 'on' or 'off'", "autoindex", directives[i]);
				location.setAutoindex(directive.value);
			}
			else if (directive.name == "allow") {
				if (directive.value != "all" && !ValidationUtils::isValidIP(directive.value) && !ValidationUtils::isValidCIDR(directive.value))
					throw ParseConfigException("' - Invalid IP address or CIDR", "allow", directives[i]);
				location.addAllow(directive.value);
			}
			else if (directive.name == "deny") {
				if (directive.value != "all" && !ValidationUtils::isValidIP(directive.value) && !ValidationUtils::isValidCIDR(directive.value))
					throw ParseConfigException("' - Invalid IP address or CIDR", "deny", directives[i]);
				location.addDeny(directive.value);
			}
			else if (directive.name == "limit_except") {
				std::vector<std::string> methods = ParserUtils::split(directive.value, ' ');
				if (methods.empty())
						throw ParseConfigException("limit_except requires at least one method", "limit_except", directives[i]);
				for (size_t j = 0; j < methods.size(); ++j) {
					if (!ValidationUtils::isValidMethod(methods[j]))
						throw ParseConfigException("' - Invalid HTTP method: " + methods[j], directives[i]);
					location.addAllowedMethod(methods[j]);
					}
			}
			else
				throw ParseConfigException("Unknown location directive: " + directive.name, directives[i]);
	}
	catch (const ParseConfigException& e){throw;}
	}
}

void ParseConfig::parseLocationBlock(const std::string& locationBlock, ServerConfig& server) {
	size_t locationStart = locationBlock.find("location");
	size_t braceStart = locationBlock.find('{');

	if (locationStart == std::string::npos || braceStart == std::string::npos) {
		std::cerr << "Invalid location block format" << std::endl;
		return;
	}
	// extract path
	std::string path = locationBlock.substr(locationStart + 8, braceStart - (locationStart + 8));
	path = ParserUtils::trim(path);
	if (path.empty()) {
		std::cerr << "Warning: Empty location path" << std::endl;
		return;
	}
	// extract content between braces
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
	// clean brace excess
	content = ParserUtils::trim(content);

	LocationConfig location;
	ParseConfig parse;
	location.setPath(path);
	parseLocationDirectives(content, location);
	server.addLocation(location);
}

void ParseConfig::parseServerDirectives(const std::string& blockContent, ServerConfig& server) {
	std::vector<std::string> lines = ParserUtils::split(blockContent, '\n');
	Directive directive;

	for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = ParserUtils::trim(lines[i]);

		if (line.empty() || line == "{" || line == "}" || line == "server {")
			continue;
		if (ParserUtils::startsWith(line, "location")) {
			std::string locationBlock = ParserUtils::checkBrace(line, lines, i);  // i is now passed by reference
			parseLocationBlock(locationBlock, server);
    		continue;
		}
		std::vector<std::string> token = ParserUtils::split(line, ' ');
		if (token.empty())
			continue;
		if (ParserUtils::startsWith(line, "server_name")){
			directive.value = ParserUtils::getInBetween(line, "server_name", ";");
			server.setServerName(ParserUtils::trim(directive.value));
		}
		else if (ParserUtils::startsWith(line, "root")){
			directive.value = ParserUtils::getInBetween(line, "root", ";");
			if (!ValidationUtils::isValidPath(directive.value))
				throw ParseConfigException("Location : Invalid root path", "root");
			server.setRoot(ParserUtils::trim(directive.value));
		}
		else if (ParserUtils::startsWith(line,"index")){
			directive.value = ParserUtils::getInBetween(line, "index", ";");
			server.setIndex(ParserUtils::trim(directive.value));
		}
		else if (ParserUtils::startsWith(line,"listen")){
			directive.value = ParserUtils::getInBetween(line, "listen", ";");
			server.setListen(ParserUtils::trim(directive.value));
		}
		else if (ParserUtils::startsWith(line,"autoindex")){
			directive.value = ParserUtils::getInBetween(line, "autoindex", ";");
			if (directive.value != "on" && directive.value != "off")
				throw ParseConfigException("' - Autoindex must be 'on' or 'off'", "autoindex");
			server.setAutoindex(directive.value);
		}
		else if (ParserUtils::startsWith(line,"client_max_body_size")) {
			directive.value = ParserUtils::getInBetween(line, "client_max_body_size", ";");
			size_t bodySize;
			std::string errorDetail;

			if (!parseBodySize(ParserUtils::trim(directive.value), bodySize, errorDetail)) {
				throw ParseConfigException("' - Invalid client_max_body_size: " + errorDetail, "client_max_body_size");
  			}
   			server.setClientMax(bodySize);
		}
		else if (ParserUtils::startsWith(line, "error_page")) {
			std::string value = ParserUtils::getInBetween(line, "error_page", ";");
	   		std::vector<std::string> token = ParserUtils::split(value, ' ');
			if (token.size() >= 2) {
				std::string errorPath = token.back();
				for (size_t j = 0; j < token.size() - 1; ++j) {
					int errorCode = std::atoi(token[j].c_str());
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
	std::vector<ServerConfig> servers;
	std::vector<std::string> serverBlock = parseBlock("server");
	for (size_t i = 0; i < serverBlock.size(); ++i){
		ServerConfig server;
		parseServerDirectives(serverBlock[i], server);
		servers.push_back(server);
	}
	if (servers.empty())
		std::cerr << "No server blocks found in config file" << std::endl;
	return servers;
}

/* int main(){
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
} */