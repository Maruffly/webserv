#pragma once

#include "ServerConfig.hpp"
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

class ConfigParser {
		private:
			std::string _configContent;
			size_t 		_pos;				
				
		public:
			ConfigParser();
			~ConfigParser();
			std::vector<ServerConfig> parse(const std::string& configPath);
			std::vector<std::string> parseBlock(const std::string& blockName);
			void parseServerDirectives(const std::string& blockContent, ServerConfig& server);
				
};