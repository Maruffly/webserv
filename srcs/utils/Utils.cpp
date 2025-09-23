#include <sys/stat.h> 
#include <unistd.h>
#include "Utils.hpp"



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




#include <sys/stat.h> 
#include <unistd.h>
#include "Utils.hpp"


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
    // Vérifier l'extension pour CGI
    (void)locations;
    size_t dotPos = uri.find_last_of('.');
    if (dotPos == std::string::npos) return false;
    
    std::string extension = uri.substr(dotPos + 1);
    for (size_t i = 0; i < extension.size(); ++i) {
        extension[i] = std::tolower(extension[i]);
    }
    
    // Extensions CGI courantes
    return (extension == "php" || extension == "py" || extension == "pl" || extension == "cgi");
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

std::string getFileExtension(const std::string& path) {
    size_t dot = path.find_last_of(".");
    if (dot == std::string::npos) {
        return "";
    }
    return path.substr(dot + 1);
}

// Crée récursivement les dossiers (mkdir -p)
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
