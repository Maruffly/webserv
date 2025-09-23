#pragma once

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <dirent.h>  // Ajouter pour opendir/readdir
#include <sys/stat.h> // Ajouter pour stat
#include <cctype>     // Ajouter pour std::tolower

#include "../../include/Webserv.hpp"
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
void		setResponse(Response reponse);