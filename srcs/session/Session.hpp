#pragma once

#include "../../include/Webserv.hpp"


class Session {
public:
    std::string sessionId;
    std::map<std::string, std::string> data;
    time_t expirationTime;
    time_t lastActivity;
    
    Session();
    Session(const std::string& id);
    bool isValid() const;
    void updateActivity();
    void setData(const std::string& key, const std::string& value);
    std::string getData(const std::string& key) const;
};