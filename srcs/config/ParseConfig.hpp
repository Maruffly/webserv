#pragma once

#include "Webserv.hpp"
#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "../utils/ValidationUtils.hpp"
#include "ParseConfigException.hpp"

enum DirectiveName {
	SERVER_NAME,
	ROOT,
	INDEX,
	LISTEN,
	AUTOINDEX,
	CGI_PARAM,
	UNKNOWN
};

struct Directive {
	std::string name;
	std::string value;
};
class ParseConfig {
			private:
					std::string _configContent;
					size_t 		_pos;
					std::string _configDir; // directory of the loaded config file

		public:
			ServerConfig server;
			ParseConfig();
			~ParseConfig();
			void validateServerConfig(const ServerConfig& server); 
			std::vector<ServerConfig> parse(const std::string& configPath);
			std::vector<std::string> parseBlock(const std::string& blockName);
			void parseLocationDirectives(const std::string& blockContent, LocationConfig& location);
			void parseServerDirectives(const std::string& blockContent, ServerConfig& server);
			std::vector<std::string> extractLocationBlocks(const std::string& serverContent);
			void parseLocationBlock(const std::string& locationBlock, ServerConfig& server);
			Directive parseDirectiveLine(const std::string &rawLine);
};
