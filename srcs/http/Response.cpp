#include "Response.hpp"


Response::Response() {}
Response::~Response() {}


void	Response::setStatus(int code, const std::string &message)
{
	_response = "HTTP/1.1 " + toString(code) + " " + message + "\r\n";
}


void	Response::setHeader(const std::string &name, const std::string &value)
{
	_response += name + ": " + value + "\r\n";
}


void Response::setBody(const std::string& body) 
{
    _body = body;
    // dynamic content length
    setHeader("Content-Length", toString(_body.length()));
    _response += "\r\n" + _body;
}


std::string	Response::getResponse() const
{
	return _response;
}


bool Response::setFile(const std::string& filePath) 
{
    // Protection contre les directory traversal attacks
    if (filePath.find("../") != std::string::npos ||
        filePath.find("//") != std::string::npos ||
        filePath.find("~") != std::string::npos) {
        LOG("Security alert: Path traversal attempt detected: " + filePath);
        return false;
    }

    // Vérifie que le chemin est dans le répertoire autorisé
    std::string canonicalPath = filePath;
    if (canonicalPath.find("./www/") != 0 && canonicalPath != "./www/index.html") 
    {
        LOG("Security alert: Attempt to access outside web directory: " + filePath);
        return false;
    }

    // Vérifie l'existence et le type du fichier
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) == -1) 
        return false; // Fichier n'existe pas

    // Vérifie que c'est un fichier régulier (pas un dossier, pipe, etc.)
    if (!S_ISREG(fileStat.st_mode))
        return false;

    // Vérifie les permissions de lecture
    if (access(filePath.c_str(), R_OK) == -1)
        return false; // Permission denied

    // Vérifie la taille du fichier (limite à 10MB)
    if (fileStat.st_size > 10 * 1024 * 1024) 
    {
        LOG("File too large: " + filePath + " (" + toString(fileStat.st_size) + " bytes)");
        return false;
    }

    // Ouvre et lit le fichier
    std::ifstream file(filePath.c_str(), std::ios::binary);
    if (!file.is_open())
        return false;

    // Lit le contenu
    std::ostringstream fileContent;
    fileContent << file.rdbuf();
    _body = fileContent.str();
    file.close();

    // Définit les headers automatiquement
    setHeader("Content-Length", toString(_body.length()));
    setHeader("Content-Type", getContentType(filePath));

    LOG("Successfully served file: " + filePath + " (" + toString(_body.length()) + " bytes)");
    return true;
}