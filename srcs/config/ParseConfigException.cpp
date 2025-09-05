#include "ParseConfigException.hpp"
#include <sstream>

std::string ParseConfigException::formatMessage(const std::string& msg, const std::string& directive, const std::string &location) {
    std::stringstream ss;
    ss << location;
    ss << msg;
    if (!directive.empty())
        ss << " (directive: " << directive << ")";
    return ss.str();
}