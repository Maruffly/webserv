#include "ParseConfig.hpp"
#include "LocationConfig.hpp"
#include "../utils/ParserUtils.hpp"
#include <pwd.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <sstream>


ParseConfig::ParseConfig() : _pos(0) {}

ParseConfig::~ParseConfig(){}

void ParseConfig::validateServerConfig(const ServerConfig& server) {
	// check listen
	if (server.getListen().empty())
		throw ParseConfigException("Missing required directive 'listen' in server block", "server");
	// check server_name
	std::cout << "SERVE_NAME : " << server.getServerName() << std::endl;
	if (server.getServerName().empty())
		throw ParseConfigException("Missing required directive 'server_name' in server block", "server");
	// check root (server / location)
	bool hasRoot = !server.getRoot().empty();
	if (!hasRoot) {
		const std::vector<LocationConfig>& locations = server.getLocations();
		for (size_t i = 0; i < locations.size(); ++i) {
			if (!locations[i].getRoot().empty()) {
				hasRoot = true;
				break;
			}
		}
	}
	if (!hasRoot) {
		throw ParseConfigException("Missing required directive 'root' in server block or locations", "server");
	}
}

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
        // Single value: interpreter path (allow <user> and relative)
        // Use current parser instance helpers via a lambda that mirrors methods
        // Note: We cannot access ParseConfig members here; so expect value already trimmed.
        // We'll expand <user> with getenv/getpwuid and resolve relative to current working dir of process.
        // However, to keep consistent with server root handling, expect caller to pass through expand + resolve.
        // As a fallback, only validate absolute here.
        if (!ValidationUtils::isValidPath(value))
            throw ParseConfigException("Invalid CGI pass path - must be an absolute path", "cgi_pass");
        location.addCgiPass(".*", value); // default extension for all files
    } 
    else if (cgitoken.size() >= 2) {
        // multi cgi: first token is extension, the rest is interpreter path (possibly with args)
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
            // Note: paramValue is usually a path (e.g., PYTHONPATH). It must be absolute.
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

// Expand /home/<user>/... to /home/$(USER)/...
std::string ParseConfig::expandLocalUserPath(const std::string& path) const {
    const std::string marker = "/home/<user>/";
    std::string::size_type pos = path.find(marker);
    if (pos == std::string::npos)
        return path;

    std::string username;
    const char* envUser = std::getenv("USER");
    if (envUser && *envUser) {
        username = envUser;
    } else {
        struct passwd* pw = getpwuid(getuid());
        if (pw && pw->pw_name) username = pw->pw_name;
    }
    if (username.empty())
        return path;

    std::string expanded = path;
    expanded.replace(pos, marker.size(), std::string("/home/") + username + "/");
    return expanded;
}

// If path starts with '/', return it; otherwise join with _configDir and canonicalize.
// If canonicalization fails, return the joined path (still absolute) to be validated later.
std::string ParseConfig::resolvePathRelativeToConfig(const std::string& path) const {
    if (!path.empty() && path[0] == '/')
        return path;
    // Join with config dir
    std::string base = _configDir.empty() ? std::string(".") : _configDir;
    std::string joined = base;
    if (!joined.empty() && joined[joined.size() - 1] != '/')
        joined += "/";
    joined += path;
    // Try to canonicalize
    char buf[PATH_MAX];
    char* rp = realpath(joined.c_str(), buf);
    if (rp)
        return std::string(buf);
    return joined; // fallback
}

Directive ParseConfig::parseDirectiveLine(const std::string &rawLine) {
    Directive result;
    // Nettoyer les commentaires par ligne et concaténer les lignes utiles
    std::stringstream ss(rawLine);
    std::string accum;
    std::string one;
    while (std::getline(ss, one)) {
        size_t h = one.find('#');
        if (h != std::string::npos) one = one.substr(0, h);
        one = ParserUtils::trim(one);
        if (!one.empty()) {
            if (!accum.empty()) accum += " ";
            accum += one;
        }
    }
    accum = ParserUtils::trim(accum);
    if (accum.empty()) return result;

    std::vector<std::string> token = ParserUtils::split(accum, ' ');
    if (token.empty()) return result;
    result.name = token[0];
    for (size_t i = 1; i < token.size(); ++i) {
        if (!result.value.empty()) result.value += " ";
        result.value += token[i];
    }
    result.value = ParserUtils::trim(result.value);
    return result;
}

void ParseConfig::parseLocationDirectives(const std::string& blockContent, LocationConfig& location){
    // Nettoyer le contenu: retirer les commentaires par ligne et concaténer
    std::stringstream ss(blockContent);
    std::string cleaned;
    std::string line;
    while (std::getline(ss, line)) {
        // couper à '#'
        size_t h = line.find('#');
        if (h != std::string::npos) line = line.substr(0, h);
        line = ParserUtils::trim(line);
        if (line.empty()) continue;
        if (!cleaned.empty()) cleaned += '\n';
        cleaned += line;
    }
    // Découper en directives par ';'
    std::vector<std::string> directives = ParserUtils::split(cleaned, ';');

	for (size_t i = 0; i < directives.size(); ++i)
	{
		Directive directive = parseDirectiveLine(directives[i]);

    	if (directive.name.empty())
			continue;
		try {
				if (directive.name == "root") {
	                // Expand <user> and allow relative paths resolved from config dir
	                std::string value = expandLocalUserPath(ParserUtils::trim(directive.value));
	                value = resolvePathRelativeToConfig(value);
	                if (!ValidationUtils::isValidPath(value))
	                    throw ParseConfigException("' - Invalid root path, it must be an absolut path.", "root", directives[i]);
	                location.setRoot(value);
	            }
			else if (directive.name == "index") {
				location.setIndex(directive.value);
			}
            else if (directive.name == "cgi_pass") {
                // Support relative interpreter paths and <user> expansion
                std::vector<std::string> parts = ParserUtils::split(ParserUtils::trim(directive.value), ' ');
                if (parts.size() == 1) {
                    std::string interp = expandLocalUserPath(parts[0]);
                    interp = resolvePathRelativeToConfig(interp);
                    directive.value = interp;
                } else if (parts.size() >= 2) {
                    std::string extension = parts[0];
                    std::string interpreter;
                    for (size_t j = 1; j < parts.size(); ++j) {
                        if (!interpreter.empty()) interpreter += " ";
                        interpreter += parts[j];
                    }
                    interpreter = expandLocalUserPath(interpreter);
                    interpreter = resolvePathRelativeToConfig(interpreter);
                    directive.value = extension + std::string(" ") + interpreter;
                }
                parseCgiPass(directive.value, location);
            }
            else if (directive.name == "cgi_param") {
                // Support relative param paths and <user> expansion
                std::vector<std::string> parts = ParserUtils::split(ParserUtils::trim(directive.value), ' ');
                if (parts.size() >= 2) {
                    std::string paramName = parts[0];
                    std::string paramValue = parts[1];
                    for (size_t j = 2; j < parts.size(); ++j) {
                        paramValue += " " + parts[j];
                    }
                    paramValue = expandLocalUserPath(paramValue);
                    paramValue = resolvePathRelativeToConfig(paramValue);
                    directive.value = paramName + std::string(" ") + paramValue;
                }
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
			else if (directive.name == "upload_store") {
				std::string p = ParserUtils::trim(directive.value);
				p = expandLocalUserPath(p);
				p = resolvePathRelativeToConfig(p);
				location.setUploadStore(p);
			}
			else if (directive.name == "upload_create_dirs") {
				if (directive.value != "on" && directive.value != "off")
					throw ParseConfigException("' - upload_create_dirs must be 'on' or 'off'", "upload_create_dirs", directives[i]);
				location.setUploadCreateDirs(ParserUtils::trim(directive.value));
			}
			else if (directive.name == "return") {
				// Syntaxe: return <code> <url>;
				std::vector<std::string> parts = ParserUtils::split(directive.value, ' ');
				if (parts.size() < 2)
					throw ParseConfigException("return requires <code> <url>", "return", directives[i]);
				int code = std::atoi(parts[0].c_str());
				if (code < 300 || code > 399)
					throw ParseConfigException("return code must be a 3xx code", "return", directives[i]);
				std::string url = parts[1];
				location.setReturn(code, url);
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
	bool hasListenDirective = false;
	bool hasServerBlock = false;

	for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = ParserUtils::trim(lines[i]);

		// ignorer les commentaires
		if (!line.empty() && line[0] == '#')
			continue;
		if (ParserUtils::startsWith(line, "server {")) {
			hasServerBlock = true;
			continue;
		}
		if (line.empty() || line == "{" || line == "}")
			continue;
		if (ParserUtils::startsWith(line, "location")) {
			int idx = (int)i;
			std::string locationBlock = ParserUtils::checkBrace(line, lines, idx);  // avance idx à la fin du bloc
			i = (size_t)idx;
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
				std::string value = expandLocalUserPath(ParserUtils::trim(directive.value));
				value = resolvePathRelativeToConfig(value);
				if (!ValidationUtils::isValidPath(value))
					throw ParseConfigException("Location : Invalid root path", "root");
				server.setRoot(value);
			}
		else if (ParserUtils::startsWith(line,"index")){
			directive.value = ParserUtils::getInBetween(line, "index", ";");
			server.setIndex(ParserUtils::trim(directive.value));
		}
		else if (ParserUtils::startsWith(line,"listen")){
			directive.value = ParserUtils::getInBetween(line, "listen", ";");
			std::string listenValue = ParserUtils::trim(directive.value);
			if (listenValue.empty()) {
				throw ParseConfigException("listen directive cannot be empty", "listen");
			}
			server.setListen(listenValue);
			hasListenDirective = true;
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
	if (!hasServerBlock)
		throw ParseConfigException("Bloc 'server {' manquant ou mal formé", "server");
	validateServerConfig(server);
}

std::vector<ServerConfig> ParseConfig::parse(const std::string& configPath){
		std::ifstream file(configPath.c_str());
		if (!file.is_open()) {
			throw std::runtime_error("Error: Cannot open config file: " + configPath);
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		_configContent = buffer.str(); //convert stream to str
		// Remember directory of the config file to resolve relative paths securely
		std::string::size_type slash = configPath.find_last_of('/');
		if (slash == std::string::npos) {
			_configDir = ".";
		} else if (slash == 0) {
			_configDir = "/";
		} else {
			_configDir = configPath.substr(0, slash);
		}
	std::vector<ServerConfig> servers;
	std::vector<std::string> serverBlock = parseBlock("server");
	if (serverBlock.empty()) {
		throw ParseConfigException("No server block found in configuration file", "config");
	}
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
