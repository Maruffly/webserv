#pragma once

#include "../../include/Webserv.hpp"
#include "Session.hpp"

class SessionManager {
private:
    std::map<std::string, Session> _sessions;
    time_t _sessionTimeout;
    time_t _cleanupInterval;
    time_t _lastCleanup;
    
    std::string generateSessionId();
    void cleanupExpiredSessions();
    
public:
    SessionManager(time_t timeout = 1800); // 30 mins
    Session* createSession();
    Session* getSession(const std::string& sessionId);
    void destroySession(const std::string& sessionId);
};