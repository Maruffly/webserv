#include "epollManager.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/ServerConfig.hpp"
#include "../utils/Utils.hpp"
#include "../utils/ParserUtils.hpp"
#include "../cgi/CgiHandler.hpp"
#include <signal.h>
#include <sys/wait.h>
#include <fstream>

epollManager::epollManager(const std::vector<int>& listenFds, const std::vector< std::vector<ServerConfig> >& serverGroups)
    : _epollFd(-1)
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
    if (_epollFd != -1)
        close(_epollFd);
    std::map<int, std::string>::iterator bufIt;
    for (bufIt = _clientBuffers.begin(); bufIt != _clientBuffers.end(); ++bufIt) {
        close(bufIt->first);
    }
    _clientBuffers.clear();
    _clientConnections.clear();
}

void epollManager::cleanupIdleConnections() {
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
                closeClient(clientFd);
                _clientConnections.erase(connIt);
                std::map<int, std::string>::iterator toErase = bufIt++;
                _clientBuffers.erase(toErase);
                closedCount++;
                continue;
            }
        }
        bufIt++;
    }
    if (closedCount > 0) LOG("Closed " + toString(closedCount) + " idle connections");

    // CGI timeouts
    for (std::map<int, ClientConnection>::iterator it = _clientConnections.begin(); it != _clientConnections.end(); ++it) {
        ClientConnection& c = it->second;
        if (c.cgiRunning && c.cgiStart && difftime(now, c.cgiStart) > CGI_TIMEOUT) {
            if (c.cgiPid > 0) kill(c.cgiPid, SIGKILL);
            if (c.cgiInFd != -1) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiInFd, NULL); close(c.cgiInFd); _cgiInToClient.erase(c.cgiInFd); c.cgiInFd = -1; }
            if (c.cgiOutFd != -1) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiOutFd, NULL); close(c.cgiOutFd); _cgiOutToClient.erase(c.cgiOutFd); c.cgiOutFd = -1; }
            c.cgiRunning = false;
            sendErrorResponse(c.fd, 504, "Gateway Timeout");
        }
    }
}

void epollManager::handleNewConnection(int listenFd)
{
    struct sockaddr_in clientAddress;
    socklen_t clientAddrLen = sizeof(clientAddress);
    int clientSocket;
    while ((clientSocket = accept(listenFd, (struct sockaddr*)&clientAddress, &clientAddrLen)) != -1) {
        if (_clientBuffers.size() >= MAX_CLIENTS) { close(clientSocket); continue; }
        int flags = fcntl(clientSocket, F_GETFL, 0);
        fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

        struct epoll_event event;
        event.events = EPOLLIN; // EPOLLOUT armed when needed
        event.data.fd = clientSocket;
        if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1) { close(clientSocket); continue; }

        ClientConnection newConn;
        newConn.fd = clientSocket;
        newConn.listenFd = listenFd;
        newConn.lastActivity = time(NULL);
        newConn.isReading = false;
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, ipbuf, INET_ADDRSTRLEN);
        newConn.remoteAddr = ipbuf;
        newConn.remotePort = ntohs(clientAddress.sin_port);
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
    if (pos == std::string::npos) return false;
    std::string headersPart = conn.buffer.substr(0, pos);
    std::string after = conn.buffer.substr(pos + 4);
    size_t eol = headersPart.find("\r\n");
    if (eol == std::string::npos) return false;
    std::string start = headersPart.substr(0, eol);
    size_t sp1 = start.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : start.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    conn.method = start.substr(0, sp1);
    conn.uri = start.substr(sp1 + 1, sp2 - sp1 - 1);
    conn.version = start.substr(sp2 + 1);

    size_t lineStart = eol + 2;
    while (lineStart < headersPart.size()) {
        size_t lineEnd = headersPart.find("\r\n", lineStart);
        if (lineEnd == std::string::npos) break;
        std::string line = headersPart.substr(lineStart, lineEnd - lineStart);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0,1);
            while (!name.empty() && (name[name.size()-1] == ' ' || name[name.size()-1] == '\t')) name.erase(name.size()-1);
            for (size_t i=0;i<name.size();++i) name[i] = std::tolower(name[i]);
            conn.headers[name] = value;
        }
        lineStart = lineEnd + 2;
    }

    std::string te;
    if (conn.headers.find("transfer-encoding") != conn.headers.end()) te = conn.headers["transfer-encoding"];
    std::string cl;
    if (conn.headers.find("content-length") != conn.headers.end()) cl = conn.headers["content-length"];

    if (!te.empty()) {
        std::string tel = te; for (size_t i=0;i<tel.size();++i) tel[i]=std::tolower(tel[i]);
        if (tel.find("chunked") != std::string::npos) { conn.bodyType = BODY_CHUNKED; conn.chunkState = CHUNK_READ_SIZE; }
    }
    if (conn.bodyType != BODY_CHUNKED) {
        if (!cl.empty()) { conn.contentLength = static_cast<size_t>(std::strtoul(cl.c_str(), NULL, 10)); conn.bodyType = (conn.contentLength > 0) ? BODY_FIXED : BODY_NONE; }
        else { conn.bodyType = BODY_NONE; }
    }

    conn.buffer.clear();
    if (conn.bodyType == BODY_FIXED) { conn.body.append(after); conn.bodyReceived = conn.body.size(); }
    else if (conn.bodyType == BODY_CHUNKED) { conn.chunkBuffer.append(after); }

    conn.headersParsed = true;
    // Select vhost according to Host header, defaulting to first
    std::map<int, std::vector<ServerConfig> >::iterator git = _serverGroups.find(conn.listenFd);
    if (git != _serverGroups.end()) {
        const std::vector<ServerConfig>& group = git->second;
        if (!group.empty()) {
            std::string hostHeader;
            if (conn.headers.find("host") != conn.headers.end()) hostHeader = conn.headers["host"];
            // strip port if present
            size_t colon = hostHeader.find(':');
            std::string hostname = (colon == std::string::npos) ? hostHeader : hostHeader.substr(0, colon);
            // trim
            while (!hostname.empty() && (hostname[0]==' '||hostname[0]=='\t')) hostname.erase(0,1);
            while (!hostname.empty() && (hostname[hostname.size()-1]==' '||hostname[hostname.size()-1]=='\t')) hostname.erase(hostname.size()-1);
            const ServerConfig* chosen = &group[0];
            if (!hostname.empty()) {
                for (size_t i=0;i<group.size();++i) {
                    if (!group[i].getServerName().empty() && group[i].getServerName() == hostname) {
                        chosen = &group[i];
                        break;
                    }
                }
            }
            _serverForClientFd[conn.fd] = *chosen;
        }
    }
    conn.state = (conn.bodyType == BODY_NONE) ? READY : READING_BODY;
    return true;
}

bool epollManager::processFixedBody(int clientFd) {
    ClientConnection &conn = _clientConnections[clientFd];
    if (conn.bodyReceived >= conn.contentLength) { conn.state = READY; return true; }
    if (!conn.buffer.empty()) {
        conn.body.append(conn.buffer);
        conn.bodyReceived = conn.body.size();
        conn.buffer.clear();
    }
    if (conn.bodyReceived >= conn.contentLength) { conn.state = READY; return true; }
    return false;
}

bool epollManager::processChunkedBody(int clientFd) {
    ClientConnection &c = _clientConnections[clientFd];
    if (!c.buffer.empty()) { c.chunkBuffer.append(c.buffer); c.buffer.clear(); }
    while (true) {
        if (c.chunkState == CHUNK_READ_SIZE) {
            size_t pos = c.chunkBuffer.find("\r\n");
            if (pos == std::string::npos) return false;
            std::string sizeLine = c.chunkBuffer.substr(0, pos);
            size_t semi = sizeLine.find(';');
            if (semi != std::string::npos) sizeLine = sizeLine.substr(0, semi);
            size_t sz = std::strtoul(sizeLine.c_str(), NULL, 16);
            c.currentChunkSize = sz;
            c.chunkBuffer.erase(0, pos + 2);
            if (sz == 0) { c.chunkState = CHUNK_COMPLETE; }
            else { c.chunkState = CHUNK_READ_DATA; }
        }
        if (c.chunkState == CHUNK_READ_DATA) {
            if (c.chunkBuffer.size() < c.currentChunkSize) return false;
            c.body.append(c.chunkBuffer.substr(0, c.currentChunkSize));
            c.chunkBuffer.erase(0, c.currentChunkSize);
            c.chunkState = CHUNK_READ_CRLF;
        }
        if (c.chunkState == CHUNK_READ_CRLF) {
            if (c.chunkBuffer.size() < 2) return false;
            if (c.chunkBuffer.substr(0,2) != "\r\n") return false;
            c.chunkBuffer.erase(0,2);
            c.chunkState = CHUNK_READ_SIZE;
        }
        if (c.chunkState == CHUNK_COMPLETE) {
            size_t pos = c.chunkBuffer.find("\r\n\r\n");
            if (pos != std::string::npos) { c.chunkBuffer.erase(0, pos + 4); c.state = READY; return true; }
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
    if (!conn.headersParsed) parseHeadersFor(clientFd);
    if (conn.state == READING_BODY) {
        if (conn.bodyType == BODY_FIXED) { if (maxBody > 0 && conn.body.size() > maxBody) return true; processFixedBody(clientFd); }
        else if (conn.bodyType == BODY_CHUNKED) { if (!processChunkedBody(clientFd)) return false; if (maxBody > 0 && conn.body.size() > maxBody) return true; }
    }
    return (conn.state == READY);
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
    size_t qpos = pathOnly.find('?'); if (qpos != std::string::npos) pathOnly = pathOnly.substr(0, qpos);
    size_t fpos = pathOnly.find('#'); if (fpos != std::string::npos) pathOnly = pathOnly.substr(0, fpos);
    std::string rel;
    if (locationHasRoot) {
        std::string mount = location->getPath(); rel = pathOnly; if (!mount.empty() && rel.find(mount) == 0) rel = rel.substr(mount.length());
    } else { rel = pathOnly; }
    if (!rel.empty() && rel[0] == '/') rel.erase(0,1);
    std::vector<std::string> parts = ParserUtils::split(rel, '/');
    std::vector<std::string> stack;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& seg = parts[i];
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") { if (stack.empty()) return ""; stack.pop_back(); }
        else stack.push_back(seg);
    }
    std::string full = root;
    if (!full.empty() && full[full.size()-1] != '/') full += "/";
    for (size_t i = 0; i < stack.size(); ++i) { if (i) full += "/"; full += stack[i]; }
    return full;
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
    if (location && !location->getCgiPass().empty()) { response.setStatus(403, "Forbidden"); response.setHeader("Content-Type", "text/html"); response.setBody(createHtmlResponse("403 Forbidden", "DELETE is not allowed on CGI endpoints")); return response; }
    std::string path = resolveFilePath(uri, config);
    if (path.empty()) { response.setStatus(403, "Forbidden"); response.setHeader("Content-Type", "text/html"); response.setBody(createHtmlResponse("403 Forbidden", "Invalid path")); return response; }
    if (!fileExists(path)) { response.setStatus(404, "Not Found"); response.setHeader("Content-Type", "text/html"); response.setBody(createHtmlResponse("404 Not Found", "Resource does not exist")); return response; }
    if (isDirectory(path)) { response.setStatus(403, "Forbidden"); response.setHeader("Content-Type", "text/html"); response.setBody(createHtmlResponse("403 Forbidden", "Directory deletion is not allowed")); return response; }
    if (unlink(path.c_str()) == 0) { response.setStatus(204, "No Content"); response.setBody(""); return response; }
    response.setStatus(500, "Internal Server Error"); response.setHeader("Content-Type", "text/html"); response.setBody(createHtmlResponse("500 Internal Server Error", "Unable to delete resource")); return response;
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
        if (pos + 2 > body.size()) break; if (body.substr(pos, 2) == "--") break; if (body.substr(pos, 2) != "\r\n") return false; pos += 2;
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
    if (location && !location->getCgiPass().empty() && isCgiFile(uri, config.getLocations())) {
        std::string extension = getFileExtension(uri);
        const std::map<std::string, std::string>& cgiConfig = location->getCgiPass();
        std::map<std::string, std::string>::const_iterator it = cgiConfig.find(extension);
        if (it == cgiConfig.end()) it = cgiConfig.find(".*");
        if (it != cgiConfig.end()) { std::string interpreter = it->second; std::string scriptPath = location->getRoot().empty() ? (config.getRoot() + uri) : (location->getRoot() + uri.substr(location->getPath().size())); CgiHandler cgi; return cgi.execute(request, scriptPath, interpreter); }
    }
    std::string basePath = resolveFilePath(uri, config);
    if (basePath.empty()) { response.setStatus(403, "Forbidden"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("403 Forbidden","Invalid upload path")); return response; }
    size_t maxBody = getEffectiveClientMax(location, config);
    if (maxBody > 0 && request.getBody().size() > maxBody) { response.setStatus(413, "Request Entity Too Large"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("413 Request Too Large","Body exceeds limit")); return response; }
    std::string ct = request.getHeader("Content-Type"); std::string ctl = ct; for (size_t i=0;i<ctl.size();++i) ctl[i]=std::tolower(ctl[i]);
    bool created = false;
    if (ctl.find("multipart/form-data") == 0) { size_t bpos = ctl.find("boundary="); if (bpos == std::string::npos) { response.setStatus(400, "Bad Request"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("400 Bad Request","Missing multipart boundary")); return response; }
        std::string boundary = ctl.substr(bpos + 9); if (!boundary.empty() && boundary[0]=='"') { size_t endq = boundary.find('"', 1); boundary = (endq==std::string::npos)?boundary.substr(1):boundary.substr(1, endq-1); }
        size_t savedCount = 0; bool anyCreated = false; std::string lastPath;
        if (!parseMultipartAndSave(request.getBody(), boundary, basePath, uri, savedCount, anyCreated, lastPath)) { response.setStatus(400, "Bad Request"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("400 Bad Request","No file parts found")); return response; }
        created = anyCreated; response.setStatus(created ? 201 : 200, created ? "Created" : "OK"); response.setHeader("Content-Type","text/html"); response.setHeader("Location", uri); response.setBody(createHtmlResponse(created?"201 Created":"200 OK", toString(savedCount) + " file(s) uploaded")); return response; }
    bool isDir = isDirectory(basePath) || (!uri.empty() && uri[uri.size()-1]=='/'); if (isDir) { response.setStatus(400, "Bad Request"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("400 Bad Request","No filename specified for upload")); return response; }
    bool existed = fileExists(basePath); std::ofstream ofs(basePath.c_str(), std::ios::binary); if (!ofs.is_open()) { response.setStatus(403, "Forbidden"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("403 Forbidden","Cannot write file")); return response; }
    const std::string& data = request.getBody(); ofs.write(data.c_str(), data.size()); ofs.close(); response.setStatus(existed?200:201, existed?"OK":"Created"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse(existed?"200 OK":"201 Created", existed?"File overwritten":"File created")); return response;
}

Response epollManager::createResponseForRequest(const Request& request, const ServerConfig& config) {
    Response response;
    if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") { response.setStatus(505, "HTTP Version Not Supported"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("505 HTTP Version Not Supported","Unsupported HTTP version")); return response; }
    std::string method = request.getMethod(); std::string uri = request.getUri(); const LocationConfig* location = findLocationConfig(uri, config);
    if (!isMethodAllowed(method, uri, config)) { response.setStatus(405, "Method Not Allowed"); response.setHeader("Allow", buildAllowHeader(location)); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("405 Method Not Allowed","Method "+method+" not allowed")); return response; }

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
    if (method == "POST") { return handlePost(request, location, config); }
    if (uri == "/" || uri == "/index.html") {
        // Prefer location index if available for '/'
        std::string indexConf = (location && !location->getIndex().empty()) ? location->getIndex() : config.getIndex();
        std::vector<std::string> indexList = ParserUtils::split(indexConf, ' '); bool indexFound = false;
        for (size_t i = 0; i < indexList.size() && !indexFound; ++i) { std::string indexFile = ParserUtils::trim(indexList[i]); if (!indexFile.empty()) { std::string filePath = resolveFilePath("/" + indexFile, config); if (!filePath.empty() && fileExists(filePath)) { response.setStatus(200, "OK"); response.setHeader("Content-Type", getContentType(indexFile)); response.setBody(readFileContent(filePath)); indexFound = true; } } }
        if (!indexFound) {
            // custom error page if configured
            std::string ep = config.getErrorPagePath(404);
            if (!ep.empty()) {
                std::string p = resolveFilePath(ep, config);
                if (!p.empty() && fileExists(p)) { response.setStatus(404, "Not Found"); response.setHeader("Content-Type", getContentType(p)); response.setBody(readFileContent(p)); goto finalize; }
            }
            response.setStatus(404, "Not Found"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("404 Not Found","Index file not found"));
        }
    } else if (location && !location->getCgiPass().empty() && isCgiFile(uri, config.getLocations())) {
        std::string extension = getFileExtension(uri); const std::map<std::string, std::string>& cgiConfig = location->getCgiPass(); std::map<std::string, std::string>::const_iterator it = cgiConfig.find(extension); if (it == cgiConfig.end()) it = cgiConfig.find(".*"); if (it != cgiConfig.end()) { std::string interpreter = it->second; std::string scriptPath = location->getRoot() + uri; CgiHandler cgi; return cgi.execute(request, scriptPath, interpreter); }
        response.setStatus(400, "Bad Request"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("400 Bad Request","No CGI interpreter configured"));
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
                        std::string ep = config.getErrorPagePath(403);
                        if (!ep.empty()) { std::string p = resolveFilePath(ep, config); if (!p.empty() && fileExists(p)) { response.setStatus(403, "Forbidden"); response.setHeader("Content-Type", getContentType(p)); response.setBody(readFileContent(p)); goto finalize; } }
                        response.setStatus(403, "Forbidden"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("403 Forbidden","Directory listing forbidden"));
                    }
                }
            } else { response.setStatus(200, "OK"); response.setHeader("Content-Type", getContentType(uri)); response.setBody(readFileContent(filePath)); }
        } else {
            std::string ep = config.getErrorPagePath(404);
            if (!ep.empty()) { std::string p = resolveFilePath(ep, config); if (!p.empty() && fileExists(p)) { response.setStatus(404, "Not Found"); response.setHeader("Content-Type", getContentType(p)); response.setBody(readFileContent(p)); goto finalize; } }
            response.setStatus(404, "Not Found"); response.setHeader("Content-Type","text/html"); response.setBody(createHtmlResponse("404 Not Found","The requested URL was not found"));
        }
    }
finalize:
    // Common headers
    response.setHeader("Connection", "close"); response.setHeader("Server", "webserv/1.0"); response.setHeader("Date", getCurrentDate());
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
        ClientConnection conn; conn.fd = clientFd; conn.lastActivity = time(NULL); _clientConnections[clientFd] = conn;
    }
    ClientConnection &conn = _clientConnections[clientFd];
    conn.isReading = true; conn.lastActivity = time(NULL);
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead = recv(clientFd, buffer, BUFFER_SIZE, 0);
    if (bytesRead > 0) {
        conn.buffer.append(buffer, bytesRead);
        if (conn.buffer.size() + conn.body.size() > MAX_REQUEST_SIZE) { sendErrorResponse(clientFd, 413, "Request Entity Too Large"); return; }
        if (!processConnectionData(clientFd)) { return; }
        try {
            std::string raw; raw += conn.method + " " + conn.uri + " " + conn.version + "\r\n";
            for (std::map<std::string,std::string>::const_iterator it=conn.headers.begin(); it!=conn.headers.end(); ++it) { if (it->first == "transfer-encoding" || it->first == "content-length") continue; raw += it->first + ": " + it->second + "\r\n"; }
            if (!conn.body.empty()) raw += std::string("Content-Length: ") + toString(conn.body.size()) + "\r\n";
            raw += "\r\n"; raw += conn.body; Request request(raw);
            if (request.isComplete()) {
                const ServerConfig& cfg = _serverForClientFd[clientFd];
                // Detect CGI route early and start async CGI if needed
                const LocationConfig* location = findLocationConfig(conn.uri, cfg);
                bool wantsCgi = (location && !location->getCgiPass().empty() && isCgiFile(conn.uri, cfg.getLocations()));
                if (wantsCgi) {
                    if (!startCgiFor(clientFd, request, cfg, location)) { sendErrorResponse(clientFd, 500, "Internal Server Error"); }
                } else {
                    Response response = createResponseForRequest(request, cfg);
                    std::string responseStr = response.getResponse(); conn.outBuffer = responseStr; conn.outOffset = 0; conn.hasResponse = true; armWriteEvent(clientFd, true);
                }
            }
            else sendErrorResponse(clientFd, 400, "Bad Request");
        } catch (...) { sendErrorResponse(clientFd, 400, "Bad Request"); }
        return;
    } else if (bytesRead == 0) { closeClient(clientFd); }
    else { closeClient(clientFd); }
    conn.isReading = false;
}

void epollManager::handleClientWrite(int clientFd, uint32_t events)
{
    (void)events;
    std::map<int, ClientConnection>::iterator it = _clientConnections.find(clientFd);
    if (it == _clientConnections.end()) return;
    ClientConnection &conn = it->second;
    if (!conn.hasResponse) return;
    size_t remaining = conn.outBuffer.size() - conn.outOffset;
    if (remaining == 0) { armWriteEvent(clientFd, false); _clientBuffers[clientFd].clear(); _clientConnections.erase(clientFd); closeClient(clientFd); return; }
    size_t toSend = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining; ssize_t n = send(clientFd, conn.outBuffer.data() + conn.outOffset, toSend, 0);
    if (n > 0) { conn.outOffset += static_cast<size_t>(n); if (conn.outOffset >= conn.outBuffer.size()) { armWriteEvent(clientFd, false); _clientBuffers[clientFd].clear(); _clientConnections.erase(clientFd); closeClient(clientFd); } }
    else if (n == 0) { closeClient(clientFd); }
    else { closeClient(clientFd); }
}

void epollManager::sendErrorResponse(int clientFd, int code, const std::string& message)
{
    Response response; response.setStatus(code, message); response.setHeader("Content-Type", "text/html"); response.setHeader("Connection", "close"); response.setHeader("Server", "webserv/1.0"); std::string body = createHtmlResponse(toString(code) + " " + message, "Error: " + message); response.setBody(body);
    std::string responseStr = response.getResponse(); ClientConnection &conn = _clientConnections[clientFd]; conn.outBuffer = responseStr; conn.outOffset = 0; conn.hasResponse = true; armWriteEvent(clientFd, true);
}

void epollManager::run()
{
    struct epoll_event events[MAX_EVENTS]; INFO("Demarrage de la boucle epoll unifiee...");

    while (true) 
    {
        int num = epoll_wait(_epollFd, events, MAX_EVENTS, -1); if (num <= 0) continue; cleanupIdleConnections();
        for (int i = 0; i < num; ++i) 
        {
            int fd = events[i].data.fd;
            if (_listenSockets.find(fd) != _listenSockets.end()) {
                handleNewConnection(fd);
            } else if (_cgiOutToClient.find(fd) != _cgiOutToClient.end()) {
                handleCgiOutEvent(fd, events[i].events);
            } else if (_cgiInToClient.find(fd) != _cgiInToClient.end()) {
                handleCgiInEvent(fd, events[i].events);
            } else {
                if (events[i].events & EPOLLIN) handleClientRead(fd, events[i].events);
                if (events[i].events & EPOLLOUT) handleClientWrite(fd, events[i].events);
            }
        }
    }
}

void epollManager::armWriteEvent(int clientFd, bool enable)
{
    struct epoll_event ev; 
    ev.data.fd = clientFd; 
    ev.events = EPOLLIN | (enable ? EPOLLOUT : 0); 
    epoll_ctl(_epollFd, EPOLL_CTL_MOD, clientFd, &ev);
}

static std::string dirnameOf(const std::string& path) {
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return std::string(".");
    if (p == 0) return std::string("/");
    return path.substr(0, p);
}

bool epollManager::startCgiFor(int clientFd, const Request& request, const ServerConfig& config, const LocationConfig* location)
{
    ClientConnection &conn = _clientConnections[clientFd];
    // Resolve script path
    std::string scriptPath = location->getRoot().empty() ? (config.getRoot() + conn.uri) : (location->getRoot() + conn.uri.substr(location->getPath().size()));
    if (!fileExists(scriptPath)) { sendErrorResponse(clientFd, 404, "Not Found"); return false; }
    int pin[2], pout[2]; if (pipe(pin) == -1 || pipe(pout) == -1) { return false; }
    // nonblocking
    fcntl(pin[1], F_SETFL, fcntl(pin[1], F_GETFL, 0) | O_NONBLOCK);
    fcntl(pout[0], F_SETFL, fcntl(pout[0], F_GETFL, 0) | O_NONBLOCK);

    pid_t pid = fork(); if (pid == -1) { close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]); return false; }
    if (pid == 0) {
        // child
        // chdir to script directory
        std::string dir = dirnameOf(scriptPath);
        chdir(dir.c_str());
        // dup stdio
        close(pin[1]); close(pout[0]);
        dup2(pin[0], STDIN_FILENO); dup2(pout[1], STDOUT_FILENO); dup2(pout[1], STDERR_FILENO);
        close(pin[0]); close(pout[1]);
        // Build env
        std::vector<std::string> envStore;
        std::vector<char*> envp;
        envStore.push_back("GATEWAY_INTERFACE=CGI/1.1");
        envStore.push_back("SERVER_PROTOCOL=HTTP/1.1");
        envStore.push_back("SERVER_SOFTWARE=webserv/1.0");
        envStore.push_back(std::string("REQUEST_METHOD=") + request.getMethod());
        envStore.push_back(std::string("SCRIPT_FILENAME=") + scriptPath);
        envStore.push_back(std::string("SCRIPT_NAME=") + conn.uri);
        envStore.push_back(std::string("REQUEST_URI=") + conn.uri);
        envStore.push_back(std::string("QUERY_STRING=") + request.getUri().substr(request.getUri().find('?')==std::string::npos?request.getUri().size():request.getUri().find('?')+1));
        envStore.push_back(std::string("SERVER_NAME=") + config.getServerName());
        envStore.push_back(std::string("SERVER_PORT=") + toString(config.getPort()));
        envStore.push_back(std::string("REMOTE_ADDR=") + conn.remoteAddr);
        envStore.push_back(std::string("DOCUMENT_ROOT=") + config.getRoot());
        if (request.getMethod() == "POST") {
            envStore.push_back(std::string("CONTENT_LENGTH=") + toString(request.getBody().size()));
            envStore.push_back(std::string("CONTENT_TYPE=") + request.getHeader("Content-Type"));
        }
        // HTTP_*
        const std::map<std::string,std::string>& hdrs = request.getHeaders();
        for (std::map<std::string,std::string>::const_iterator it = hdrs.begin(); it != hdrs.end(); ++it) {
            std::string name = toUpperCase(replaceChars(it->first, "-", "_"));
            envStore.push_back(std::string("HTTP_") + name + std::string("=") + it->second);
        }
        for (size_t i=0;i<envStore.size();++i) envp.push_back(strdup(envStore[i].c_str()));
        envp.push_back(NULL);
        // Args (interpreter optional)
        std::vector<char*> args;
        std::string ext = getFileExtension(scriptPath);
        std::string interpreter;
        const std::map<std::string,std::string>& cgiPass = location->getCgiPass();
        std::map<std::string,std::string>::const_iterator it = cgiPass.find(ext);
        if (it == cgiPass.end()) { std::map<std::string,std::string>::const_iterator it2 = cgiPass.find(".*"); if (it2 != cgiPass.end()) interpreter = it2->second; }
        else interpreter = it->second;
        if (!interpreter.empty()) { args.push_back(strdup(interpreter.c_str())); args.push_back(strdup(scriptPath.c_str())); }
        else { args.push_back(strdup(scriptPath.c_str())); }
        args.push_back(NULL);
        if (!interpreter.empty()) { execve(interpreter.c_str(), args.data(), envp.data()); }
        else { execve(scriptPath.c_str(), args.data(), envp.data()); }
        // If execve fails
        _exit(1);
    }
    // parent
    close(pin[0]); close(pout[1]);
    // register fds to epoll
    struct epoll_event ev;
    ev.data.fd = pout[0]; ev.events = EPOLLIN; epoll_ctl(_epollFd, EPOLL_CTL_ADD, pout[0], &ev);
    _cgiOutToClient[pout[0]] = clientFd;
    // Register input if there is a body to send
    if (!conn.body.empty()) { ev.data.fd = pin[1]; ev.events = EPOLLOUT; epoll_ctl(_epollFd, EPOLL_CTL_ADD, pin[1], &ev); _cgiInToClient[pin[1]] = clientFd; }
    conn.cgiRunning = true; conn.cgiPid = pid; conn.cgiInFd = pin[1]; conn.cgiOutFd = pout[0]; conn.cgiInOffset = 0; conn.cgiStart = time(NULL);
    return true;
}

void epollManager::handleCgiOutEvent(int pipeFd, uint32_t events)
{
    (void)events;
    int clientFd = _cgiOutToClient[pipeFd];
    ClientConnection &conn = _clientConnections[clientFd];
    char buf[BUFFER_SIZE]; ssize_t n = read(pipeFd, buf, sizeof(buf));
    if (n > 0) { conn.cgiOutBuffer.append(buf, n); return; }
    // EOF or error: finalize
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL); close(pipeFd); _cgiOutToClient.erase(pipeFd); conn.cgiOutFd = -1;
    finalizeCgiFor(clientFd);
}

void epollManager::handleCgiInEvent(int pipeFd, uint32_t events)
{
    (void)events;
    int clientFd = _cgiInToClient[pipeFd];
    ClientConnection &conn = _clientConnections[clientFd];
    if (conn.body.empty()) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL); close(pipeFd); _cgiInToClient.erase(pipeFd); conn.cgiInFd = -1; return; }
    size_t remaining = conn.body.size() - conn.cgiInOffset;
    if (remaining == 0) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL); close(pipeFd); _cgiInToClient.erase(pipeFd); conn.cgiInFd = -1; return; }
    size_t toWrite = remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining; ssize_t w = write(pipeFd, conn.body.data() + conn.cgiInOffset, toWrite);
    if (w > 0) { conn.cgiInOffset += static_cast<size_t>(w); if (conn.cgiInOffset >= conn.body.size()) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL); close(pipeFd); _cgiInToClient.erase(pipeFd); conn.cgiInFd = -1; } }
    else if (w == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, pipeFd, NULL); close(pipeFd); _cgiInToClient.erase(pipeFd); conn.cgiInFd = -1; }
}

static void parseCgiOutputToResponse(const std::string& cgiOutput, Response& response)
{
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        std::string headersPart = cgiOutput.substr(0, headerEnd);
        std::string body = cgiOutput.substr(headerEnd + 4);
        std::vector<std::string> headerLines = ParserUtils::split(headersPart, '\n');
        int statusCode = 200; std::string statusText = "OK";
        for (size_t i = 0; i < headerLines.size(); ++i) {
            if (!headerLines[i].empty() && headerLines[i][headerLines[i].size()-1] == '\r') headerLines[i].erase(headerLines[i].size()-1);
            size_t colonPos = headerLines[i].find(':');
            if (colonPos != std::string::npos) {
                std::string name = ParserUtils::trim(headerLines[i].substr(0, colonPos));
                std::string value = ParserUtils::trim(headerLines[i].substr(colonPos + 1));
                if (toUpperCase(name) == "STATUS") {
                    // Format: "Status: 302 Found"
                    std::istringstream iss(value); iss >> statusCode; std::string rest; std::getline(iss, rest); if (!rest.empty() && rest[0]==' ') rest.erase(0,1); statusText = rest.empty()?"":rest;
                } else {
                    response.setHeader(name, value);
                }
            }
        }
        response.setStatus(statusCode, statusText.empty()?"OK":statusText);
        response.setBody(body);
        if (response.getHeaders().find("Content-Type") == response.getHeaders().end()) response.setHeader("Content-Type", "text/html");
    } else {
        response.setStatus(200, "OK"); response.setHeader("Content-Type", "text/html"); response.setBody(cgiOutput);
    }
}

void epollManager::finalizeCgiFor(int clientFd)
{
    ClientConnection &conn = _clientConnections[clientFd];
    // Reap child if finished
    if (conn.cgiPid > 0) { int st; waitpid(conn.cgiPid, &st, WNOHANG); }
    // Build response from CGI output
    Response resp; parseCgiOutputToResponse(conn.cgiOutBuffer, resp);
    // HEAD handling: keep headers, strip body but keep original length
    if (conn.method == "HEAD") {
        size_t len = resp.getBodyLength();
        resp.setHeader("Content-Length", toString(len));
        resp.setBody("");
    }
    std::string out = resp.getResponse();
    conn.outBuffer = out; conn.outOffset = 0; conn.hasResponse = true; armWriteEvent(clientFd, true);
    conn.cgiRunning = false; conn.cgiPid = -1; conn.cgiOutBuffer.clear();
}

void epollManager::closeClient(int clientFd)
{
    std::map<int, ClientConnection>::iterator it = _clientConnections.find(clientFd);
    if (it != _clientConnections.end()) {
        ClientConnection& c = it->second;
        if (c.cgiRunning) {
            if (c.cgiPid > 0) kill(c.cgiPid, SIGKILL);
            if (c.cgiInFd != -1) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiInFd, NULL); close(c.cgiInFd); _cgiInToClient.erase(c.cgiInFd); }
            if (c.cgiOutFd != -1) { epoll_ctl(_epollFd, EPOLL_CTL_DEL, c.cgiOutFd, NULL); close(c.cgiOutFd); _cgiOutToClient.erase(c.cgiOutFd); }
        }
    }
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
    close(clientFd);
    _clientBuffers.erase(clientFd);
    _clientConnections.erase(clientFd);
}
