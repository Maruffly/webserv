#pragma once

#include "ServerConfig.hpp"
#include "LocationConfig.hpp"

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

enum Directive {
    SERVER_NAME,
    ROOT,
    INDEX,
    LISTEN,
    AUTOINDEX,
    CGI_PARAM,
    UNKNOWN
};

class ParseConfig {
		private:
			std::string _configContent;
			size_t 		_pos;				
				
		public:
			ServerConfig server;
			ParseConfig();
			~ParseConfig();
			std::vector<ServerConfig> parse(const std::string& configPath);
			std::vector<std::string> parseBlock(const std::string& blockName);
			void parseLocationDirectives(const std::string& blockContent, LocationConfig& location);
			void parseServerDirectives(const std::string& blockContent, ServerConfig& server);
			std::vector<std::string> extractLocationBlocks(const std::string& serverContent);
			void parseLocationBlock(const std::string& locationBlock, ServerConfig& server);
};