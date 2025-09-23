#include "epollManager.hpp"
#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/ServerConfig.hpp"
#include "../utils/Utils.hpp"
#include "../cgi/CgiHandler.hpp"
#include <fstream>

Response epollManager::handleUpload(const Request& request, const LocationConfig& location) {
    Response response;

    if (!location.hasUploadPath()) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("Upload not allowed here\n");
        return response;
    }
    size_t maxBody = getEffectiveClientMax(&location);

    if (maxBody > 0 && request.getBody().size() > maxBody) {
        response.setStatus(413, "Request Entity Too Large");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("Body exceeds client_max_body_size\n");
        return response;
    }
    std::string dir = location.getUploadPath();

    // create directory if needed
    if (mkdirRecursive(dir, 0750) == -1) {
        response.setStatus(500, "Internal Server Error");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("Upload directory could not be created\n");
        return response;
    }

    // generate unique filename
    std::ostringstream filename;
    filename << "upload_" << time(NULL) << "_" << getpid() << ".bin";
    std::string filepath = dir + "/" + filename.str();

    // write the file
    std::ofstream ofs(filepath.c_str(), std::ios::binary);
    if (!ofs) {
        response.setStatus(500, "Internal Server Error");
        response.setHeader("Content-Type", "text/plain");
        response.setBody("Could not open file for writing\n");
        return response;
    }
    ofs.write(request.getBody().c_str(), request.getBody().size());
    ofs.close();

    std::string publicUrl = location.getPath();
    if (!publicUrl.empty() && publicUrl[publicUrl.size() - 1] != '/')
        publicUrl += "/";
    publicUrl += filename.str();

    // response
    response.setStatus(201, "Created");
    response.setHeader("Content-Type", "text/plain");
    response.setHeader("Location", publicUrl);
    response.setBody("✅ File uploaded successfully!\n"
                     "Saved as: " + filepath + "\n"
                     "Accessible at: " + publicUrl + "\n");

    return response;
}

void epollManager::cleanupIdleConnections() {
    time_t now = time(NULL);
    
    // clean connection each 5sec
    if (now - _lastCleanup < CLEANUP_INTERVAL) {
        return;
    }
    _lastCleanup = now;
    
    LOG("Checking for idle connections...");
    int closedCount = 0;
    
    std::map<int, std::string>::iterator bufIt = _clientBuffers.begin();
    while (bufIt != _clientBuffers.end()) {
        int clientFd = bufIt->first;
        
        // check connection if client exist
        std::map<int, ClientConnection>::iterator connIt = _clientConnections.find(clientFd);
        if (connIt != _clientConnections.end()) {
            double idleTime = difftime(now, connIt->second.lastActivity);
            
            if (idleTime > CONNECTION_TIMEOUT) {
                LOG("Closing idle connection (fd: " + toString(clientFd) + 
                    ", idle: " + toString(idleTime) + "s)");
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
    if (closedCount > 0) {
        LOG("Closed " + toString(closedCount) + " idle connections");
    }
}

// Use custom body size if defined
size_t epollManager::getEffectiveClientMax(const LocationConfig* location) const {
    if (location && location->getClientMax() > 0) return location->getClientMax();
    return _config.getClientMax();
}

// Parse headers and startline for a client if available
bool epollManager::parseHeadersFor(int clientFd) {
    ClientConnection &conn = _clientConnections[clientFd];
    size_t pos = conn.buffer.find("\r\n\r\n");
    if (pos == std::string::npos) return false; // not yet

    std::string headersPart = conn.buffer.substr(0, pos);
    std::string after = conn.buffer.substr(pos + 4);

    // Parse start line
    size_t eol = headersPart.find("\r\n");
    if (eol == std::string::npos) return false;
    std::string start = headersPart.substr(0, eol);
    size_t sp1 = start.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : start.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    conn.method = start.substr(0, sp1);
    conn.uri = start.substr(sp1 + 1, sp2 - sp1 - 1);
    conn.version = start.substr(sp2 + 1);

    // Parse header lines
    size_t lineStart = eol + 2;
    while (lineStart < headersPart.size()) {
        size_t lineEnd = headersPart.find("\r\n", lineStart);
        if (lineEnd == std::string::npos) break;
        std::string line = headersPart.substr(lineStart, lineEnd - lineStart);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // trim spaces
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0,1);
            while (!name.empty() && (name[name.size()-1] == ' ' || name[name.size()-1] == '\t')) name.erase(name.size()-1);
            // lowercase header name
            for (size_t i=0;i<name.size();++i) name[i] = std::tolower(name[i]);
            conn.headers[name] = value;
        }
        lineStart = lineEnd + 2;
    }

    // Determine body strategy
    std::string te;
    if (conn.headers.find("transfer-encoding") != conn.headers.end()) te = conn.headers["transfer-encoding"];
    std::string cl;
    if (conn.headers.find("content-length") != conn.headers.end()) cl = conn.headers["content-length"];

    if (!te.empty()) {
        // check chunked presence
        std::string tel = te; for (size_t i=0;i<tel.size();++i) tel[i]=std::tolower(tel[i]);
        if (tel.find("chunked") != std::string::npos) {
            conn.bodyType = BODY_CHUNKED;
            conn.chunkState = CHUNK_READ_SIZE;
        }
    }
    if (conn.bodyType != BODY_CHUNKED) {
        if (!cl.empty()) {
            conn.contentLength = static_cast<size_t>(std::strtoul(cl.c_str(), NULL, 10));
            conn.bodyType = (conn.contentLength > 0) ? BODY_FIXED : BODY_NONE;
        } else {
            conn.bodyType = BODY_NONE;
        }
    }

    // Move post-header data
    conn.buffer.clear();
    if (conn.bodyType == BODY_FIXED) {
        conn.body.append(after);
        conn.bodyReceived = conn.body.size();
    } else if (conn.bodyType == BODY_CHUNKED) {
        conn.chunkBuffer.append(after);
    } else {
        // No body: ready immediately
    }

    conn.headersParsed = true;
    conn.state = (conn.bodyType == BODY_NONE) ? READY : READING_BODY;
    return true;
}

bool epollManager::processFixedBody(int clientFd) {
    ClientConnection &conn = _clientConnections[clientFd];
    if (conn.bodyReceived >= conn.contentLength) { conn.state = READY; return true; }
    // Move from main buffer if any (should be empty in our design)
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
    // move from buffer to chunkBuffer
    if (!c.buffer.empty()) { c.chunkBuffer.append(c.buffer); c.buffer.clear(); }

    while (true) {
        if (c.chunkState == CHUNK_READ_SIZE) {
            size_t pos = c.chunkBuffer.find("\r\n");
            if (pos == std::string::npos) return false;
            std::string sizeLine = c.chunkBuffer.substr(0, pos);
            // handle optional chunk extensions by trimming after ';'
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
            if (c.chunkBuffer.substr(0,2) != "\r\n") {
                // malformed
                return false;
            }
            c.chunkBuffer.erase(0,2);
            c.chunkState = CHUNK_READ_SIZE;
        }
        if (c.chunkState == CHUNK_COMPLETE) {
            // consume optional trailers until CRLF
            size_t pos = c.chunkBuffer.find("\r\n\r\n");
            if (pos != std::string::npos) {
                c.chunkBuffer.erase(0, pos + 4);
                c.state = READY;
                return true;
            }
            // sometimes there are no trailers: allow CRLF only
            if (c.chunkBuffer.find("\r\n") == 0) {
                c.chunkBuffer.erase(0, 2);
                c.state = READY;
                return true;
            }
            return false;
        }
    }
}

bool epollManager::processConnectionData(int clientFd) {
    ClientConnection &conn = _clientConnections[clientFd];
    const LocationConfig* location = findLocationConfig(conn.uri.empty() ? "/" : conn.uri);
    size_t maxBody = getEffectiveClientMax(location);

    if (!conn.headersParsed) {
        if (!parseHeadersFor(clientFd)) return false;
        if (conn.method == "POST" && conn.bodyType == BODY_NONE) {
            // Length required when a body is expected for POST and none provided
            // We'll consider empty body as allowed for now; remove if strict.
        }
    }

    if (conn.state == READING_BODY) {
        if (conn.bodyType == BODY_FIXED) {
            if (maxBody > 0 && conn.body.size() > maxBody) return true; // will 413 at routing
            processFixedBody(clientFd);
        } else if (conn.bodyType == BODY_CHUNKED) {
            if (!processChunkedBody(clientFd)) return false;
            if (maxBody > 0 && conn.body.size() > maxBody) return true;
        }
    }
    return (conn.state == READY);
}

bool epollManager::isCgiRequest(const std::string& uri) const {
	// Get the location block for this URI
	const LocationConfig* location = findLocationConfig(uri);
	if (!location) return false;

	// Check if this location has CGI configured
	return !location->getCgiPass().empty();
}

epollManager::epollManager(int serverSocket, ServerConfig& config) 
	: _serverSocket(serverSocket), _epollFd(-1), _config(config)
{
	_lastCleanup = time(NULL);
	_epollFd = epoll_create1(0);
	if (_epollFd == -1)
		throw std::runtime_error("epoll_create1 failed");
	LOG("epoll instance created");

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = _serverSocket;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, _serverSocket, &event) == -1)
	{
		close(_epollFd);
		throw std::runtime_error("Failed to add server socket to epoll");
	}
	LOG("Server socket added to epoll");
}

epollManager::~epollManager()
{
	if (_epollFd != -1)
		close(_epollFd);

	// close clients connections
	std::map<int, std::string>::iterator bufIt;
    for (bufIt = _clientBuffers.begin(); bufIt != _clientBuffers.end(); ++bufIt) {
        close(bufIt->first);
    }
    _clientBuffers.clear();
    _clientConnections.clear();
}


bool epollManager::isMethodAllowed(const std::string& method, const std::string& uri) const
{
	const LocationConfig* location = findLocationConfig(uri);
	if (location) {
		const std::vector<std::string>& allowedMethods = location->getAllowedMethods();
		if (!allowedMethods.empty()) {
			return std::find(allowedMethods.begin(), allowedMethods.end(), method) != allowedMethods.end();
		}
	}
	return true; // Par défaut, toutes les méthodes sont autorisées
}

const LocationConfig* epollManager::findLocationConfig(const std::string& uri) const
{
    const std::vector<LocationConfig>& locations = _config.getLocations();
    const LocationConfig* bestMatch = NULL;
    
    for (size_t i = 0; i < locations.size(); ++i) {
        const LocationConfig& loc = locations[i];
        if (uri.find(loc.getPath()) == 0) {
            if (!bestMatch || loc.getPath().length() > bestMatch->getPath().length()) {
                bestMatch = &loc;
            }
        }
    }
    return bestMatch;
}

// Construit la valeur de l'en-tête Allow à partir des méthodes autorisées de la location
std::string epollManager::buildAllowHeader(const LocationConfig* location) const {
    std::string allow;
    if (location) {
        const std::vector<std::string>& methods = location->getAllowedMethods();
        for (size_t i = 0; i < methods.size(); ++i) {
            if (i) allow += ", ";
            allow += methods[i];
        }
    }
    // Si non défini dans la config, propose un set par défaut informatif
    if (allow.empty()) allow = "GET, POST, DELETE";
    return allow;
}

// Résout un chemin absolu sécurisé à partir de l'URI et de la meilleure location
// Règles:
// - Si la location n'a PAS de root explicite: utiliser server_root + uri (ne pas retirer le préfixe)
// - Si la location a un root explicite: retirer le préfixe de location et utiliser location_root + chemin_relatif
std::string epollManager::resolveFilePath(const std::string& uri) const {
    const LocationConfig* location = findLocationConfig(uri);
    const bool hasLocation = (location != NULL);
    const bool locationHasRoot = (hasLocation && !location->getRoot().empty());

    std::string root = locationHasRoot ? location->getRoot() : _config.getRoot();

    // Retirer query-string et fragment de l'URI
    std::string pathOnly = uri;
    size_t qpos = pathOnly.find('?');
    if (qpos != std::string::npos) pathOnly = pathOnly.substr(0, qpos);
    size_t fpos = pathOnly.find('#');
    if (fpos != std::string::npos) pathOnly = pathOnly.substr(0, fpos);

    // Construire la partie relative
    std::string rel;
    if (locationHasRoot) {
        // On retire le préfixe de la location pour respecter le mapping style nginx
        std::string mount = location->getPath();
        rel = pathOnly;
        if (!mount.empty() && rel.find(mount) == 0) {
            rel = rel.substr(mount.length());
        }
    } else {
        // Pas de root spécifique à la location: on conserve l'URI entière
        rel = pathOnly;
    }

    // Normalisation canonique de rel (résolution . et ..)
    if (!rel.empty() && rel[0] == '/') rel.erase(0,1);

    std::vector<std::string> parts = ParserUtils::split(rel, '/');
    std::vector<std::string> stack;
    for (size_t i = 0; i < parts.size(); ++i) {
        const std::string& seg = parts[i];
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (stack.empty()) {
                return ""; // tentative d'évasion
            }
            stack.pop_back();
        } else {
            stack.push_back(seg);
        }
    }

    // Rejoindre proprement
    std::string full = root;
    if (!full.empty() && full[full.size()-1] != '/') full += "/";
    for (size_t i = 0; i < stack.size(); ++i) {
        if (i) full += "/";
        full += stack[i];
    }
    if (hasLocation && rel.empty()) {
        std::string mount = location->getPath();
        std::string index = location->getIndex();
        if (!index.empty()) {
            if (full[full.size()-1] != '/') full += "/";
                full += mount.substr(1); // ajoute "uploads/"
            if (full[full.size()-1] != '/') full += "/";
                full += index; // ajoute "index.html"
        }
}
    //std::cout << "\n\n\n" << "FULLL PATH : " << full << std::endl;
    return full;
}

// Handler de la méthode DELETE
Response epollManager::handleDelete(const Request& request, const LocationConfig* location) {
    Response response;
    const std::string uri = request.getUri();

    // Ne pas autoriser DELETE sur des endpoints CGI (sécurité conservatrice)
    if (location && !location->getCgiPass().empty()) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/html");
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        response.setBody(createHtmlResponse("403 Forbidden", "DELETE is not allowed on CGI endpoints"));
        return response;
    }

    std::string path = resolveFilePath(uri);
    if (path.empty()) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/html");
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        response.setBody(createHtmlResponse("403 Forbidden", "Invalid path"));
        return response;
    }

    if (!fileExists(path)) {
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "text/html");
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        response.setBody(createHtmlResponse("404 Not Found", "Resource does not exist"));
        return response;
    }

    if (isDirectory(path)) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/html");
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        response.setBody(createHtmlResponse("403 Forbidden", "Directory deletion is not allowed"));
        return response;
    }

    if (unlink(path.c_str()) == 0) {
        LOG("Deleted file: " + path);
        response.setStatus(204, "No Content");
        // Corps vide et Content-Length: 0 pour clarté
        response.setBody("");
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        return response;
    }

    // Mapping d'erreurs courantes
    int err = errno;
    if (err == EACCES || err == EPERM) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("403 Forbidden", "Permission denied"));
    } else if (err == ENOENT) {
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("404 Not Found", "Resource not found"));
    } else if (err == EISDIR) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("403 Forbidden", "Target is a directory"));
    } else {
        response.setStatus(500, "Internal Server Error");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("500 Internal Server Error", "Unable to delete resource"));
    }
    response.setHeader("Connection", "close");
    response.setHeader("Server", "webserv/1.0");
    response.setHeader("Date", getCurrentDate());
    return response;
}


static std::string sanitizeFilename(const std::string& name) {
    std::string n;
    for (size_t i=0;i<name.size();++i) {
        char c = name[i];
        if (c=='/' || c=='\\') continue;
        if (std::isalnum(static_cast<unsigned char>(c)) || c=='.' || c=='-' || c=='_') n += c;
        else n += '_';
    }
    if (n.empty()) n = "upload.bin";
    return n;
}

// Very simple multipart parser: saves file parts to disk
bool epollManager::parseMultipartAndSave(const std::string& body, const std::string& boundary,
                                         const std::string& basePath, const std::string& uri,
                                         size_t& savedCount, bool& anyCreated, std::string& lastSavedPath)
{
    savedCount = 0; anyCreated = false; lastSavedPath.clear();
    if (boundary.empty()) return false;
    std::string sep = std::string("--") + boundary;
    size_t pos = 0;
    // Skip possible preamble until first boundary
    size_t start = body.find(sep, pos);
    if (start == std::string::npos) return false;
    pos = start + sep.size();
    while (true) {
        if (pos + 2 > body.size()) break;
        if (body.substr(pos, 2) == "--") {
            // end marker
            break;
        }
        if (body.substr(pos, 2) != "\r\n") return false;
        pos += 2; // skip CRLF
        // headers until CRLFCRLF
        size_t hdrEnd = body.find("\r\n\r\n", pos);
        if (hdrEnd == std::string::npos) return false;
        std::string partHeaders = body.substr(pos, hdrEnd - pos);
        pos = hdrEnd + 4;
        // find next boundary
        size_t next = body.find(sep, pos);
        if (next == std::string::npos) return false;
        // content is [pos, next-2] (exclude trailing CRLF)
        size_t contentEnd = next;
        if (contentEnd >= 2 && body.substr(contentEnd - 2, 2) == "\r\n") contentEnd -= 2;
        std::string content = body.substr(pos, contentEnd - pos);
        pos = next + sep.size();

        // Parse Content-Disposition header for filename
        std::string disp;
        std::istringstream iss(partHeaders);
        std::string line;
        std::string filename;
        while (std::getline(iss, line)) {
            if (!line.empty() && line[line.size()-1]=='\r') line.erase(line.size()-1);
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon+1);
            while (!value.empty() && (value[0]==' '||value[0]=='\t')) value.erase(0,1);
            for (size_t i=0;i<name.size();++i) name[i]=std::tolower(name[i]);
            if (name == "content-disposition") disp = value;
        }
        if (!disp.empty()) {
            size_t fn = disp.find("filename=");
            if (fn != std::string::npos) {
                size_t startq = disp.find('"', fn);
                size_t endq = (startq==std::string::npos)?std::string::npos:disp.find('"', startq+1);
                if (startq!=std::string::npos && endq!=std::string::npos) filename = disp.substr(startq+1, endq-startq-1);
            }
        }

        // Decide destination path
        std::string dest = basePath;
        bool isDir = isDirectory(basePath) || (!uri.empty() && uri[uri.size()-1]=='/');
        if (isDir) {
            std::string clean = sanitizeFilename(filename);
            if (!dest.empty() && dest[dest.size()-1] != '/') dest += "/";
            dest += clean;
        }

        // Write file
        bool existed = fileExists(dest);
        std::ofstream ofs(dest.c_str(), std::ios::binary);
        if (!ofs.is_open()) return false;
        ofs.write(content.c_str(), content.size());
        ofs.close();
        savedCount += 1;
        anyCreated = anyCreated || (!existed);
        lastSavedPath = dest;
    }
    return savedCount > 0;
}

Response epollManager::handlePost(const Request& request, const LocationConfig* location) {
    Response response;
    const std::string uri = request.getUri();

    // CGI?
    if (location && !location->getCgiPass().empty() && isCgiFile(uri, _config.getLocations())) {
        std::string extension = getFileExtension(uri);
        /* std::cout << "\n\n" << extension << "\n\n" << std::endl; */
        const std::map<std::string, std::string>& cgiConfig = location->getCgiPass();
        std::map<std::string, std::string>::const_iterator it = cgiConfig.find(extension);
        if (it == cgiConfig.end()) it = cgiConfig.find(".*");
        if (it != cgiConfig.end()) {
            std::string interpreter = it->second;
            std::string scriptPath;
        if (location->getRoot().empty()) {
            scriptPath = _config.getRoot() + uri;
        }
        else
            scriptPath = location->getRoot() + uri.substr(location->getPath().size());
        CgiHandler cgi;
        return cgi.execute(request, scriptPath, interpreter);
        }
    }

    // Upload handler
    std::string basePath = resolveFilePath(uri);

    if (location && location->hasUploadPath())
        return handleUpload(request, *location);

    // check body size
    size_t maxBody = getEffectiveClientMax(location);
    if (maxBody > 0 && request.getBody().size() > maxBody) {
        response.setStatus(413, "Request Entity Too Large");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("413 Request Too Large", "Body exceeds limit"));
        return response;
    }

    std::string ct = request.getHeader("Content-Type");
    std::string ctl = ct; for (size_t i=0;i<ctl.size();++i) ctl[i]=std::tolower(ctl[i]);

    bool created = false;
    if (ctl.find("multipart/form-data") == 0) {
        // extract boundary
        size_t bpos = ctl.find("boundary=");
        if (bpos == std::string::npos) {
            response.setStatus(400, "Bad Request");
            response.setHeader("Content-Type", "text/html");
            response.setBody(createHtmlResponse("400 Bad Request", "Missing multipart boundary"));
            return response;
        }
        std::string boundary = ctl.substr(bpos + 9);
        if (!boundary.empty() && boundary[0]=='"') {
            size_t endq = boundary.find('"', 1);
            boundary = (endq==std::string::npos)?boundary.substr(1):boundary.substr(1, endq-1);
        }

        size_t savedCount = 0; bool anyCreated = false; std::string lastPath;
        if (!parseMultipartAndSave(request.getBody(), boundary, basePath, uri, savedCount, anyCreated, lastPath)) {
            response.setStatus(400, "Bad Request");
            response.setHeader("Content-Type", "text/html");
            response.setBody(createHtmlResponse("400 Bad Request", "No file parts found"));
            return response;
        }
        created = anyCreated;
        response.setStatus(created ? 201 : 200, created ? "Created" : "OK");
        response.setHeader("Content-Type", "text/html");
        if (!lastPath.empty()) {
            // Try to rebuild a URL from lastPath (best effort)
            response.setHeader("Location", uri);
        }
        response.setBody(createHtmlResponse(created?"201 Created":"200 OK",
                          toString(savedCount) + " file(s) uploaded"));
        return response;
    }

    // Raw body: decide destination
    bool isDir = isDirectory(basePath) || (!uri.empty() && uri[uri.size()-1]=='/');
    if (isDir) {
        response.setStatus(400, "Bad Request");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("400 Bad Request", "No filename specified for upload"));
        return response;
    }
    bool existed = fileExists(basePath);
    std::ofstream ofs(basePath.c_str(), std::ios::binary);
    if (!ofs.is_open()) {
        response.setStatus(403, "Forbidden");
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("403 Forbidden", "Cannot write file"));
        return response;
    }
    const std::string& data = request.getBody();
    ofs.write(data.c_str(), data.size());
    ofs.close();
    response.setStatus(existed ? 200 : 201, existed ? "OK" : "Created");
    response.setHeader("Content-Type", "text/html");
    response.setBody(createHtmlResponse(existed?"200 OK":"201 Created", existed?"File overwritten":"File created"));
    return response;
}
// Modifier createResponseForRequest pour utiliser la config
// epollManager.cpp - IMPLÉMENTATION COMPLÈTE
Response epollManager::createResponseForRequest(const Request& request) {
	Response response;
	
	// 1. Vérifier la version HTTP
	if (request.getVersion() != "HTTP/1.1" && request.getVersion() != "HTTP/1.0") {
		response.setStatus(505, "HTTP Version Not Supported");
		response.setBody(createHtmlResponse("505 HTTP Version Not Supported", 
										  "Unsupported HTTP version: " + request.getVersion()));
		response.setHeader("Content-Type", "text/html");
		return response;
	}

    // 2. Vérifier les méthodes autorisées via config
    std::string method = request.getMethod();
    std::string uri = request.getUri();
    const LocationConfig* location = findLocationConfig(uri);
	
    if (!isMethodAllowed(method, uri)) {
        response.setStatus(405, "Method Not Allowed");
        // Ajouter l'en-tête Allow avec les méthodes autorisées pour cette location
        response.setHeader("Allow", buildAllowHeader(location));
        response.setHeader("Content-Type", "text/html");
        response.setBody(createHtmlResponse("405 Method Not Allowed",
                                            "Method " + method + " is not allowed for this resource"));
        return response;
    }

    // 2bis. Gestion de DELETE (aucun corps attendu)
    if (method == "DELETE") {
        Response delResp = handleDelete(request, location);
        return delResp;
    }

    // 2ter. POST
    if (method == "POST") {
        Response postResp = handlePost(request, location);
        return postResp;
    }

	// 3. Vérifier la taille du body pour POST
	if (method == "POST" && request.getBody().length() > _config.getClientMax()) {
		response.setStatus(413, "Request Entity Too Large");
		response.setBody(createHtmlResponse("413 Request Too Large", 
										  "Request body exceeds maximum allowed size of " + 
										  toString(_config.getClientMax()) + " bytes"));
		response.setHeader("Content-Type", "text/html");
		return response;
        }
    
	// 5. Routing basé sur la configuration
    if (uri == "/") {
        // Servir la page d'accueil configurée (utilise resolveFilePath pour cohérence)
        std::string indexFiles = _config.getIndex();
        
        // Parse multiple index files (space-separated)
        std::vector<std::string> indexList = ParserUtils::split(indexFiles, ' ');
        bool indexFound = false;
        
        for (size_t i = 0; i < indexList.size() && !indexFound; ++i) {
            std::string indexFile = ParserUtils::trim(indexList[i]);
            if (!indexFile.empty()) {
                std::string filePath = resolveFilePath("/" + indexFile);
                if (!filePath.empty() && fileExists(filePath)) {
                    response.setStatus(200, "OK");
                    response.setHeader("Content-Type", getContentType(indexFile));
                    response.setBody(readFileContent(filePath));
                    indexFound = true;
                }
            }
        }
        
        if (!indexFound) {
            response.setStatus(404, "Not Found");
            response.setHeader("Content-Type", "text/html");
            response.setBody(createHtmlResponse("404 Not Found", 
                                              "Index file not found: " + indexFiles));
        }
    }
   else if (location && !location->getCgiPass().empty() && 
		 isCgiFile(uri, _config.getLocations())) {
	std::string extension = getFileExtension(uri);
	const std::map<std::string, std::string>& cgiConfig = location->getCgiPass();
	
	// Debug logging
	std::cout << "Processing CGI request for extension: " << extension << std::endl;
	std::cout << "Available CGI handlers: " << std::endl;
	for (std::map<std::string, std::string>::const_iterator it = cgiConfig.begin(); 
		 it != cgiConfig.end(); ++it) {
		std::cout << "  " << it->first << " => " << it->second << std::endl;
	}
	
	// Check for exact match or wildcard
	std::map<std::string, std::string>::const_iterator it = cgiConfig.find(extension);
	if (it == cgiConfig.end()) {
		it = cgiConfig.find(".*"); // Try wildcard match
	}
	
	if (it != cgiConfig.end()) {
		std::string interpreter = it->second;
		std::string scriptPath = location->getRoot() + uri;
		
		std::cout << "Executing CGI with:" << std::endl
				 << "  Interpreter: " << interpreter << std::endl
				 << "  Script path: " << scriptPath << std::endl;
		
		CgiHandler cgi;
		return cgi.execute(request, scriptPath, interpreter);
	} else {
		response.setStatus(400, "Bad Request");
		response.setBody(createHtmlResponse("400 Bad Request", 
					   "No CGI interpreter configured for extension: " + extension));
	}
}
	else {
        // Servir des fichiers statiques (utilise resolveFilePath)
        std::string filePath = resolveFilePath(uri);
        
        if (!filePath.empty() && fileExists(filePath)) {
            if (isDirectory(filePath)) {
                // Gestion des répertoires
                if (location && location->getAutoindex()) {
                    // Autoindex activé
                    response.setStatus(200, "OK");
                    response.setHeader("Content-Type", "text/html");
                    response.setBody(generateDirectoryListing(filePath, uri));
                } else {
                    // Autoindex désactivé, chercher index files
                    std::string indexFiles = _config.getIndex();
                    std::vector<std::string> indexList = ParserUtils::split(indexFiles, ' ');
                    bool indexFound = false;
                    
                    for (size_t i = 0; i < indexList.size() && !indexFound; ++i) {
                        std::string indexFile = ParserUtils::trim(indexList[i]);
                        if (!indexFile.empty()) {
                            std::string indexFilePath = filePath + "/" + indexFile;
                            if (fileExists(indexFilePath)) {
                                response.setStatus(200, "OK");
                                response.setHeader("Content-Type", getContentType(indexFile));
                                response.setBody(readFileContent(indexFilePath));
                                indexFound = true;
                            }
                        }
                    }
                    
                    if (!indexFound) {
                        response.setStatus(403, "Forbidden");
                        response.setHeader("Content-Type", "text/html");
                        response.setBody(createHtmlResponse("403 Forbidden", 
                                                          "Directory listing forbidden"));
                    }
                }
            } else {
                // Fichier régulier
                response.setStatus(200, "OK");
                response.setHeader("Content-Type", "text/html");
                response.setBody(readFileContent(filePath));
            }
        } else {
            // Fichier non trouvé - utiliser la page d'erreur configurée si disponible
            response.setStatus(404, "Not Found");
            response.setHeader("Content-Type", "text/html");
            response.setBody(createHtmlResponse("404 Not Found", 
                                              "The requested URL " + uri + " was not found"));
        }
	}

	// 6. Headers communs à toutes les réponses
	response.setHeader("Connection", "close");
	response.setHeader("Server", "webserv/1.0");
	response.setHeader("Date", getCurrentDate());

	return response;
}


void	epollManager::handleNewConnection()
{
	struct sockaddr_in	clientAddress;
	socklen_t clientAddrLen = sizeof(clientAddress);

	int	clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddress, &clientAddrLen);
	if (clientSocket == -1)
	{
		ERROR("accept failed");
		return;
	}
	if (_clientBuffers.size() >= MAX_CLIENTS) {
		LOG("Too many connections, rejecting new client");
		close(clientSocket);
		return;
	}
	int	flags = fcntl(clientSocket, F_GETFL, 0);
	fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

	char clientIP[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddress.sin_addr, clientIP, INET_ADDRSTRLEN);
	int clientPort = ntohs(clientAddress.sin_port);

	LOG("New connection accepted from " + std::string(clientIP) + ":" + toString(clientPort));

	// adding client to epoll
	struct epoll_event	event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = clientSocket;

	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientSocket, &event) == -1)
	{
		ERROR("Failed to add client to epoll");
		close(clientSocket);
		return;
	}

	_clientBuffers[clientSocket] = ""; // init empty buffer

	ClientConnection newConn;
    newConn.fd = clientSocket;
    newConn.lastActivity = time(NULL);
    newConn.isReading = false;
    
    _clientConnections[clientSocket] = newConn;
    _clientBuffers[clientSocket] = "";
}



void epollManager::sendErrorResponse(int clientFd, int code, const std::string& message) 
{
	Response response;
	response.setStatus(code, message);
	response.setHeader("Content-Type", "text/html");
	response.setHeader("Connection", "close");
	response.setHeader("Server", "webserv/1.0");
	
	std::string body = createHtmlResponse(toString(code) + " " + message, 
										"Error: " + message + "<br>Please try another URL.");
	response.setBody(body);
	
	std::string responseStr = response.getResponse();
	if (send(clientFd, responseStr.c_str(), responseStr.length(), 0) == -1) {
		ERROR("Failed to send error response to client " + toString(clientFd));
	}
}



void epollManager::handleClientData(int clientFd) 
{
    // Ensure connection entry exists
    if (_clientConnections.find(clientFd) == _clientConnections.end()) {
        ClientConnection conn;
        conn.fd = clientFd;
        conn.lastActivity = time(NULL);
        _clientConnections[clientFd] = conn;
    }
    ClientConnection &conn = _clientConnections[clientFd];
    conn.isReading = true;
    conn.lastActivity = time(NULL);

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;

    while ((bytesRead = recv(clientFd, buffer, BUFFER_SIZE, 0)) > 0) 
    {
        conn.buffer.append(buffer, bytesRead);

        // Guard hard limit
        if (conn.buffer.size() + conn.body.size() > MAX_REQUEST_SIZE) {
            LOG("Request too large from client " + toString(clientFd));
            sendErrorResponse(clientFd, 413, "Request Entity Too Large");
            closeClient(clientFd);
            conn.isReading = false;
            return;
        }

        if (!processConnectionData(clientFd)) {
            continue; // need more data
        }

        // Build Request and respond
        try {
            std::string raw;
            raw += conn.method + " " + conn.uri + " " + conn.version + "\r\n";
            for (std::map<std::string,std::string>::const_iterator it=conn.headers.begin(); it!=conn.headers.end(); ++it) {
                if (it->first == "transfer-encoding" || it->first == "content-length") continue;
                raw += it->first + ": " + it->second + "\r\n";
            }
            if (!conn.body.empty()) raw += std::string("Content-Length: ") + toString(conn.body.size()) + "\r\n";
            raw += "\r\n";
            raw += conn.body;

            Request request(raw);
            if (request.isComplete()) {
                Response response = createResponseForRequest(request);
                std::string responseStr = response.getResponse();
                if (send(clientFd, responseStr.c_str(), responseStr.length(), 0) == -1)
                    ERROR("Failed to send response to client " + toString(clientFd));
                else
                    LOG("Response sent to client " + toString(clientFd));
            } else {
                sendErrorResponse(clientFd, 400, "Bad Request");
            }
        } catch (const std::exception& e) {
            ERROR("Request parsing failed: " + std::string(e.what()));
            sendErrorResponse(clientFd, 400, "Bad Request");
        }

        // Clean and close after processing one request (no keep-alive)
        _clientBuffers[clientFd].clear();
        _clientConnections.erase(clientFd);
        closeClient(clientFd);
        conn.isReading = false;
        return;
    }

    if (bytesRead == 0) {
        LOG("Client " + toString(clientFd) + " disconnected");
        closeClient(clientFd);
    } else if (bytesRead == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ERROR("recv failed for client " + toString(clientFd) + ": " + std::string(strerror(errno)));
            closeClient(clientFd);
        }
    }
    conn.isReading = false;
}


void epollManager::closeClient(int clientFd) 
{
	epoll_ctl(_epollFd, EPOLL_CTL_DEL, clientFd, NULL);
	close(clientFd);
	_clientBuffers.erase(clientFd);
	LOG("Connection closed for client " + toString(clientFd));
}

/* void cleanupIdleConnections() {
    time_t now = time(NULL);
    
    for (auto it = _clientConnections.begin(); it != _clientConnections.end(); ) {
        ClientConnection& client = it->second;
        double idle = difftime(now, client.lastActivity);
        
        if (client.buffer.empty() && idle > CONNECTION_TIMEOUT) {
            // Connexion établie mais aucune donnée reçue
            closeClient(client.fd);
            it = _clientConnections.erase(it);
        }
        else if (!client.buffer.empty() && idle > READ_TIMEOUT) {
            // Données reçues mais requête incomplète
            closeClient(client.fd);
            it = _clientConnections.erase(it);
        }
        else if (idle > KEEP_ALIVE_TIMEOUT) {
            // Connexion keep-alive inactive
            closeClient(client.fd);
            it = _clientConnections.erase(it);
        }
        else {
            ++it;
        }
    }
}
void checkTimeouts() {
    time_t now = time(NULL);
    for (auto it = _clientConnections.begin(); it != _clientConnections.end(); ) {
        if (now - it->second.lastActivity > CONNECTION_TIMEOUT) {
            LOG("Closing idle connection: " + toString(it->first));
            closeClient(it->first);
            it = _clientConnections.erase(it);
        } else {
            ++it;
        }
    }
} */

void epollManager::run() 
{
	struct epoll_event events[MAX_EVENTS];
	
	INFO("Starting epoll event loop...");

	while (true) {
		int numEvents = epoll_wait(_epollFd, events, MAX_EVENTS, -1);
		if (numEvents == -1) {
			if (errno == EINTR) continue;
			ERROR("epoll_wait failed");
			break;
		}
		cleanupIdleConnections();
		for (int i = 0; i < numEvents; ++i) {
			if (events[i].data.fd == _serverSocket) 
				handleNewConnection();
			else
				handleClientData(events[i].data.fd);
		}
	}
}
