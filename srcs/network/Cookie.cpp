#include "Cookie.hpp"
#include "ClientConnection.hpp"
#include "../utils/ParserUtils.hpp"
#include "../http/Response.hpp"


// Parses the Cookie header into a name/value map.
std::map<std::string, std::string> parseCookies(const std::string& header)
{
    std::map<std::string, std::string> cookies;
    std::stringstream stream(header);
    std::string token;

    while (std::getline(stream, token, ';')) {
        size_t separator = token.find('=');
        if (separator == std::string::npos)
            continue;

        const std::string name = ParserUtils::trim(token.substr(0, separator));
        const std::string value = ParserUtils::trim(token.substr(separator + 1));

        if (!name.empty())
            cookies[name] = value;
    }
    return cookies;
}


// Extracts the session identifier from the Cookie header if present.
std::string extractSessionId(const Request& request)
{
    const std::string cookieHeader = request.getHeader("Cookie");
    if (cookieHeader.empty())
        return std::string();
    const std::map<std::string, std::string> cookies = parseCookies(cookieHeader);
    std::map<std::string, std::string>::const_iterator it = cookies.find("session_id");
    if (it == cookies.end())
        return std::string();
    return it->second;
}


// Attempts to locate an existing session by identifier.
SessionData* findSession(std::map<std::string, SessionData>& sessions, const std::string& sessionId)
{
    if (sessionId.empty())
        return NULL;
    std::map<std::string, SessionData>::iterator it = sessions.find(sessionId);
    if (it == sessions.end())
        return NULL;
    return &it->second;
}


// Inserts a session entry and reports whether it was newly created.
SessionData& createSession(std::map<std::string, SessionData>& sessions, const std::string& sessionId, time_t now, bool& created)
{
    SessionData data;
    data.lastSeen = now;
    data.requestCount = 1;
    std::pair<std::map<std::string, SessionData>::iterator, bool> inserted =
        sessions.insert(std::make_pair(sessionId, data));
    created = inserted.second;
    return inserted.first->second;
}


// Updates the usage statistics of an existing session.
void touchSession(SessionData& session, time_t now)
{
    session.lastSeen = now;
    session.requestCount += 1;
}


// Returns the global in-memory session storage.
std::map<std::string, SessionData>& sessionStore()
{
    static std::map<std::string, SessionData> store;
    return store;
}


// Removes sessions that have been idle longer than SESSION_MAX_IDLE seconds.
void removeExpiredSessions(time_t now)
{
    std::map<std::string, SessionData>& sessions = sessionStore();
    for (std::map<std::string, SessionData>::iterator it = sessions.begin(); it != sessions.end();) 
    {
        double idle = difftime(now, it->second.lastSeen);

        if (idle > SESSION_MAX_IDLE) 
        {
            std::map<std::string, SessionData>::iterator eraseIt = it++;
            sessions.erase(eraseIt);
        } 
        else 
            ++it;
    }
}


// Generates a new random session identifier.
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


// Ensures that the connection has an associated session state.
void ensureConnectionSession(ClientConnection& conn, const Request& request)
{
    std::map<std::string, SessionData>& sessions = sessionStore();
    std::string sessionId = extractSessionId(request);
    SessionData* session = findSession(sessions, sessionId);
    const time_t now = time(NULL);
    bool created = false;

    if (!session) {
        if (sessionId.empty())
            sessionId = generateSessionId(conn.fd);
        session = &createSession(sessions, sessionId, now, created);
        if (!created)
            touchSession(*session, now);
    } else {
        touchSession(*session, now);
    }

    conn.sessionId = sessionId;
    conn.sessionAssigned = true;
    conn.sessionShouldSetCookie = created;
}


// Attaches the Set-Cookie header when the session is new.
void attachSessionCookie(Response& response, ClientConnection& conn)
{
    if (conn.sessionId.empty())
        return;
    if (!conn.sessionShouldSetCookie)
        return;
    response.setHeader("Set-Cookie", "session_id=" + conn.sessionId + "; Path=/; SameSite=Lax");
    conn.sessionShouldSetCookie = false;
}
