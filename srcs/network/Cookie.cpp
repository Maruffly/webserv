#include "Cookie.hpp"
#include "ClientConnection.hpp"
#include "../utils/ParserUtils.hpp"
#include "../http/Response.hpp"


std::map<std::string, SessionData>& sessionStore()
{
    static std::map<std::string, SessionData> store;
    return store;
}

void pruneExpiredSessions(time_t now)
{
    std::map<std::string, SessionData>& sessions = sessionStore();
    for (std::map<std::string, SessionData>::iterator it = sessions.begin(); it != sessions.end(); ) {
        double idle = difftime(now, it->second.lastSeen);
        if (idle > SESSION_MAX_IDLE) {
            std::map<std::string, SessionData>::iterator eraseIt = it++;
            sessions.erase(eraseIt);
        } else {
            ++it;
        }
    }
}

std::map<std::string, std::string> parseCookies(const std::string& header)
{
    std::map<std::string, std::string> cookies;
    std::stringstream ss(header);
    std::string token;
    while (std::getline(ss, token, ';')) {
        size_t eq = token.find('=');
        if (eq == std::string::npos) continue;
        std::string name = ParserUtils::trim(token.substr(0, eq));
        std::string value = ParserUtils::trim(token.substr(eq + 1));
        if (!name.empty()) cookies[name] = value;
    }
    return cookies;
}

std::string generateSessionId(int clientFd)
{
    static bool seeded = false;
    if (!seeded) {
        std::srand(static_cast<unsigned int>(time(NULL)));
        seeded = true;
    }
    std::ostringstream oss;
    oss << std::hex << time(NULL) << "-" << std::rand() << "-" << clientFd;
    return oss.str();
}

void ensureSessionFor(ClientConnection& conn, Request& request)
{
    std::string sessionId;
    std::string cookieHeader = request.getHeader("Cookie");
    if (!cookieHeader.empty()) {
        std::map<std::string, std::string> cookies = parseCookies(cookieHeader);
        std::map<std::string, std::string>::iterator it = cookies.find("session_id");
        if (it != cookies.end()) sessionId = it->second;
    }
    std::map<std::string, SessionData>& sessions = sessionStore();
    bool created = false;
    if (!sessionId.empty()) {
        std::map<std::string, SessionData>::iterator sit = sessions.find(sessionId);
        if (sit == sessions.end()) {
            SessionData data;
            data.lastSeen = time(NULL);
            data.requestCount = 1;
            sessions[sessionId] = data;
            created = true;
        } else {
            sit->second.lastSeen = time(NULL);
            sit->second.requestCount += 1;
        }
    } else {
        sessionId = generateSessionId(conn.fd);
        SessionData data;
        data.lastSeen = time(NULL);
        data.requestCount = 1;
        sessions[sessionId] = data;
        created = true;
    }

    conn.sessionId = sessionId;
    conn.sessionAssigned = true;
    conn.sessionShouldSetCookie = created;
}

void attachSessionCookie(Response& response, ClientConnection& conn)
{
    if (conn.sessionId.empty()) return;
    if (!conn.sessionShouldSetCookie) return;
    response.setHeader("Set-Cookie", "session_id=" + conn.sessionId + "; Path=/; SameSite=Lax");
    conn.sessionShouldSetCookie = false;
}
