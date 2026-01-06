#include "Webserv.hpp"
#include "epollManager.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/ServerConfig.hpp"
#include "../utils/Utils.hpp"
#include "../utils/ParserUtils.hpp"
#include "Cookie.hpp"


// Converts a binary IPv4 address into dotted-decimal notation.
std::string formatIpv4Address(const struct in_addr& addr) 
{
    unsigned long ip = ntohl(addr.s_addr);
    std::string dotted = toString((ip >> 24) & 0xFF);
    dotted += ".";
    dotted += toString((ip >> 16) & 0xFF);
    dotted += ".";
    dotted += toString((ip >> 8) & 0xFF);
    dotted += ".";
    dotted += toString(ip & 0xFF);
    return dotted;
}


// Splits the raw header buffer into the request line, header block and trailing body fragment.
struct HeaderSections {
    std::string requestLine;
    std::string headerBlock;
    std::string remainder;
};

// Extracts header sections from the client buffer. Returns false while headers are incomplete.
bool extractHeaderSections(const std::string& buffer, HeaderSections& sections) {
    size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        return false;

    sections.headerBlock = buffer.substr(0, headerEnd);
    sections.remainder = buffer.substr(headerEnd + 4);

    size_t firstEol = sections.headerBlock.find("\r\n");
    if (firstEol == std::string::npos)
        return false;

    sections.requestLine = sections.headerBlock.substr(0, firstEol);
    sections.headerBlock.erase(0, firstEol + 2);
    return true;
}

// Parses the request line and stores method, URI and version on the connection.
bool parseRequestLine(const std::string& line, ClientConnection& conn) {
    size_t firstSpace = line.find(' ');
    size_t secondSpace = (firstSpace == std::string::npos) ? std::string::npos : line.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos)
        return false;

    conn.method = line.substr(0, firstSpace);
    conn.uri = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    conn.version = line.substr(secondSpace + 1);
    return true;
}

// Inserts an HTTP header line into the connection header map (lowercased name).
void storeHeaderLine(const std::string& line, ClientConnection& conn) {
    size_t colon = line.find(':');
    if (colon == std::string::npos)
        return;

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
    while (!name.empty() && (name[name.size() - 1] == ' ' || name[name.size() - 1] == '\t')) name.erase(name.size() - 1);
    for (size_t i = 0; i < name.size(); ++i)
        name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    if (!name.empty())
        conn.headers[name] = value;
}

// Parses all header lines following the request line.
void parseHeaderBlock(const std::string& block, ClientConnection& conn) {
    size_t lineStart = 0;
    while (lineStart < block.size()) {
        size_t lineEnd = block.find("\r\n", lineStart);
        if (lineEnd == std::string::npos)
            break;
        std::string line = block.substr(lineStart, lineEnd - lineStart);
        storeHeaderLine(line, conn);
        lineStart = lineEnd + 2;
    }
}

// Normalises the body handling strategy depending on Content-Length / Transfer-Encoding.
void configureBodyStrategy(ClientConnection& conn, const std::string& remainder) {
    std::string transferEncoding;
    std::string contentLength;
    std::map<std::string, std::string>::const_iterator teIt = conn.headers.find("transfer-encoding");
    if (teIt != conn.headers.end())
        transferEncoding = teIt->second;
    std::map<std::string, std::string>::const_iterator clIt = conn.headers.find("content-length");
    if (clIt != conn.headers.end())
        contentLength = clIt->second;

    std::string transferLower = transferEncoding;
    for (size_t i = 0; i < transferLower.size(); ++i)
        transferLower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(transferLower[i])));

    if (!transferLower.empty() && transferLower.find("chunked") != std::string::npos) {
        conn.bodyType = BODY_CHUNKED;
        conn.chunkState = CHUNK_READ_SIZE;
    } else if (!contentLength.empty()) {
        conn.contentLength = static_cast<size_t>(std::strtoul(contentLength.c_str(), NULL, 10));
        conn.bodyType = (conn.contentLength > 0) ? BODY_FIXED : BODY_NONE;
    } else {
        conn.bodyType = BODY_NONE;
    }

    conn.buffer.clear();
    if (conn.bodyType == BODY_FIXED) {
        conn.body.append(remainder);
        conn.bodyReceived = conn.body.size();
    } else if (conn.bodyType == BODY_CHUNKED) {
        conn.chunkBuffer.append(remainder);
    }
}

// Serialises the parsed connection data back into a raw HTTP request string.
std::string buildRawHttpRequest(const ClientConnection& conn) 
{
    std::string raw = conn.method + " " + conn.uri + " " + conn.version + "\r\n";
    for (std::map<std::string, std::string>::const_iterator it = conn.headers.begin(); it != conn.headers.end(); ++it) {
        if (it->first == "transfer-encoding" || it->first == "content-length")
            continue;
        raw += it->first + ": " + it->second + "\r\n";
    }
    if (!conn.body.empty())
        raw += std::string("Content-Length: ") + toString(conn.body.size()) + "\r\n";
    raw += "\r\n";
    raw += conn.body;
    return raw;
}

// Applies keep-alive / close rules according to version and Connection header.
void applyKeepAlivePolicy(ClientConnection& conn) 
{
    std::string connectionValue;
    std::map<std::string, std::string>::iterator it = conn.headers.find("connection");
    if (it != conn.headers.end())
        connectionValue = it->second;

    std::string connectionLower = connectionValue;
    for (size_t i = 0; i < connectionLower.size(); ++i)
        connectionLower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(connectionLower[i])));
    bool explicitClose = connectionLower.find("close") != std::string::npos;
    bool explicitKeep = connectionLower.find("keep-alive") != std::string::npos;

    std::string versionUpper = conn.version;
    for (size_t i = 0; i < versionUpper.size(); ++i)
        versionUpper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(versionUpper[i])));

    if (explicitClose) 
    {
        conn.keepAlive = false;
        return;
    }
    if (versionUpper == "HTTP/1.1")
        conn.keepAlive = true;
    else if (versionUpper == "HTTP/1.0")
        conn.keepAlive = explicitKeep;
    else
        conn.keepAlive = explicitKeep;
}


// Resets every per-request field so the connection can handle a new request.
void resetClientState(ClientConnection& conn)
{
    conn.headers.clear();
    conn.body.clear();
    conn.chunkBuffer.clear();
    conn.method.clear();
    conn.uri.clear();
    conn.version.clear();
    conn.state = READING_HEADERS;
    conn.headersParsed = false;
    conn.bodyType = BODY_NONE;
    conn.contentLength = 0;
    conn.bodyReceived = 0;
    conn.chunkState = CHUNK_READ_SIZE;
    conn.currentChunkSize = 0;
    conn.hasResponse = false;
    conn.keepAlive = false;
    conn.outBuffer.clear();
    conn.outOffset = 0;
    conn.cgiRunning = false;
    conn.cgiPid = -1;
    conn.cgiInFd = -1;
    conn.cgiOutFd = -1;
    conn.cgiInOffset = 0;
    conn.cgiOutBuffer.clear();
    conn.isReading = false;
}


// Registers every listening socket and prepares host:port groupings.
epollManager::epollManager(const std::vector<int>& listenFds, const std::vector< std::vector<ServerConfig> >& serverGroups)
    : _epollFd(-1)
    , _running(true)
    , _activeCgiCount(0)
{
    _lastCleanup = time(NULL);
    _epollFd = epoll_create1(0);
    if (_epollFd == -1)
        throw std::runtime_error("epoll_create1 failed");

    if (listenFds.size() != serverGroups.size()) {
        throw std::runtime_error("listenFds and serverConfigs size mismatch");
    }

    for (size_t i = 0; i < listenFds.size(); ++i) {
        int sfd = listenFds[i];
        _listenSockets.insert(sfd);
        _serverGroups[sfd] = serverGroups[i];

        struct epoll_event event;
        event.events = EPOLLIN; // monitor read on listening sockets
        event.data.fd = sfd;
        if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, sfd, &event) == -1) {
            close(_epollFd);
            throw std::runtime_error("Failed to add server socket to epoll");
        }
    }
}


// Ensures all sockets, CGI pipes and sessions are released on shutdown.
epollManager::~epollManager()
{
    _running = false;
    std::vector<int> fds;
    for (std::map<int, ClientConnection>::iterator it = _clientConnections.begin();
        it != _clientConnections.end(); ++it)
        fds.push_back(it->first);
    for (size_t i = 0; i < fds.size(); ++i)
        closeClientSocket(fds[i]);
    if (_epollFd != -1)
        close(_epollFd);
    _clientConnections.clear();
    _clientBuffers.clear();
    _cgiOutToClient.clear();
    _cgiInToClient.clear();
    _listenSockets.clear();
    _serverForClientFd.clear();
    _serverGroups.clear();
    sessionStore().clear();
}


// Signals the event loop to exit after the current iteration.
void epollManager::requestStop() { _running = false; }


// Closes idle clients, enforces CGI/read timeouts and expires sessions.
void epollManager::cleanupInactiveConnections() {
    time_t now = time(NULL);
    if (now - _lastCleanup < CLEANUP_INTERVAL) return;
    _lastCleanup = now;
    int closedCount = 0;
    std::map<int, std::string>::iterator bufIt = _clientBuffers.begin();
    while (bufIt != _clientBuffers.end()) {
        int clientFd = bufIt->first;
        std::map<int, ClientConnection>::iterator connIt = _clientConnections.find(clientFd);
        if (connIt != _clientConnections.end()) {
            double idleTime = difftime(now, connIt->second.lastActivity);
            if (idleTime > CONNECTION_TIMEOUT) {
                std::map<int, std::string>::iterator next = bufIt;
                ++next;
                closeClientSocket(clientFd);
                removeClientState(clientFd);
                bufIt = next;
                closedCount++;
                continue;
            }
        }
        bufIt++;
    }
    if (closedCount > 0) LOG("Closed " + toString(closedCount) + " idle connections");

    // CGI timeouts + read timeouts to ensure no hanging connections
    for (std::map<int, ClientConnection>::iterator it = _clientConnections.begin(); it != _clientConnections.end(); ) {
        ClientConnection& c = it->second;
        double idle = difftime(now, c.lastActivity);
        if (c.cgiRunning && c.cgiStart && difftime(now, c.cgiStart) > CGI_TIMEOUT) {
            if (c.cgiPid > 0){
                kill(c.cgiPid, SIGKILL);
		        --_activeCgiCount;
            }
            if (c.cgiInFd != -1) {
                epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiInFd, NULL);
                close(c.cgiInFd);
                _cgiInToClient.erase(c.cgiInFd);
                c.cgiInFd = -1;
            }
            if (c.cgiOutFd != -1) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiOutFd, NULL); close(c.cgiOutFd); _cgiOutToClient.erase(c.cgiOutFd); c.cgiOutFd = -1; }
            c.cgiRunning = false;
            c.keepAlive = false;
            queueErrorResponse(c.fd, 504, "Gateway Timeout");
        } else if ((!c.headersParsed || c.state == READING_BODY) && idle > READ_TIMEOUT) {
            if (!c.hasResponse) {
                c.keepAlive = false;
                queueErrorResponse(c.fd, 408, "Request Timeout");
            }
        }
        ++it;
    }
    removeExpiredSessions(now);
}


// Accepts every pending client connection on the given listening socket.
void epollManager::acceptPendingConnections(int listenFd)
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLen = sizeof(clientAddress);
    int clientSocket;
    ClientConnection newConn;

    while ((clientSocket = accept(listenFd, (struct sockaddr*)&clientAddress, &clientAddrLen)) != -1)
    {
        if (_clientBuffers.size() >= MAX_CLIENTS) {
            close(clientSocket);
            continue;
        }
        int flags = fcntl(clientSocket, F_GETFL, 0); // to check
        fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

        struct epoll_event event;
        event.events = EPOLLIN; // EPOLLOUT armed when needed
        event.data.fd = clientSocket;
        if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1) {
            ERROR_SYS("epoll_ctl add client"); close(clientSocket);
            continue;
        }
        newConn.fd = clientSocket;
        newConn.listenFd = listenFd;
        newConn.lastActivity = time(NULL);
        newConn.isReading = false;
        newConn.remoteAddr = formatIpv4Address(clientAddress.sin_addr);
        newConn.remotePort = ntohs(clientAddress.sin_port); //to check
        _clientConnections[clientSocket] = newConn;
        _clientBuffers[clientSocket].clear();
        // Default to first server of the group for this listen fd
        if (_serverGroups.find(listenFd) != _serverGroups.end() && !_serverGroups[listenFd].empty())
            _serverForClientFd[clientSocket] = _serverGroups[listenFd][0];
    }
    // EAGAIN acceptable when drained
}


// Parses the headers currently stored for the connection and prepares body decoding.
bool epollManager::parseClientHeaders(int clientFd) 
{
    ClientConnection &conn = _clientConnections[clientFd];

    HeaderSections sections;
    if (!extractHeaderSections(conn.buffer, sections))
        return false;
    if (!parseRequestLine(sections.requestLine, conn))
        return false;

    parseHeaderBlock(sections.headerBlock, conn);
    configureBodyStrategy(conn, sections.remainder);

    conn.headersParsed = true;
    applyKeepAlivePolicy(conn);
    conn.state = (conn.bodyType == BODY_NONE) ? READY : READING_BODY;
    return true;
}


// Transfers buffered data into the fixed-size request body until fully received.
bool epollManager::consumeFixedBody(int clientFd) 
{
    ClientConnection &conn = _clientConnections[clientFd];
    if (conn.bodyReceived >= conn.contentLength) {
        conn.state = READY;
        return true;
    }
    if (!conn.buffer.empty()) {
        conn.body.append(conn.buffer);
        conn.bodyReceived = conn.body.size();
        conn.buffer.clear();
    }
    if (conn.bodyReceived >= conn.contentLength) {
        conn.state = READY;
        return true;
    }
    return false;
}


// Consumes the chunked request body and marks completion when the last chunk arrives.
bool epollManager::consumeChunkedBody(int clientFd) 
{
    ClientConnection &c = _clientConnections[clientFd];
    if (!c.buffer.empty()) {
        c.chunkBuffer.append(c.buffer);
        c.buffer.clear();
    }
    while (true) {
        if (c.chunkState == CHUNK_READ_SIZE) {
            size_t pos = c.chunkBuffer.find("\r\n");
            if (pos == std::string::npos)
                return false;
            std::string sizeLine = c.chunkBuffer.substr(0, pos);
            size_t semi = sizeLine.find(';');
            if (semi != std::string::npos)
                sizeLine = sizeLine.substr(0, semi);
            size_t sz = std::strtoul(sizeLine.c_str(), NULL, 16);
            c.currentChunkSize = sz;
            c.chunkBuffer.erase(0, pos + 2);
            if (sz == 0)
                c.chunkState = CHUNK_COMPLETE;
            else
                c.chunkState = CHUNK_READ_DATA;
        }
        if (c.chunkState == CHUNK_READ_DATA) {
            if (c.chunkBuffer.size() < c.currentChunkSize)
                return false;
            c.body.append(c.chunkBuffer.substr(0, c.currentChunkSize));
            c.chunkBuffer.erase(0, c.currentChunkSize);
            c.chunkState = CHUNK_READ_CRLF;
        }
        if (c.chunkState == CHUNK_READ_CRLF) {
            if (c.chunkBuffer.size() < 2)
                return false;
            if (c.chunkBuffer.substr(0,2) != "\r\n")
                return false;
            c.chunkBuffer.erase(0,2);
            c.chunkState = CHUNK_READ_SIZE;
        }
        if (c.chunkState == CHUNK_COMPLETE) {
            size_t pos = c.chunkBuffer.find("\r\n\r\n");
            if (pos != std::string::npos) {
                c.chunkBuffer.erase(0, pos + 4);
                c.state = READY;
                return true;
             }
            if (c.chunkBuffer.find("\r\n") == 0) { c.chunkBuffer.erase(0, 2); c.state = READY; return true; }
            return false;
        }
    }
}


// Aggregates incoming data and reports when a full HTTP request is ready.
bool epollManager::collectClientRequest(int clientFd) 
{
    ClientConnection &conn = _clientConnections[clientFd];
    const ServerConfig& cfg = _serverForClientFd[clientFd];
    const LocationConfig* location = findLocationConfig(conn.uri.empty() ? "/" : conn.uri, cfg);
    size_t maxBody = getEffectiveClientMax(location, cfg);

    // Enforce max body size early, as data is being received.
    if (maxBody > 0 && (conn.body.size() + conn.buffer.size() + conn.chunkBuffer.size()) > maxBody) {
        queueErrorResponse(clientFd, 413, "Request Entity Too Large");
        return false; // Stop processing
    }

    if (!conn.headersParsed && !parseClientHeaders(clientFd))
        return false;

    if (conn.state == READING_BODY) {
        if (conn.bodyType == BODY_FIXED)
            consumeFixedBody(clientFd);
        else if (conn.bodyType == BODY_CHUNKED)
            consumeChunkedBody(clientFd);
    }
    /* if (conn.cgiRunning)
        return false; */
    return (conn.state == READY);
}


// Builds the Request object for a ready client and dispatches it to the right handler.
void epollManager::handleReadyRequest(int clientFd)
{
    ClientConnection &conn = _clientConnections[clientFd];
    try 
    {
        std::string raw = buildRawHttpRequest(conn);
        Request request(raw);
        if (request.isComplete()) {
            LOG("Request " + request.getMethod() + " " + request.getUri() + " fd=" + toString(clientFd));
            const ServerConfig& cfg = _serverForClientFd[clientFd];
            const LocationConfig* location = findLocationConfig(conn.uri, cfg);
            ensureConnectionSession(conn, request);
            bool wantsCgi = (location && location->isCgiRequest(conn.uri));
            if (wantsCgi) 
            {
                if (!startCgiFor(clientFd, request, cfg, location))
                    queueErrorResponse(clientFd, 502, "Bad Gateway");
            } 
            else 
            {
                Response response = buildResponseForRequest(request, cfg);
                if (conn.keepAlive) 
                {
                    response.setHeader("Connection", "keep-alive");
                    response.setHeader("Keep-Alive", "timeout=5, max=100");
                } 
                else 
                    response.setHeader("Connection", "close");
                attachSessionCookie(response, conn);
                std::string responseStr = response.getResponse();
                std::string statusLine = responseStr.substr(0, responseStr.find("\r\n"));
                LOG("Response " + statusLine + " fd=" + toString(clientFd));
                conn.outBuffer = responseStr; conn.outOffset = 0; conn.hasResponse = true; updateClientInterest(clientFd, true);
            }
            
        } 
        else 
        {
            conn.keepAlive = false;
            queueErrorResponse(clientFd, 400, "Bad Request");
        }
    } catch (...) 
    {
        conn.keepAlive = false;
        queueErrorResponse(clientFd, 400, "Bad Request");
    }
}


// Finds the most specific matching location block for a given URI.
const LocationConfig* epollManager::findLocationConfig(const std::string& uri, const ServerConfig& config) const
{
    const std::vector<LocationConfig>& locations = config.getLocations();
    const LocationConfig* bestMatch = NULL;
    for (size_t i = 0; i < locations.size(); ++i) 
    {
        const LocationConfig& loc = locations[i];
        if (uri.find(loc.getPath()) == 0) 
        {
            if (!bestMatch || loc.getPath().length() > bestMatch->getPath().length()) 
                bestMatch = &loc;
        }
    }
    
    return bestMatch;
}


// Builds the Allow header listing permitted methods for an endpoint.
std::string epollManager::buildAllowHeader(const LocationConfig* location) const 
{
    std::string allow;
    if (location) 
    {
        const std::vector<std::string>& methods = location->getAllowedMethods();
        for (size_t i = 0; i < methods.size(); ++i) { if (i) allow += ", "; allow += methods[i]; }
    }
    if (allow.empty()) 
        allow = "GET, POST, DELETE";
    
    return allow;
}


// Resolves a request URI to a filesystem path respecting location roots.
std::string epollManager::resolveFilePath(const std::string& uri, const ServerConfig& config) const 
{
    const LocationConfig* location = findLocationConfig(uri, config);
    const bool hasLocation = (location != NULL);
    const bool locationHasRoot = (hasLocation && !location->getRoot().empty());

    std::string root = locationHasRoot ? location->getRoot() : config.getRoot();
    std::string pathOnly = uri;
    size_t qpos = pathOnly.find('?');

    if (qpos != std::string::npos)
        pathOnly = pathOnly.substr(0, qpos);

    size_t fpos = pathOnly.find('#');
    if (fpos != std::string::npos)
        pathOnly = pathOnly.substr(0, fpos);
    std::string rel;
    
    if (locationHasRoot) 
    {
        std::string mount = location->getPath();
        rel = pathOnly;
        if (!mount.empty() && rel.find(mount) == 0)
        rel = rel.substr(mount.length());
    }
    else
        rel = pathOnly;
    if (!rel.empty() && rel[0] == '/')
        rel.erase(0,1);
    std::vector<std::string> parts = ParserUtils::split(rel, '/');
    std::vector<std::string> stack;
    for (size_t i = 0; i < parts.size(); ++i) 
    {
        const std::string& seg = parts[i];
        if (seg.empty() || seg == ".")
            continue;
        if (seg == "..") {
            if (stack.empty())
                return ""; 
            stack.pop_back(); 
        }
        else
            stack.push_back(seg);
    }
    std::string full = root;
    if (!full.empty() && full[full.size()-1] != '/') full += "/";
    for (size_t i = 0; i < stack.size(); ++i) { if (i) full += "/"; full += stack[i]; }
    return full;
}


// Resolves an error page path, checking location and server overrides.
std::string epollManager::resolveErrorPagePath(const std::string& candidate, const ServerConfig& config) const {
    if (candidate.empty())
        return "";

    // Direct absolute or relative filesystem path
    if (fileExists(candidate))
        return candidate;

    std::string normalized = candidate;
    if (!normalized.empty() && normalized[0] == '/')
    {
        std::string resolved = resolveFilePath(normalized, config);
        if (!resolved.empty() && fileExists(resolved))
            return resolved;
    }
    else
    {
        std::string root = config.getRoot();
        if (!root.empty())
        {
            std::string rel = normalized;
            if (!rel.empty() && rel[0] == '/')
                rel.erase(0, 1);
            if (rel.compare(0, 2, "./") == 0)
                rel = rel.substr(2);
            std::string combined = root;
            if (!combined.empty() && combined[combined.size() - 1] != '/')
                combined += "/";
            combined += rel;
            if (fileExists(combined))
                return combined;
        }
    }
    return "";
}


// Loads a custom error page from configuration or default repository.
bool epollManager::loadErrorPage(int code, const ServerConfig* config, std::string& body, std::string& contentType) const {
    if (config) {
        std::string candidate = config->getErrorPagePath(code);
        if (!candidate.empty()) {
            std::string path = resolveErrorPagePath(candidate, *config);
            if (path.empty() && fileExists(candidate))
                path = candidate;
            if (!path.empty() && fileExists(path)) {
                body = readFileContent(path);
                contentType = getContentType(path);
                return true;
            }
        }
        const std::string& dir = config->getErrorPageDirectory();
        if (dir.size()) {
            std::string base = dir;
            if (!base.empty() && base[base.size() - 1] != '/')
                base += "/";
            std::string candidatePath = base + toString(code) + ".html";
            std::string path = resolveErrorPagePath(candidatePath, *config);
            if (path.empty() && fileExists(candidatePath))
                path = candidatePath;
            if (!path.empty() && fileExists(path)) {
                body = readFileContent(path);
                contentType = getContentType(path);
                return true;
            }
        }
    }
    std::string defaultPath = std::string("www/defaultPages/error/") + toString(code) + ".html";
    if (fileExists(defaultPath)) {
        body = readFileContent(defaultPath);
        contentType = getContentType(defaultPath);
        return true;
    }
    return false;
}


// Creates an HTTP error response with optional custom page content.
void epollManager::buildErrorResponse(Response& response, int code, const std::string& message, const ServerConfig* config) const 
{
    response.setStatus(code, message);
    response.setHeader("Server", "webserv/1.0");
    response.setHeader("Date", getCurrentDate());

    std::string body;
    std::string contentType;
    if (loadErrorPage(code, config, body, contentType)) {
        response.setHeader("Content-Type", contentType.empty() ? "text/html" : contentType);
        response.setBody(body);
    } else {
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse(toString(code) + " " + message, "Error: " + message + "<br>Please try another URL."));
    }
}


// Checks whether the HTTP method is permitted for the requested resource.
bool epollManager::isMethodAllowed(const std::string& method, const std::string& uri, const ServerConfig& config) const
{
    const LocationConfig* location = findLocationConfig(uri, config);
    if (location) {
        const std::vector<std::string>& allowedMethods = location->getAllowedMethods();
        if (!allowedMethods.empty())
            return std::find(allowedMethods.begin(), allowedMethods.end(), method) != allowedMethods.end();
    }
    return true;
}


// Computes the maximum allowed request body size for the current location.
size_t epollManager::getEffectiveClientMax(const LocationConfig* location, const ServerConfig& config) const 
{
    if (location && location->getClientMax() > 0) return location->getClientMax();
    return config.getClientMax();
}


bool epollManager::isCgiRequest(const std::string& uri, const ServerConfig& config) const 
{
    const LocationConfig* location = findLocationConfig(uri, config);
    if (!location) return false;
    return !location->getCgiPass().empty();
}


// Implements DELETE by removing the targeted resource when permitted.
Response epollManager::handleDelete(const Request& request, const LocationConfig* location, const ServerConfig& config) 
{
    Response response;
    const std::string uri = request.getUri();
    if (location && !location->getCgiPass().empty()) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    std::string path = resolveFilePath(uri, config);
    if (path.empty()) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    if (!fileExists(path)) { buildErrorResponse(response, 404, "Not Found", &config); return response; }
    if (isDirectory(path)) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    if (unlink(path.c_str()) == 0) { response.setStatus(204, "No Content"); response.setBody(""); return response; }
    buildErrorResponse(response, 500, "Internal Server Error", &config); return response;
}


static std::string sanitizeFilename(const std::string& name) 
{
    std::string n; for (size_t i=0;i<name.size();++i) { char c = name[i]; if (c=='/'||c=='\\') continue; if (std::isalnum(static_cast<unsigned char>(c))||c=='.'||c=='-'||c=='_') n+=c; else n+='_'; } if (n.empty()) n = "upload.bin"; return n;
}


// Parses a multipart/form-data payload and persists uploaded files.
bool epollManager::parseMultipartAndSave(const std::string& body, const std::string& boundary,
                                         const std::string& basePath, const std::string& uri,
                                         size_t& savedCount, bool& anyCreated, std::string& lastSavedPath)
{
    savedCount = 0; anyCreated = false; lastSavedPath.clear();
    if (boundary.empty()) return false;
    std::string sep = std::string("--") + boundary; size_t pos = 0; size_t start = body.find(sep, pos); if (start == std::string::npos) return false; pos = start + sep.size();
    while (true) {
        if (pos + 2 > body.size()) break;
        if (body.substr(pos, 2) == "--") break;
        if (body.substr(pos, 2) != "\r\n") return false;
        pos += 2;
        size_t hdrEnd = body.find("\r\n\r\n", pos); if (hdrEnd == std::string::npos) return false; std::string headers = body.substr(pos, hdrEnd - pos); pos = hdrEnd + 4;
        std::string filename;
        size_t cd = headers.find("Content-Disposition:"); if (cd != std::string::npos) {
            size_t fn = headers.find("filename="); if (fn != std::string::npos) { size_t startq = headers.find('"', fn); size_t endq = (startq==std::string::npos)?std::string::npos:headers.find('"', startq+1); if (startq!=std::string::npos && endq!=std::string::npos) filename = headers.substr(startq+1, endq-startq-1); }
        }
        size_t next = body.find(sep, pos); if (next == std::string::npos) return false; std::string content = body.substr(pos, next - pos - 2); pos = next + sep.size();
        std::string dest = basePath; bool isDir = isDirectory(basePath) || (!uri.empty() && uri[uri.size()-1]=='/'); if (isDir) { std::string clean = sanitizeFilename(filename); if (!dest.empty() && dest[dest.size()-1] != '/') dest += "/"; dest += clean; }
        bool existed = fileExists(dest); std::ofstream ofs(dest.c_str(), std::ios::binary); if (!ofs.is_open()) return false; ofs.write(content.c_str(), content.size()); ofs.close(); savedCount += 1; anyCreated = anyCreated || (!existed); lastSavedPath = dest;
    }
    return savedCount > 0;
}


// Handles non-CGI POST requests such as form submissions and uploads.
Response epollManager::handlePost(const Request& request, const LocationConfig* location, const ServerConfig& config) 
{
    Response response; const std::string uri = request.getUri();
    // CGI requests are handled asynchronously in handleReadyRequest via launchCgi
    // This function now only covers non-CGI POST handlers (uploads, file writes, etc.)
    // Determine upload base path: upload_store if set, else resolve from URI
    std::string basePath = (location && !location->getUploadStore().empty()) ? location->getUploadStore() : resolveFilePath(uri, config);
    // Ensure upload dir exists if upload_store is set
    if (location && !location->getUploadStore().empty()) {
        if (!dirExists(basePath)) {
            buildErrorResponse(response, 500, "Internal Server Error", &config);
            return response;
        }
    }
    if (basePath.empty()) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    size_t maxBody = getEffectiveClientMax(location, config);
    if (maxBody > 0 && request.getBody().size() > maxBody) { buildErrorResponse(response, 413, "Request Entity Too Large", &config); return response; }
    std::string ct = request.getHeader("Content-Type"); std::string ctl = ct; for (size_t i=0;i<ctl.size();++i) ctl[i]=std::tolower(static_cast<unsigned char>(ctl[i]));
    bool created = false;
    if (ctl.find("multipart/form-data") == 0) 
    {
        size_t bpos = ctl.find("boundary=");
        if (bpos == std::string::npos) { buildErrorResponse(response, 400, "Bad Request", &config); return response; }
        size_t start = bpos + 9;
        std::string boundary = ct.substr(start);
        size_t scPos = boundary.find(';'); if (scPos != std::string::npos) boundary = boundary.substr(0, scPos);
        while (!boundary.empty() && (boundary[0]==' ' || boundary[0]=='\t')) boundary.erase(0,1);
        while (!boundary.empty() && (boundary[boundary.size()-1]==' ' || boundary[boundary.size()-1]=='\t')) boundary.erase(boundary.size()-1);
        if (!boundary.empty() && boundary[0]=='"') { size_t endq = boundary.find('"', 1); boundary = (endq==std::string::npos)?boundary.substr(1):boundary.substr(1, endq-1); }
        size_t savedCount = 0; bool anyCreated = false; std::string lastPath;
        if (!parseMultipartAndSave(request.getBody(), boundary, basePath, uri, savedCount, anyCreated, lastPath)) { buildErrorResponse(response, 400, "Bad Request", &config); return response; }
        created = anyCreated; response.setStatus(created ? 201 : 200, created ? "Created" : "OK"); response.setHeader("Content-Type","text/html"); response.setHeader("Location", uri); response.setBody(createHtmlResponse(created?"201 Created":"200 OK", toString(savedCount) + " file(s) uploaded")); return response; }
    bool isDir = isDirectory(basePath) || (!uri.empty() && uri[uri.size()-1]=='/');
    if (isDir) 
    {
        buildErrorResponse(response, 400, "Bad Request", &config);
        return response;
    }
    bool existed = fileExists(basePath); std::ofstream ofs(basePath.c_str(), std::ios::binary); if (!ofs.is_open()) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    const std::string& data = request.getBody(); ofs.write(data.c_str(), data.size()); ofs.close(); response.setStatus(existed?200:201, existed?"OK":"Created"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse(existed?"200 OK":"201 Created", existed?"File overwritten":"File created")); return response;
}


// Ensures POST requests supply either a Content-Length, a Transfer-Encoding or an inline body.
bool epollManager::validatePostLengthHeader(const Request& request, Response& response, const ServerConfig& config) const 
{
    std::string contentLength = request.getHeader("Content-Length");
    std::string transferEncoding = request.getHeader("Transfer-Encoding");
    if (contentLength.empty() && transferEncoding.empty() && request.getBody().empty()) {
        buildErrorResponse(response, 411, "Length Required", &config);
        return false;
    }
    return true;
}

// Applies the configured body size limit to POST requests.
bool epollManager::validatePostBodySize(const Request& request, const LocationConfig* location, const ServerConfig& config, Response& response) const 
{
    size_t effectiveMax = getEffectiveClientMax(location, config);
    if (effectiveMax > 0 && request.getBody().length() > effectiveMax) {
        buildErrorResponse(response, 413, "Request Entity Too Large", &config);
        return false;
    }
    return true;
}

// Produces a redirect response when the location block defines a return directive.
bool epollManager::handleConfiguredRedirect(const LocationConfig* location, Response& response, const ServerConfig& config) const 
{
    if (!location || !location->hasReturn())
        return false;
    (void)config;
    int code = location->getReturnCode();
    std::string url = location->getReturnUrl();
    response.setStatus(code, (code == 301 ? "Moved Permanently" : (code == 302 ? "Found" : "Temporary Redirect")));
    response.setHeader("Location", url);
    response.setHeader("Content-Type", "text/html");
    response.setBody(createHtmlResponse(toString(code) + " Redirect", "Redirecting to <a href="" + url + "">" + url + "</a>"));
    return true;
}

// Serves the configured index file when the client requests the root URI.
bool epollManager::tryServeRootIndex(const std::string& uri, const LocationConfig* location, const ServerConfig& config, Response& response) const {
    if (uri != "/" && uri != "/index.html")
        return false;
    std::string indexConf = (location && !location->getIndex().empty()) ? location->getIndex() : config.getIndex();
    std::vector<std::string> indexes = ParserUtils::split(indexConf, ' ');
    for (size_t i = 0; i < indexes.size(); ++i) {
        std::string indexFile = ParserUtils::trim(indexes[i]);
        if (indexFile.empty())
            continue;
        std::string filePath = resolveFilePath("/" + indexFile, config);
        if (filePath.empty() || !fileExists(filePath))
            continue;
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", getContentType(indexFile));
        response.setBody(readFileContent(filePath));
        return true;
    }
    buildErrorResponse(response, 404, "Not Found", &config);
    return true;
}

// Serves files or directory listings for non-root URIs.
bool epollManager::tryServeResourceFromFilesystem(const std::string& uri, const LocationConfig* location, const ServerConfig& config, Response& response) const {
    std::string filePath = resolveFilePath(uri, config);
    if (filePath.empty() || !fileExists(filePath)) {
        buildErrorResponse(response, 404, "Not Found", &config);
        return true;
    }
    if (isDirectory(filePath)) {
        if (location && location->getAutoindex()) {
            response.setStatus(200, "OK");
            response.setHeader("Content-Type", "text/html");
            response.setBody(generateDirectoryListing(filePath, uri));
            return true;
        }
        std::string indexConf = (location && !location->getIndex().empty()) ? location->getIndex() : config.getIndex();
        std::vector<std::string> indexList = ParserUtils::split(indexConf, ' ');
        for (size_t i = 0; i < indexList.size(); ++i) {
            std::string indexFile = ParserUtils::trim(indexList[i]);
            if (indexFile.empty())
                continue;
            std::string indexFilePath = filePath + "/" + indexFile;
            if (!fileExists(indexFilePath))
                continue;
            response.setStatus(200, "OK");
            response.setHeader("Content-Type", getContentType(indexFile));
            response.setBody(readFileContent(indexFilePath));
            return true;
        }
        buildErrorResponse(response, 404, "Not Found", &config);
        return true;
    }
    response.setStatus(200, "OK");
    response.setHeader("Content-Type", getContentType(uri));
    response.setBody(readFileContent(filePath));
    return true;
}

// Adds the standard headers expected on every locally generated response and trims HEAD bodies.
void epollManager::addStandardHeaders(Response& response, const std::string& method) const 
{
    response.setHeader("Server", "webserv");
    response.setHeader("Date", getCurrentDate());
    if (method == "HEAD") {
        size_t len = response.getBodyLength();
        response.setHeader("Content-Length", toString(len));
        response.setBody("");
    }
}


// Routes the request to the correct handler and builds a complete HTTP response.
Response epollManager::buildResponseForRequest(const Request& request, const ServerConfig& config) 
{
    Response response;
    if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") {
        buildErrorResponse(response, 505, "HTTP Version Not Supported", &config);
        return response;
    }

    const std::string method = request.getMethod();
    const std::string uri = request.getUri();
    const LocationConfig* location = findLocationConfig(uri, config);

    if (!isMethodAllowed(method, uri, config)) {
        buildErrorResponse(response, 405, "Method Not Allowed", &config);
        response.setHeader("Allow", buildAllowHeader(location));
        return response;
    }

    if (method == "POST" && !validatePostLengthHeader(request, response, config))
        return response;

    if (handleConfiguredRedirect(location, response, config)) {
        addStandardHeaders(response, method);
        return response;
    }

    if (method == "DELETE")
        return handleDelete(request, location, config);

    if (method == "POST" && (!location || !location->isCgiRequest(uri))) {
        Response postResponse = handlePost(request, location, config);
        return postResponse;
    }

    if (method == "POST" && !validatePostBodySize(request, location, config, response))
        return response;

    if (tryServeRootIndex(uri, location, config, response)) {
        addStandardHeaders(response, method);
        return response;
    }

    if (location && location->isCgiRequest(uri)) {
        buildErrorResponse(response, 500, "Internal Server Error", &config);
        addStandardHeaders(response, method);
        return response;
    }

    if (tryServeResourceFromFilesystem(uri, location, config, response)) {
        addStandardHeaders(response, method);
        return response;
    }

    buildErrorResponse(response, 404, "Not Found", &config);
    addStandardHeaders(response, method);
    return response;
}


// Reads available data from the client socket and advances request parsing.
void epollManager::readClientData(int clientFd, uint32_t events)
{
    (void)events;
    if (_clientConnections.find(clientFd) == _clientConnections.end()) {
        ClientConnection conn;
        conn.fd = clientFd;
        conn.lastActivity = time(NULL);
        _clientConnections[clientFd] = conn;
        if (_clientConnections.size() > MAX_CLIENTS) {
            queueErrorResponse(clientFd, 503, "Service Unavailable");
            return;
        }
    }
    ClientConnection &conn = _clientConnections[clientFd];
   if (_activeCgiCount > MAX_CGI_PROCESS) {
        conn.keepAlive = false;
        queueErrorResponse(clientFd, 503, "Too many CGI requests");
        return;
    }
    if (conn.cgiRunning || conn.cgiPid > 0) {
        return;
    }
    conn.isReading = true; conn.lastActivity = time(NULL);
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead = recv(clientFd, buffer, BUFFER_SIZE, 0);
    conn.keepAlive = false;
    if (bytesRead > 0) {
        conn.buffer.append(buffer, bytesRead);
        if (conn.buffer.size() + conn.body.size() > MAX_REQUEST_SIZE) {
            conn.keepAlive = false;
            queueErrorResponse(clientFd, 413, "Request Entity Too Large"); return; }
        if (!collectClientRequest(clientFd)) { return; }
        handleReadyRequest(clientFd);
        return;
    }
    conn.isReading = false;
    closeClientSocket(clientFd);
    removeClientState(clientFd);
}


// Flushes the pending response buffer to the client socket respecting keep-alive.
void epollManager::flushClientBuffer(int clientFd, uint32_t events)
{
    (void)events;
    std::map<int, ClientConnection>::iterator it = _clientConnections.find(clientFd);
    if (it == _clientConnections.end()) return;

    ClientConnection &conn = it->second;
    if (!conn.hasResponse) return;

    size_t remaining = conn.outBuffer.size() - conn.outOffset;
    if (remaining == 0) 
    {
        updateClientInterest(clientFd, false);
        if (conn.keepAlive) {
            resetClientState(conn);
            conn.lastActivity = time(NULL);
        } else {
            closeClientSocket(clientFd);
            removeClientState(clientFd);
        }
        return;
    }

    size_t toSend = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;
    ssize_t n = send(clientFd, conn.outBuffer.data() + conn.outOffset, toSend, 0);

    if (n > 0) 
    {
        conn.outOffset += static_cast<size_t>(n);
        if (conn.outOffset >= conn.outBuffer.size()) {
            LOG("Response sent to client " + toString(clientFd));
            updateClientInterest(clientFd, false); // cut the writing
            if (conn.keepAlive) {
            resetClientState(conn);
                conn.lastActivity = time(NULL);
            } else {
                closeClientSocket(clientFd);
                removeClientState(clientFd);
            }
        }
        return;
    }

    LOG("send() failed or connection closed for client " + toString(clientFd) + ", closing socket");
    updateClientInterest(clientFd, false);
    closeClientSocket(clientFd);
    removeClientState(clientFd);
}


// Schedules an error response to be written back to the client.
void epollManager::queueErrorResponse(int clientFd, int code, const std::string& message) 
{
    ClientConnection &conn = _clientConnections[clientFd];
    conn.keepAlive = false;
    Response response;

    const ServerConfig* cfgPtr = NULL;
    std::map<int, ServerConfig>::iterator sit = _serverForClientFd.find(clientFd);
    if (sit != _serverForClientFd.end())
        cfgPtr = &sit->second;

    buildErrorResponse(response, code, message, cfgPtr);
    response.setHeader("Connection", "close");
    attachSessionCookie(response, conn);
    std::string responseStr = response.getResponse();
    std::string statusLine = responseStr.substr(0, responseStr.find("\r\n"));
    LOG("Response ready: " + statusLine + " (" + toString(responseStr.size()) + " bytes)");
    conn.outBuffer = responseStr; conn.outOffset = 0; conn.hasResponse = true; updateClientInterest(clientFd, true);
}


// Reaps terminated CGI children without blocking the main loop.
void epollManager::reapZombies()
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (_activeCgiCount > 0)
            _activeCgiCount--;

    for (std::map<int, ClientConnection>::iterator it = _clientConnections.begin();
         it != _clientConnections.end(); ++it)
        {
        ClientConnection &conn = it->second;
        if (conn.cgiPid == pid)
        {
            conn.cgiPid = -1;
            conn.cgiRunning = false;
            break;
        }
        }
    }
    // If no children exist, no error
    if (pid == -1 && errno != ECHILD)
    {
        ERROR_SYS("waitpid failed in reapZombies()");
    }
}


// Main epoll loop that dispatches events and maintains the server state.
void epollManager::run()
{
    struct epoll_event events[MAX_EVENTS];
    while (_running)
    {
        int num = epoll_wait(_epollFd, events, MAX_EVENTS, 1000);
        if (num < 0) {
            if (errno == EINTR) {
                cleanupInactiveConnections();
                if (!_running)
                    break;
                continue;
            }
            ERROR_SYS("epoll_wait");
            cleanupInactiveConnections();
            continue;
        }
        if (num == 0) {
            cleanupInactiveConnections();
            continue;
        }
        cleanupInactiveConnections();
        for (int i = 0; i < num; ++i)
        {
            int fd = events[i].data.fd;
            if (_listenSockets.find(fd) != _listenSockets.end())
                acceptPendingConnections(fd);
            else if (_cgiOutToClient.find(fd) != _cgiOutToClient.end())
                drainCgiOutput(fd, events[i].events);
            else if (_cgiInToClient.find(fd) != _cgiInToClient.end())
                feedCgiInput(fd, events[i].events);
            else {
                if (events[i].events & EPOLLIN)
                    readClientData(fd, events[i].events);
                if (events[i].events & EPOLLOUT)
                    flushClientBuffer(fd, events[i].events);
            }
        }
        reapZombies();
    }
}


// Updates epoll interest for a client socket, optionally enabling EPOLLOUT.
void epollManager::updateClientInterest(int clientFd, bool enable)
{
    struct epoll_event ev; 
    ev.data.fd = clientFd; 
    ev.events = EPOLLIN;
    if (enable)
        ev.events |= EPOLLOUT;
    if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &ev) == -1)
        ERROR_SYS("epoll_ctl mod client");
}


// Removes all bookkeeping for a client after the socket is closed.
void epollManager::removeClientState(int clientFd)
{
    _clientBuffers.erase(clientFd);
    _clientConnections.erase(clientFd);
    _serverForClientFd.erase(clientFd);
}


// Closes the client socket and tears down any associated CGI resources.
void epollManager::closeClientSocket(int clientFd)
{
    if (_clientConnections.find(clientFd) == _clientConnections.end()) { return; }

    std::map<int, ClientConnection>::iterator it = _clientConnections.find(clientFd);
    if (it != _clientConnections.end()) {
        ClientConnection& c = it->second;
        if (c.cgiRunning) {
            if (c.cgiPid > 0){
                kill(c.cgiPid, SIGKILL);
                waitpid(c.cgiPid, NULL, 0);
                c.cgiPid = -1;
                if (_activeCgiCount > 0)
                    --_activeCgiCount;
            }
            if (c.cgiInFd != -1) {
                if (_epollFd != -1)
                    epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiInFd, NULL);
                close(c.cgiInFd);
                _cgiInToClient.erase(c.cgiInFd);
                c.cgiInFd = -1;
                c.cgiRunning = false;
            }
            if (c.cgiOutFd != -1) {
                if (_epollFd != -1)
                    epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiOutFd, NULL);
                close(c.cgiOutFd);
                _cgiOutToClient.erase(c.cgiOutFd);
                c.cgiOutFd = -1;
                c.cgiRunning = false;
            }
        }
    }
    if (_epollFd != -1)
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
    close(clientFd);
    _clientConnections.erase(it);
}


void epollManager::armWriteEvent(int clientFd, bool enable)
{
    struct epoll_event ev; 
    ev.data.fd = clientFd; 
    ev.events = EPOLLIN;
    if (enable)
        ev.events |= EPOLLOUT;
    if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &ev) == -1)
        ERROR_SYS("epoll_ctl mod client");
}
