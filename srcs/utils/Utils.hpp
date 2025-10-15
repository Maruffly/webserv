#pragma once

#include "Webserv.hpp"
#include "../config/LocationConfig.hpp"


std::string getCurrentDate();
std::string getContentType(const std::string& uri);
std::string createHtmlResponse(const std::string& title, const std::string& content);
int			mkdirRecursive(const std::string& path, mode_t mode = 0750);
bool		fileExists(const std::string& path);
bool		isDirectory(const std::string& path);
std::string	readFileContent(const std::string& path);
std::string	getContentType(const std::string& path);
std::string	generateDirectoryListing(const std::string& dirPath, const std::string& uri);
bool 		isCgiFile(const std::string& uri, const std::vector<LocationConfig>& locations);

std::string toUpperCase(const std::string& str);
std::string replaceChars(const std::string& str, const std::string& from, const std::string& to);
std::string getFileExtension(const std::string& path);
std::string toLowerCase(const std::string &str);
// Ensure directory exists (optionally create recursively)
bool		ensureDirectoryExists(const std::string& path, bool create);
std::string dirnameOf(const std::string& path);
void safeClose(int pipefd[2]);
char* ft_strdup(const std::string& value);
