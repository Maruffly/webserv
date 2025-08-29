#include "ParseConfig.hpp"
#include "LocationConfig.hpp"
#include "../utils/ParserUtils.hpp"


ParseConfig::ParseConfig() : _pos(0) {}

ParseConfig::~ParseConfig(){}

std::vector<std::string> ParseConfig::parseBlock(const std::string& blockName) {
	std::vector<std::string>	blocks;
	std::string	searchPattern = blockName + " {";

	size_t pos = 0;
	while ((pos = _configContent.find(searchPattern, pos)) != std::string::npos) {
		std::string blockContent = ParserUtils::getInBetween(_configContent.substr(pos), "{", "}");
		if (!blockContent.empty())
			blocks.push_back(blockContent);
		pos += searchPattern.length() + blockContent.length() + 2;
	}
	return blocks;
}

void ParseConfig::parseLocationDirectives(const std::string& blockContent, LocationConfig& location){
	std::vector<std::string> lines = ParserUtils::split(blockContent, '\n');

	for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = ParserUtils::trim(lines[i]);
		if (line.empty() || line == "{" || line == "}") continue;

		// Supprimer le point-virgule final
		if (!line.empty() && line.back() == ';') {
			line = line.substr(0, line.size() - 1);
		}

		std::vector<std::string> parts = ParserUtils::split(line, ' ');
		if (parts.empty()) continue;

		const std::string& directive = parts[0];
		std::string value;
		for (size_t j = 1; j < parts.size(); ++j) {
			if (j > 1) value += " ";
			value += parts[j];
		}
		if (directive == "root") {
			location.setRoot(value);
		}
		else if (directive == "index") {
			location.setIndex(value);
		}
		else if (directive == "cgi_pass") {
			location.setCgiPass(value);
		}
		else if (directive == "cgi_param" && parts.size() >= 3) {
			location.addCgiParam(parts[1], value.substr(parts[1].size() + 1));
		}
		else if (directive == "client_max_body_size") {
			size_t multiplier = 1;
			if (!value.empty()) {
				char lastChar = std::tolower(value.back());
				if (lastChar == 'k') multiplier = 1024;
				else if (lastChar == 'm') multiplier = 1024 * 1024;
				else if (lastChar == 'g') multiplier = 1024 * 1024 * 1024;
				
				if (multiplier > 1) {
					value = value.substr(0, value.size() - 1);
				}
			}
			location.setClientMax(std::atoi(value.c_str()) * multiplier);
		}
		else if (directive == "autoindex") {
			location.setAutoindex(value == "on");
		}
		else if (directive == "allow") {
			location.addAllow(value);
		}
		else if (directive == "deny") {
			location.addDeny(value);
		}
		else if (directive == "limit_except") {
			std::vector<std::string> methods = ParserUtils::split(value, ' ');
			for (size_t j = 0; j < methods.size(); ++j) {
				location.addAllowedMethod(methods[j]);
			}
		}
		else {
			std::cerr << "Unknown location directive: " << directive << std::endl;
		}
	}
}

void ParseConfig::parseServerDirectives(const std::string& blockContent, ServerConfig& server) {

	std::vector<std::string> lines = ParserUtils::split(blockContent, '\n');

	 for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = ParserUtils::trim(lines[i]);
		if (line.empty() || line == "{" || line == "}")
			continue;
		std::vector<std::string> parts = ParserUtils::split(line, ' ');
		if (parts.empty())
			continue;
		if (ParserUtils::startsWith(line, "server_name")){
			std::string value = ParserUtils::getInBetween(line, "server_name", ";");
			server.setServerName(ParserUtils::trim(value));
		}
		else if (ParserUtils::startsWith(line, "root")){
			std::string value = ParserUtils::getInBetween(line, "root", ";");
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
			server.setAutoindex(ParserUtils::trim(value) == "on");
		}
		else if (ParserUtils::startsWith(line,"client_max_body_size")){
			std::string value = ParserUtils::getInBetween(line, "client_max_body_size", ";");
			server.setClientMax(std::atoi(ParserUtils::trim(value).c_str()));
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
		else if (ParserUtils::startsWith(line, "location")) {
			std::string locationLine = line;
			// Find the opening brace
			size_t openBrace = locationLine.find('{');
			if (openBrace == std::string::npos) {
				// Look for opening brace in next lines
				while (++i < lines.size() && openBrace == std::string::npos) {
					locationLine += " " + lines[i];
					openBrace = locationLine.find('{');
				}
			}
			std::string path = ParserUtils::getInBetween(locationLine, "location", "{");
			path = ParserUtils::trim(path);
			std::cout << "Parsed location path: [" << path << "]" << std::endl;
			if (path.empty()) {
				std::cerr << "Warning: Empty location path" << std::endl;
				continue;
			}
			std::string locBlock;
			size_t braceCount = 1;
			while (++i < lines.size() && braceCount > 0) {
				std::string currentLine = ParserUtils::trim(lines[i]);
				if (currentLine.find('{') != std::string::npos) braceCount++;
				if (currentLine.find('}') != std::string::npos) braceCount--;
				if (braceCount > 0) locBlock += currentLine + "\n";
			}

			LocationConfig location;
			location.setPath(path);
			parseLocationDirectives(locBlock, location);
			server.addLocation(location);
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