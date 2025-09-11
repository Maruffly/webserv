#include "SessionManager.hpp"

#include "SessionManager.hpp"
#include "../utils/Utils.hpp"
#include <cstdlib>
#include <ctime>

SessionManager::SessionManager(time_t timeout) : _sessionTimeout(timeout) {
    srand(time(NULL));
    _lastCleanup = time(NULL);
    _cleanupInterval = 300; // clean / 5mins
}

std::string SessionManager::generateSessionId() {
    const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string id;
    
    for (int i = 0; i < 32; ++i) {
        id += chars[rand() % (sizeof(chars) - 1)];
    }
    return id;
}

Session* SessionManager::createSession() {
    cleanupExpiredSessions();
    
    std::string sessionId = generateSessionId();
    Session newSession(sessionId);
    _sessions[sessionId] = newSession;
    
    return &_sessions[sessionId];
}

Session* SessionManager::getSession(const std::string& sessionId) {
    cleanupExpiredSessions();
    
    std::map<std::string, Session>::iterator it = _sessions.find(sessionId);
    if (it != _sessions.end() && it->second.isValid()) {
        it->second.updateActivity();
        return &(it->second);
    }
    return NULL;
}

void SessionManager::destroySession(const std::string& sessionId) {
    std::map<std::string, Session>::iterator it = _sessions.find(sessionId);
    if (it != _sessions.end()) {
        _sessions.erase(it);
    }
}

void SessionManager::cleanupExpiredSessions() {
    time_t now = time(NULL);
    if (now - _lastCleanup < _cleanupInterval) {
        return;
    }
    
    _lastCleanup = now;
    std::map<std::string, Session>::iterator it = _sessions.begin();
    
    while (it != _sessions.end()) {
        if (!it->second.isValid()) {
            std::map<std::string, Session>::iterator toErase = it++;
            _sessions.erase(toErase);
        } else {
            ++it;
        }
    }
}