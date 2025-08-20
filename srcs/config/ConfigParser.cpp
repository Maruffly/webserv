#include "ConfigParser.hpp"
#include "../utils/ParserUtils.hpp"


ConfigParser::ConfigParser() : _pos(0) {}

ConfigParser::~ConfigParser(){}


std::vector<std::string> ConfigParser::parseBlock(const std::string& blockName) {
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

void ConfigParser::parseServerDirectives(const std::string& blockContent, ServerConfig& server) {

	std::vector<std::string> lines = ParserUtils::split(blockContent, '\n');

	for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = ParserUtils::trim(lines[i]);
		if (line.empty())
			continue;
		if (ParserUtils::startsWith(line, "server_name")) {
			server.setServerName(line.substr(SERVER_NAME));
		}
		else if (ParserUtils::startsWith(line, "root")) {
			server.setRoot(line.substr(ROOT));
		}
		else if (ParserUtils::startsWith(line, "index")) {
			server.setIndex(line.substr(INDEX));
		}
		else if (ParserUtils::startsWith(line, "listen")) {
			server.setListen(line.substr(LISTEN));
		}
		else if (ParserUtils::startsWith(line, "autoindex")) {
			std::string value = ParserUtils::trim(line.substr(AUTOINDEX));
			server.setAutoindex(value == "on");
		}
		/* else if (ParserUtils::startsWith(line, "cgi_param")) {
			std::vector<std::string> parts = ParserUtils::split(line, ' ');
			if (parts.size() >= 3) {
				server.addCgiParam(parts[1], parts[2]);
			}
		} */
		else if (ParserUtils::startsWith(line, "location")) {
			// récupère le bloc location complet
			std::string locBlock = ParserUtils::getInBetween(blockContent.substr(blockContent.find(line)), "{", "}");
			// parseLocationDirectives(locBlock, locationObj);
		}
		else {
			std::cerr << "Unknown directive: " << line << std::endl;
		}
	}
}

std::vector<ServerConfig> ConfigParser::parse(const std::string& configPath){
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
	ConfigParser parse;
	parse.parse("linux.conf");
}