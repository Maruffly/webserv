#include "epollManager.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/ServerConfig.hpp"
#include "../utils/Utils.hpp"
#include "../utils/ParserUtils.hpp"
#include <signal.h>
#include <sys/wait.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "Cookie.hpp"

void resetConnectionState(ClientConnection& conn)
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

epollManager::epollManager(const std::vector<int>& listenFds, const std::vector< std::vector<ServerConfig> >& serverGroups)
    : _epollFd(-1)
    , _running(true)
    , _activeCgiCount(0)
{
    _lastCleanup = time(NULL);
    _epollFd = epoll_create1(0);
    if (_epollFd == -1)
        throw std::runtime_error("epoll_create1 failed");
    LOG("epoll instance created");

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
    LOG("All server sockets added to epoll");
}

epollManager::~epollManager()
{
    _running = false;
    std::vector<int> fds;
    for (std::map<int, ClientConnection>::iterator it = _clientConnections.begin();
        it != _clientConnections.end(); ++it)
        fds.push_back(it->first);
    for (size_t i = 0; i < fds.size(); ++i)
        closeClient(fds[i]);
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

void epollManager::requestStop()
{
    _running = false;
}

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
                closeClient(clientFd);
                purgeClient(clientFd);
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
        bool erased = false;
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
            sendErrorResponse(c.fd, 504, "Gateway Timeout");
        } else if ((!c.headersParsed || c.state == READING_BODY) && idle > READ_TIMEOUT) {
            if (!c.hasResponse) {
                c.keepAlive = false;
                sendErrorResponse(c.fd, 408, "Request Timeout");
            }
        }
        if (!erased) ++it;
    }
}

void epollManager::handleNewConnection(int listenFd)
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
        char ipbuf[INET_ADDRSTRLEN]; //to check
        inet_ntop(AF_INET, &clientAddress.sin_addr, ipbuf, INET_ADDRSTRLEN);
        newConn.remoteAddr = ipbuf;
        newConn.remotePort = ntohs(clientAddress.sin_port); //to check
        _clientConnections[clientSocket] = newConn;
        _clientBuffers[clientSocket].clear();
        // Default to first server of the group for this listen fd
        if (_serverGroups.find(listenFd) != _serverGroups.end() && !_serverGroups[listenFd].empty())
            _serverForClientFd[clientSocket] = _serverGroups[listenFd][0];
    }
    // EAGAIN acceptable when drained
}

bool epollManager::parseHeadersFor(int clientFd) {
    ClientConnection &conn = _clientConnections[clientFd];
    size_t pos = conn.buffer.find("\r\n\r\n");
    if (pos == std::string::npos)
        return false;
    std::string headersPart = conn.buffer.substr(0, pos);
    std::string after = conn.buffer.substr(pos + 4);
    size_t eol = headersPart.find("\r\n");
    if (eol == std::string::npos)
        return false;
    std::string start = headersPart.substr(0, eol);
    size_t sp1 = start.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : start.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos)
        return false;
    conn.method = start.substr(0, sp1);
    conn.uri = start.substr(sp1 + 1, sp2 - sp1 - 1);
    conn.version = start.substr(sp2 + 1);

    size_t lineStart = eol + 2;
    while (lineStart < headersPart.size()) {
        size_t lineEnd = headersPart.find("\r\n", lineStart);
        if (lineEnd == std::string::npos)
            break;
        std::string line = headersPart.substr(lineStart, lineEnd - lineStart);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0,1);
            while (!name.empty() && (name[name.size()-1] == ' ' || name[name.size()-1] == '\t')) name.erase(name.size()-1);
            for (size_t i=0;i<name.size();++i)
                name[i] = std::tolower(name[i]);
            conn.headers[name] = value;
        }
        lineStart = lineEnd + 2;
    }

    std::string te;
    if (conn.headers.find("transfer-encoding") != conn.headers.end())
        te = conn.headers["transfer-encoding"];
    std::string cl;
    if (conn.headers.find("content-length") != conn.headers.end())
        cl = conn.headers["content-length"];
    if (!te.empty()) {
        std::string tel = te;
        for (size_t i=0;i<tel.size();++i)
            tel[i]=std::tolower(tel[i]);
        if (tel.find("chunked") != std::string::npos) {
            conn.bodyType = BODY_CHUNKED;
            conn.chunkState = CHUNK_READ_SIZE;
        }
    }
    if (conn.bodyType != BODY_CHUNKED) {
        if (!cl.empty()) {
            conn.contentLength = static_cast<size_t>(std::strtoul(cl.c_str(), NULL, 10));
            conn.bodyType = (conn.contentLength > 0) ? BODY_FIXED : BODY_NONE;
        }
        else
            conn.bodyType = BODY_NONE;
    }
    conn.buffer.clear();
    if (conn.bodyType == BODY_FIXED) {
        conn.body.append(after);
        conn.bodyReceived = conn.body.size();
    }
    else if (conn.bodyType == BODY_CHUNKED)
        conn.chunkBuffer.append(after);

    conn.headersParsed = true;
    // Select vhost according to Host header, defaulting to first
    std::map<int, std::vector<ServerConfig> >::iterator git = _serverGroups.find(conn.listenFd);
    if (git != _serverGroups.end()) {
        const std::vector<ServerConfig>& group = git->second;
        if (!group.empty()) {
            std::string hostHeader;
            if (conn.headers.find("host") != conn.headers.end())
                hostHeader = conn.headers["host"];
            size_t colon = hostHeader.find(':');
            std::string hostname = (colon == std::string::npos) ? hostHeader : hostHeader.substr(0, colon);
            hostname = ParserUtils::trim(hostname);
            std::string hostnameLower = hostname;
            for (size_t i = 0; i < hostnameLower.size(); ++i)
                hostnameLower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(hostnameLower[i])));

            const ServerConfig* chosen = &group[0];
            if (!hostnameLower.empty()) {
                for (size_t i=0;i<group.size();++i) {
                    const std::vector<std::string>& aliases = group[i].getServerNames();
                    for (size_t j = 0; j < aliases.size(); ++j) {
                        std::string aliasLower = aliases[j];
                        for (size_t k = 0; k < aliasLower.size(); ++k)
                            aliasLower[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(aliasLower[k])));
                        if (aliasLower == hostnameLower) {
                            chosen = &group[i];
                            goto host_selected;
                        }
                    }
                    if (aliases.empty()) {
                        std::string hostLower = group[i].getHost();
                        for (size_t k = 0; k < hostLower.size(); ++k)
                            hostLower[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(hostLower[k])));
                        if (!hostLower.empty() && hostLower == hostnameLower) {
                            chosen = &group[i];
                            goto host_selected;
                        }
                    }
                }
            }
host_selected:
            _serverForClientFd[conn.fd] = *chosen;
        }
    }
    std::string connectionValue;
    std::map<std::string, std::string>::iterator connIt = conn.headers.find("connection");
    if (connIt != conn.headers.end()) connectionValue = connIt->second;
    std::string connectionLower = connectionValue;
    for (size_t i = 0; i < connectionLower.size(); ++i) connectionLower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(connectionLower[i])));
    bool explicitClose = connectionLower.find("close") != std::string::npos;
    bool explicitKeep = connectionLower.find("keep-alive") != std::string::npos;
    std::string versionUpper = conn.version;
    for (size_t i = 0; i < versionUpper.size(); ++i) versionUpper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(versionUpper[i])));
    if (!explicitClose) {
        if (versionUpper == "HTTP/1.1") conn.keepAlive = true;
        else if (versionUpper == "HTTP/1.0") conn.keepAlive = explicitKeep;
        else conn.keepAlive = explicitKeep;
    } else {
        conn.keepAlive = false;
    }

    conn.state = (conn.bodyType == BODY_NONE) ? READY : READING_BODY;
    return true;
}

bool epollManager::processFixedBody(int clientFd) {
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

bool epollManager::processChunkedBody(int clientFd) {
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

bool epollManager::processConnectionData(int clientFd) {
    ClientConnection &conn = _clientConnections[clientFd];
    const ServerConfig& cfg = _serverForClientFd[clientFd];
    const LocationConfig* location = findLocationConfig(conn.uri.empty() ? "/" : conn.uri, cfg);
    size_t maxBody = getEffectiveClientMax(location, cfg);
    if (!conn.headersParsed)
        parseHeadersFor(clientFd);
    if (conn.state == READING_BODY) {
        if (conn.bodyType == BODY_FIXED) {
            if (maxBody > 0 && conn.body.size() > maxBody)
            return true;
            processFixedBody(clientFd);
        }
        else if (conn.bodyType == BODY_CHUNKED) {
            if (!processChunkedBody(clientFd))
            return false;
            if (maxBody > 0 && conn.body.size() > maxBody)
                return true;
        }
    }
    /* if (conn.cgiRunning)
        return false; */
    return (conn.state == READY);
}

void epollManager::processReadyRequest(int clientFd)
{
    ClientConnection &conn = _clientConnections[clientFd];
    try {
        std::string raw;
        raw += conn.method + " " + conn.uri + " " + conn.version + "\r\n";
        for (std::map<std::string,std::string>::const_iterator it = conn.headers.begin(); it != conn.headers.end(); ++it) {
            if (it->first == "transfer-encoding" || it->first == "content-length") continue;
            raw += it->first + ": " + it->second + "\r\n";
        }
        if (!conn.body.empty()) raw += std::string("Content-Length: ") + toString(conn.body.size()) + "\r\n";
        raw += "\r\n";
        raw += conn.body;
        Request request(raw);
        if (request.isComplete()) {
            LOG("Request " + request.getMethod() + " " + request.getUri() + " fd=" + toString(clientFd));
            const ServerConfig& cfg = _serverForClientFd[clientFd];
            const LocationConfig* location = findLocationConfig(conn.uri, cfg);
            ensureSessionFor(conn, request);
            bool wantsCgi = (location && location->isCgiRequest(conn.uri));
            if (wantsCgi) {
                if (!startCgiFor(clientFd, request, cfg, location)){
                    sendErrorResponse(clientFd, 502, "Bad Gateway");
                }
            } else {
                Response response = createResponseForRequest(request, cfg);
                if (conn.keepAlive) {
                    response.setHeader("Connection", "keep-alive");
                    response.setHeader("Keep-Alive", "timeout=5, max=100");
                } else {
                    response.setHeader("Connection", "close");
                }
                attachSessionCookie(response, conn);
                std::string responseStr = response.getResponse();
                std::string statusLine = responseStr.substr(0, responseStr.find("\r\n"));
                LOG("Response " + statusLine + " fd=" + toString(clientFd));
                conn.outBuffer = responseStr; conn.outOffset = 0; conn.hasResponse = true; armWriteEvent(clientFd, true);
            }
            
        } else {
            conn.keepAlive = false;
            sendErrorResponse(clientFd, 400, "Bad Request");
        }
    } catch (...) {
        conn.keepAlive = false;
        sendErrorResponse(clientFd, 400, "Bad Request");
    }
}

const LocationConfig* epollManager::findLocationConfig(const std::string& uri, const ServerConfig& config) const
{
    const std::vector<LocationConfig>& locations = config.getLocations();
    const LocationConfig* bestMatch = NULL;
    for (size_t i = 0; i < locations.size(); ++i) {
        const LocationConfig& loc = locations[i];
        if (uri.find(loc.getPath()) == 0) {
            if (!bestMatch || loc.getPath().length() > bestMatch->getPath().length()) bestMatch = &loc;
        }
    }
    
    return bestMatch;
}

std::string epollManager::buildAllowHeader(const LocationConfig* location) const {
    std::string allow;
    if (location) {
        const std::vector<std::string>& methods = location->getAllowedMethods();
        for (size_t i = 0; i < methods.size(); ++i) { if (i) allow += ", "; allow += methods[i]; }
    }
    if (allow.empty()) allow = "GET, POST, DELETE";
    return allow;
}

std::string epollManager::resolveFilePath(const std::string& uri, const ServerConfig& config) const {
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
    if (locationHasRoot) {
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
    for (size_t i = 0; i < parts.size(); ++i) {
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

void epollManager::buildErrorResponse(Response& response, int code, const std::string& message, const ServerConfig* config) const {
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

bool epollManager::isMethodAllowed(const std::string& method, const std::string& uri, const ServerConfig& config) const
{
    const LocationConfig* location = findLocationConfig(uri, config);
    if (location) {
        const std::vector<std::string>& allowedMethods = location->getAllowedMethods();
        if (!allowedMethods.empty()) {
            if (std::find(allowedMethods.begin(), allowedMethods.end(), method) != allowedMethods.end()) return true;
            // Treat HEAD as allowed when GET is allowed
            if (method == "HEAD" && std::find(allowedMethods.begin(), allowedMethods.end(), std::string("GET")) != allowedMethods.end()) return true;
            return false;
        }
    }
    return true;
}

size_t epollManager::getEffectiveClientMax(const LocationConfig* location, const ServerConfig& config) const {
    if (location && location->getClientMax() > 0) return location->getClientMax();
    return config.getClientMax();
}

bool epollManager::isCgiRequest(const std::string& uri, const ServerConfig& config) const {
    const LocationConfig* location = findLocationConfig(uri, config);
    if (!location) return false;
    return !location->getCgiPass().empty();
}

Response epollManager::handleDelete(const Request& request, const LocationConfig* location, const ServerConfig& config) {
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

static std::string sanitizeFilename(const std::string& name) {
    std::string n; for (size_t i=0;i<name.size();++i) { char c = name[i]; if (c=='/'||c=='\\') continue; if (std::isalnum(static_cast<unsigned char>(c))||c=='.'||c=='-'||c=='_') n+=c; else n+='_'; } if (n.empty()) n = "upload.bin"; return n;
}

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

Response epollManager::handlePost(const Request& request, const LocationConfig* location, const ServerConfig& config) {
    Response response; const std::string uri = request.getUri();
    // CGI requests are handled asynchronously in processReadyRequest via startCgiFor
    // This function now only covers non-CGI POST handlers (uploads, file writes, etc.)
    // Determine upload base path: upload_store if set, else resolve from URI
    std::string basePath = (location && !location->getUploadStore().empty()) ? location->getUploadStore() : resolveFilePath(uri, config);
    // Ensure upload dir exists if upload_store is set
    if (location && !location->getUploadStore().empty()) {
        if (!ensureDirectoryExists(basePath, location->getUploadCreateDirs())) {
            buildErrorResponse(response, 403, "Forbidden", &config);
            return response;
        }
    }
    if (basePath.empty()) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    size_t maxBody = getEffectiveClientMax(location, config);
    if (maxBody > 0 && request.getBody().size() > maxBody) { buildErrorResponse(response, 413, "Request Entity Too Large", &config); return response; }
    std::string ct = request.getHeader("Content-Type"); std::string ctl = ct; for (size_t i=0;i<ctl.size();++i) ctl[i]=std::tolower(static_cast<unsigned char>(ctl[i]));
    bool created = false;
    if (ctl.find("multipart/form-data") == 0) {
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
    if (isDir) {
        buildErrorResponse(response, 400, "Bad Request", &config);
        return response;
    }
    bool existed = fileExists(basePath); std::ofstream ofs(basePath.c_str(), std::ios::binary); if (!ofs.is_open()) { buildErrorResponse(response, 403, "Forbidden", &config); return response; }
    const std::string& data = request.getBody(); ofs.write(data.c_str(), data.size()); ofs.close(); response.setStatus(existed?200:201, existed?"OK":"Created"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse(existed?"200 OK":"201 Created", existed?"File overwritten":"File created")); return response;
}

Response epollManager::createResponseForRequest(const Request& request, const ServerConfig& config) {
    Response response;
    if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") { buildErrorResponse(response, 505, "HTTP Version Not Supported", &config); return response; }
    std::string method = request.getMethod(); std::string uri = request.getUri(); const LocationConfig* location = findLocationConfig(uri, config);
    if (!isMethodAllowed(method, uri, config)) { buildErrorResponse(response, 405, "Method Not Allowed", &config); response.setHeader("Allow", buildAllowHeader(location)); return response; }

    // 411 for POST without length or transfer-encoding
    if (method == "POST") {
        std::string cl = request.getHeader("Content-Length");
        std::string te = request.getHeader("Transfer-Encoding");
        if (cl.empty() && te.empty() && request.getBody().empty()) {
            buildErrorResponse(response, 411, "Length Required", &config);
            return response;
        }
    }

    // Redirection (return 3xx) at location level
    if (location && location->hasReturn()) {
        int code = location->getReturnCode();
        std::string url = location->getReturnUrl();
        response.setStatus(code, (code==301?"Moved Permanently":(code==302?"Found":"Temporary Redirect")));
        response.setHeader("Location", url);
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse(toString(code) + " Redirect", "Redirecting to <a href=\"" + url + "\">" + url + "</a>"));
        // Apply HEAD stripping below
        goto finalize;
    }
    if (method == "DELETE") { return handleDelete(request, location, config); }
    if (method == "POST" && (!location || !location->isCgiRequest(uri))) { return handlePost(request, location, config); }
    if (method == "POST") {
        // Enforce effective body max at routing time too
        size_t effectiveMax = getEffectiveClientMax(location, config);
        if (effectiveMax > 0 && request.getBody().length() > effectiveMax) {
            buildErrorResponse(response, 413, "Request Entity Too Large", &config);
            return response;
        }
    }

    if (uri == "/" || uri == "/index.html") {
        // Prefer location index if available for '/'
        std::string indexConf = (location && !location->getIndex().empty()) ? location->getIndex() : config.getIndex();
        std::vector<std::string> indexList = ParserUtils::split(indexConf, ' '); bool indexFound = false;
        for (size_t i = 0; i < indexList.size() && !indexFound; ++i) { std::string indexFile = ParserUtils::trim(indexList[i]); if (!indexFile.empty()) { std::string filePath = resolveFilePath("/" + indexFile, config); if (!filePath.empty() && fileExists(filePath)) { response.setStatus(200, "OK"); response.setHeader("Content-Type", getContentType(indexFile)); response.setBody(readFileContent(filePath)); indexFound = true; } } }
        if (!indexFound) {
            buildErrorResponse(response, 404, "Not Found", &config);
            goto finalize;
        }
    } else if (location && location->isCgiRequest(uri)) {
        // CGI responses are produced asynchronously via startCgiFor; this path should not be reached
        buildErrorResponse(response, 500, "Internal Server Error", &config);
    } else {
        std::string filePath = resolveFilePath(uri, config);
        if (!filePath.empty() && fileExists(filePath)) {
            if (isDirectory(filePath)) {
                if (location && location->getAutoindex()) { response.setStatus(200, "OK"); response.setHeader("Content-Type","text/html"); response.setBody(generateDirectoryListing(filePath, uri)); }
                else {
                    // Prefer location index if defined, else server index
                    std::string indexConf = (location && !location->getIndex().empty()) ? location->getIndex() : config.getIndex();
                    std::vector<std::string> indexList = ParserUtils::split(indexConf, ' '); bool indexFound = false;
                    for (size_t i = 0; i < indexList.size() && !indexFound; ++i) { std::string indexFile = ParserUtils::trim(indexList[i]); if (!indexFile.empty()) { std::string indexFilePath = filePath + "/" + indexFile; if (fileExists(indexFilePath)) { response.setStatus(200, "OK"); response.setHeader("Content-Type", getContentType(indexFile)); response.setBody(readFileContent(indexFilePath)); indexFound = true; } } }
                    if (!indexFound) {
                        buildErrorResponse(response, 403, "Forbidden", &config);
                        goto finalize;
                    }
                }
            } else { response.setStatus(200, "OK"); response.setHeader("Content-Type", getContentType(uri)); response.setBody(readFileContent(filePath)); }
        } else {
            buildErrorResponse(response, 404, "Not Found", &config);
            goto finalize;
        }
    }
finalize:
    // Common headers
    response.setHeader("Server", "webserv/1.0"); response.setHeader("Date", getCurrentDate());
    // HEAD: same headers as GET, no body
    if (method == "HEAD") {
        size_t len = response.getBodyLength();
        response.setHeader("Content-Length", toString(len));
        response.setBody("");
    }
    return response;
}

void epollManager::handleClientRead(int clientFd, uint32_t events)
{
    (void)events;
    if (_clientConnections.find(clientFd) == _clientConnections.end()) {
        ClientConnection conn;
        conn.fd = clientFd;
        conn.lastActivity = time(NULL);
        _clientConnections[clientFd] = conn;
        if (_clientConnections.size() > MAX_CLIENTS) {
            sendErrorResponse(clientFd, 503, "Service Unavailable");
            return;
        }
    }
    ClientConnection &conn = _clientConnections[clientFd];
   if (_activeCgiCount > MAX_CGI_PROCESS) {
        conn.keepAlive = false;
        sendErrorResponse(clientFd, 503, "Too many CGI requests");
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
            sendErrorResponse(clientFd, 413, "Request Entity Too Large"); return; }
        if (!processConnectionData(clientFd)) { return; }
        processReadyRequest(clientFd);
        return;
    }
    conn.isReading = false;
    closeClient(clientFd);
    purgeClient(clientFd);
}

void epollManager::handleClientWrite(int clientFd, uint32_t events)
{
    (void)events;
    std::map<int, ClientConnection>::iterator it = _clientConnections.find(clientFd);
    if (it == _clientConnections.end()) return;

    ClientConnection &conn = it->second;
    if (!conn.hasResponse) return;

    size_t remaining = conn.outBuffer.size() - conn.outOffset;
    if (remaining == 0) 
    {
        armWriteEvent(clientFd, false);
        if (conn.keepAlive) {
            resetConnectionState(conn);
            conn.lastActivity = time(NULL);
        } else {
            closeClient(clientFd);
            purgeClient(clientFd);
        }
        return;
    }

    size_t toSend = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;
    ssize_t n = send(clientFd, conn.outBuffer.data() + conn.outOffset, toSend, 0);

    if (n > 0) 
    {
        LOG("Sent " + toString(n) + " bytes to client " + toString(clientFd));
        conn.outOffset += static_cast<size_t>(n);
        if (conn.outOffset >= conn.outBuffer.size()) {
            LOG("Response sent to client " + toString(clientFd));
            armWriteEvent(clientFd, false); // cut the writing
            if (conn.keepAlive) {
                resetConnectionState(conn);
                conn.lastActivity = time(NULL);
            } else {
                closeClient(clientFd);
                purgeClient(clientFd);
            }
        }
        return;
    }

    LOG("send() failed or connection closed for client " + toString(clientFd) + ", closing socket");
    armWriteEvent(clientFd, false);
    closeClient(clientFd);
    purgeClient(clientFd);
}

void epollManager::sendErrorResponse(int clientFd, int code, const std::string& message) 
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
    conn.outBuffer = responseStr; conn.outOffset = 0; conn.hasResponse = true; armWriteEvent(clientFd, true);
}

void epollManager::reapZombies()
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (_activeCgiCount > 0)
            _activeCgiCount--;

    // Debug log facultatif, utile en phase de test
    LOG("Reaped CGI child PID=" + toString(pid));
    // Si tu veux, tu peux parcourir _clientConnections
    // pour marquer la connexion correspondante comme terminée :
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
    // Si aucun enfant n’existe, ce n’est pas une erreur
    if (pid == -1 && errno != ECHILD)
    {
        ERROR_SYS("waitpid failed in reapZombies()");
    }
}

void epollManager::run()
{
    struct epoll_event events[MAX_EVENTS];
    INFO("Demarrage de la boucle epoll unifiee...");
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
                handleNewConnection(fd);
            else if (_cgiOutToClient.find(fd) != _cgiOutToClient.end())
                handleCgiOutEvent(fd, events[i].events);
            else if (_cgiInToClient.find(fd) != _cgiInToClient.end())
                handleCgiInEvent(fd, events[i].events);
            else {
                if (events[i].events & EPOLLIN)
                    handleClientRead(fd, events[i].events);
                if (events[i].events & EPOLLOUT)
                    handleClientWrite(fd, events[i].events);
            }
        }
        reapZombies();
    }
    INFO("Boucle epoll arretee proprement");
}

void epollManager::armWriteEvent(int clientFd, bool enable)
{
    struct epoll_event ev; 
    ev.data.fd = clientFd; 
    ev.events = EPOLLIN;
    /* if (_clientConnections.find(clientFd) == _clientConnections.end()) {
        return;
    } */
    if (enable)
        ev.events |= EPOLLOUT;
    if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &ev) == -1)
        ERROR_SYS("epoll_ctl mod client");
}

/* void epollManager::handleCgiInEvent(int pipeFd, uint32_t events)
{
    (void)events;
    int clientFd = _cgiInToClient[pipeFd];
    ClientConnection &conn = _clientConnections[clientFd];
    if (conn.body.empty())
    {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        close(pipeFd); _cgiInToClient.erase(pipeFd);
        conn.cgiInFd = -1;
        return;
    }
    size_t remaining = conn.body.size() - conn.cgiInOffset;
    if (remaining == 0)
    {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
        close(pipeFd); _cgiInToClient.erase(pipeFd);
        conn.cgiInFd = -1;
        return;
    }
    size_t toWrite = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining;
    ssize_t w = write(pipeFd, conn.body.data() + conn.cgiInOffset, toWrite);

    if (w > 0) 
    {
        conn.cgiInOffset += static_cast<size_t>(w);
        conn.lastActivity = time(NULL);
        if (conn.cgiInOffset >= conn.body.size()) {
            epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
            close(pipeFd);
            _cgiInToClient.erase(pipeFd);
            conn.cgiInFd = -1;
        }
        return;
    }
    if (w == -1) { 
        int err = errno;
        // Pipe temporarily full, wait for next EPOLLOUT // Do nothing — we stay registered for EPOLLOUT and retry later.
        if (err == EAGAIN || err == EWOULDBLOCK)
            return;
        if (err == EINTR) //  caller can retry later (we don't kill CGI)
            return; // Other errors are fatal
    }
    LOG("write() vers le CGI échoué, fermeture de la connexion client " + toString(clientFd));
    if (conn.cgiPid > 0) 
    {
        kill(conn.cgiPid, SIGKILL);
        conn.cgiPid = -1;
    }

    if (conn.cgiOutFd != -1) 
    {
        epoll_ctl(_epollFd, EPOLL_CTL_DEL, conn.cgiOutFd, NULL);
        close(conn.cgiOutFd);
        _cgiOutToClient.erase(conn.cgiOutFd);
        conn.cgiOutFd = -1;
    }

    epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL);
    close(pipeFd);
    _cgiInToClient.erase(pipeFd);
    conn.cgiInFd = -1;
    conn.cgiRunning = false;
    sendErrorResponse(clientFd, 500, "Internal Server Error");
} */


// Extrait : Remplacer/mettre à jour la logique d'écriture vers le pipe stdin du CGI
// (fonction handleCgiInEvent ou équivalent dans epollManager.cpp)

void epollManager::purgeClient(int clientFd)
{
    _clientBuffers.erase(clientFd);
    _clientConnections.erase(clientFd);
    _serverForClientFd.erase(clientFd);
}

void epollManager::closeClient(int clientFd)
{
    if (_clientConnections.find(clientFd) == _clientConnections.end()){
        //std::cout << "BONJOURRRRRRRRR" << std::endl;
        return;
    }
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
