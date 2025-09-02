#include "ParseConfig.hpp"
#include "LocationConfig.hpp"
#include "../utils/ParserUtils.hpp"


ParseConfig::ParseConfig() : _pos(0) {}

ParseConfig::~ParseConfig(){}

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

            std::cout << "\nParsed block content:" << std::endl;
            std::cout << fullBlock << std::endl;
            std::cout << "Block size: " << fullBlock.length() << std::endl;
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
    
    for (const std::string& directive : directives) {
        std::string line = ParserUtils::trim(directive);
        if (line.empty())
			continue;
        std::vector<std::string> parts = ParserUtils::split(line, ' ');
        if (parts.empty())
			continue;

        const std::string& directive_name = parts[0];
         std::string value;
        for (size_t i = 1; i < parts.size(); i++) {
            if (!value.empty()) value += " ";
            value += parts[i];
        }
		//std::cout << "Processing directive: [" << directive_name << "] with value: [" << value << "]" << std::endl;
        value = ParserUtils::trim(value);
		if (directive_name == "root") {
			location.setRoot(value);
		}
		else if (directive_name == "index") {
			location.setIndex(value);
		}
		else if (directive_name == "cgi_pass") {
			location.setCgiPass(value);
		}
		else if (directive_name == "cgi_param" && parts.size() >= 3) {
			location.addCgiParam(parts[1], value.substr(parts[1].size() + 1));
		}
		else if (directive_name == "client_max_body_size") {
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
		else if (directive_name == "autoindex") {
			location.setAutoindex(value == "on");
		}
		else if (directive_name == "allow") {
			location.addAllow(value);
		}
		else if (directive_name == "deny") {
			location.addDeny(value);
		}
		else if (directive_name == "limit_except") {
			std::vector<std::string> methods = ParserUtils::split(value, ' ');
			for (size_t j = 0; j < methods.size(); ++j) {
				location.addAllowedMethod(methods[j]);
			}
		}
		else {
			std::cerr << "Unknown location directive: " << directive_name << std::endl;
		}
	}
}

void ParseConfig::parseLocationBlock(const std::string& locationBlock, ServerConfig& server) {
    // Extract location path
/* 	std::cout << "LOCATION BLOCK - before parsing location" << std::endl;
	for (int i = 0; i < locationBlock.size(); ++i)
		std::cout << locationBlock[i];
	std::cout << "\n" << std::endl; */

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
    location.setPath(path);
    parseLocationDirectives(content, location);
    server.addLocation(location);
    
    std::cout << "Added location: " << path << std::endl;
    std::cout << "Location content: " << content << std::endl; // Debug
}

void ParseConfig::parseServerDirectives(const std::string& blockContent, ServerConfig& server) {

	std::vector<std::string> lines = ParserUtils::split(blockContent, '\n');
	std::cout << "BLOCK CONTENT - before parseSerDirectives " << std::endl;
	for (int i = 0; i < blockContent.size(); ++i)
		std::cout << blockContent[i];
	std::cout << "\n" << std::endl;
	 for (size_t i = 0; i < lines.size(); ++i) {
		//std::cout << "LINE[" << i << "] = " << lines[i] << std::endl;
		std::string line = ParserUtils::trim(lines[i]);
		if (line.empty() || line == "{" || line == "}")
			continue;
		 if (ParserUtils::startsWith(line, "location")) {
            std::string locationBlock = line;
            size_t openBraces = 1;
            size_t closeBraces = 0;
            
            // Continue collecting lines until we find matching closing brace
            while (++i < lines.size() && openBraces > closeBraces) {
                locationBlock += "\n" + lines[i];
                if (lines[i].find('{') != std::string::npos) openBraces++;
                if (lines[i].find('}') != std::string::npos) closeBraces++;
            }
            
            // Debug print
            std::cout << "Found location block:\n" << locationBlock << std::endl;
            
            parseLocationBlock(locationBlock, server);
            i--; // Decrement i to not skip the next line after location block
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