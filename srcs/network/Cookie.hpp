#pragma once

#include "Webserv.hpp"
#include "../http/Request.hpp"

struct SessionData {
	time_t lastSeen;
	size_t requestCount;
};

class ClientConnection;
class Response;

std::string generateSessionId(int clientFd);
void ensureConnectionSession(ClientConnection& conn, const Request& request);
void attachSessionCookie(Response& response, ClientConnection& conn);
std::map<std::string, SessionData>& sessionStore();
void removeExpiredSessions(time_t now);
