#include "Webserv.hpp"
#include "Utils.hpp"



char* ft_strdup(const std::string& value)
{
    size_t length = value.size();
    char* duplicate = new char[length + 1];
    std::memcpy(duplicate, value.c_str(), length + 1);
    return duplicate;
}

std::string getCurrentDate() 
{
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    char buf[100];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf);
}


std::string createHtmlResponse(const std::string& title, const std::string& content) 
{
    std::string html = "<!DOCTYPE html>\n";
    html += "<html lang=\"en\">\n";
    html += "<head>\n";
    html += "    <meta charset=\"UTF-8\">\n";
    html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "    <title>" + title + "</title>\n";
    html += "    <style>\n";
    html += "        body { font-family: Arial, sans-serif; margin: 40px; line-height: 1.6; }\n";
    html += "        h1 { color: #333; border-bottom: 2px solid #eee; padding-bottom: 10px; }\n";
    html += "        .container { max-width: 800px; margin: 0 auto; }\n";
    html += "        .error { color: #d32f2f; }\n";
    html += "        .success { color: #388e3c; }\n";
    html += "    </style>\n";
    html += "</head>\n";
    html += "<body>\n";
    html += "    <div class=\"container\">\n";
    html += "        <h1>" + title + "</h1>\n";
    html += "        <p>" + content + "</p>\n";
    html += "    </div>\n";
    html += "</body>\n";
    html += "</html>";
    
    return html;
}




bool fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool isDirectory(const std::string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) return false;
    return S_ISDIR(buffer.st_mode);
}

std::string readFileContent(const std::string& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        ERROR("Cannot open file: " + path);
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string getContentType(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos) return "text/plain";
    
    std::string extension = path.substr(dotPos + 1);
    
    // Convertir en minuscules pour case-insensitive
    for (size_t i = 0; i < extension.size(); ++i) {
        extension[i] = std::tolower(extension[i]);
    }
    
    static std::map<std::string, std::string> contentTypes;
    if (contentTypes.empty()) {
        contentTypes["html"] = "text/html";
        contentTypes["htm"] = "text/html";
        contentTypes["css"] = "text/css";
        contentTypes["js"] = "application/javascript";
        contentTypes["json"] = "application/json";
        contentTypes["jpg"] = "image/jpeg";
        contentTypes["jpeg"] = "image/jpeg";
        contentTypes["png"] = "image/png";
        contentTypes["gif"] = "image/gif";
        contentTypes["bmp"] = "image/bmp";
        contentTypes["ico"] = "image/x-icon";
        contentTypes["txt"] = "text/plain";
        contentTypes["pdf"] = "application/pdf";
        contentTypes["zip"] = "application/zip";
        contentTypes["xml"] = "application/xml";
    }
    
    std::map<std::string, std::string>::iterator it = contentTypes.find(extension);
    if (it != contentTypes.end()) {
        return it->second;
    }
    
    return "application/octet-stream";
}

// Fonction pour générer le listing de répertoire (simplifié)
std::string generateDirectoryListing(const std::string& dirPath, const std::string& uri) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return "<h1>Error reading directory</h1>";
    }
    
    std::string html = "<!DOCTYPE html><html><head><title>Index of " + uri + "</title></head><body>";
    html += "<h1>Index of " + uri + "</h1><hr><ul>";
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        std::string fullPath = dirPath + "/" + name;
        std::string link = uri + (uri == "/" ? "" : "/") + name;
        
        struct stat fileStat;
        stat(fullPath.c_str(), &fileStat);
        
        html += "<li><a href=\"" + link + "\">" + name;
        if (S_ISDIR(fileStat.st_mode)) {
            html += "/";
        }
        html += "</a> (" + toString(fileStat.st_size) + " bytes)</li>";
    }
    
    html += "</ul><hr></body></html>";
    closedir(dir);
    return html;
}

bool isCgiFile(const std::string& uri, const std::vector<LocationConfig>& locations) {
    for (size_t i = 0; i < locations.size(); ++i) {
        const LocationConfig& loc = locations[i];
        if (uri.find(loc.getPath()) == 0 && loc.isCgiRequest(uri))
            return true;
    }
    return false;
}

std::string toUpperCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string replaceChars(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

std::string getFileExtension(const std::string& uri) {
    std::string cleanUri = uri;
    size_t queryPos = uri.find('?');
    if (queryPos != std::string::npos) {
        cleanUri = uri.substr(0, queryPos);
    }
    
    size_t dotPos = cleanUri.find_last_of('.');
    if (dotPos != std::string::npos && dotPos < cleanUri.length() - 1) {
        std::string ext = cleanUri.substr(dotPos); // Ceci inclut le point
        std::cout << "🔍 getFileExtension: '" << uri << "' -> '" << ext << "'" << std::endl;
        return ext;
    }
    std::cout << "🔍 getFileExtension: '" << uri << "' -> ''" << std::endl;
    return "";
}

// Recursively creates folders (mkdir -p)
int mkdirRecursive(const std::string& path, mode_t mode) {
    if (path.empty()) return -1;

    std::string current;
    std::stringstream ss(path);
    std::string segment;

    if (path[0] == '/')
        current = "/";

    while (std::getline(ss, segment, '/')) {
        if (segment.empty()) continue;
        if (current != "/" && !current.empty())
            current += "/";
        current += segment;

        if (!isDirectory(current)) {
            if (mkdir(current.c_str(), mode) != 0) {
                if (errno == EEXIST) continue;
                return -1;
            }
        }
    }
    return 0;
}


static bool dirExists(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}


bool ensureDirectoryExists(const std::string& path, bool create)
{
    if (path.empty()) return false;
    if (dirExists(path)) return true;
    if (!create) return false;
    // recursive create
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        cur.push_back(c);
        if (c == '/' || i == path.size() - 1) {
            if (!cur.empty() && cur != "/" && !dirExists(cur)) 
            {
                if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) 
                    return false;
            }
        }
    }
    return dirExists(path);
}


std::string toLowerCase(const std::string &str) 
{
    std::string result = str;
    for (size_t i = 0; i < result.size(); ++i) 
        result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
    
    return result;
}


std::string dirnameOf(const std::string& path) 
{
    size_t p = path.find_last_of('/');
    if (p == std::string::npos) return std::string(".");
    if (p == 0) return std::string("/");
    return path.substr(0, p);
}


void safeClose(int pipefd[2])
{
    if (pipefd[0] != -1)
        close(pipefd[0]);
    if (pipefd[1] != -1)
        close(pipefd[1]);
}
