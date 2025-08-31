#pragma once

#include "../../include/Webserv.hpp"


std::string getCurrentDate();
std::string getContentType(const std::string& uri);
std::string createHtmlResponse(const std::string& title, const std::string& content);