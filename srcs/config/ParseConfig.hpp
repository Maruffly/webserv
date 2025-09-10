#pragma once

#include "ServerConfig.hpp"
#include "LocationConfig.hpp"
#include "../utils/ValidationUtils.hpp"
#include "ParseConfigException.hpp"

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

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

					// Expand placeholders like /home/<user>/... to the current username
					std::string expandLocalUserPath(const std::string& path) const;
					// Resolve relative path against _configDir and return canonical absolute path if possible
					std::string resolvePathRelativeToConfig(const std::string& path) const;

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
			Directive parseDirectiveLine(const std::string &rawLine);
};
