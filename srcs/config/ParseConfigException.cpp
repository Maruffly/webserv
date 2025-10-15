#include "Webserv.hpp"
#include "ParseConfigException.hpp"

std::string ParseConfigException::formatMessage(const std::string& msg, const std::string& directive, const std::string &location) {
    std::stringstream ss;
    ss << location;
    ss << msg;
    if (!directive.empty())
        ss << " (directive: " << directive << ")";
    return ss.str();
}
