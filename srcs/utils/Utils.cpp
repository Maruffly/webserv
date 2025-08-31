#include "Utils.hpp"


std::string getCurrentDate() 
{
    time_t now = time(0);
    struct tm tm = *gmtime(&now);
    char buf[100];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf);
}


std::string getContentType(const std::string& uri) 
{
    size_t dotPos = uri.find_last_of('.');
    if (dotPos == std::string::npos) 
        return "text/html";
    
    std::string extension = uri.substr(dotPos + 1);
    
    // Convertir en lowercase pour la casse insensible
    for (size_t i = 0; i < extension.length(); ++i) 
        extension[i] = std::tolower(extension[i]);
    
    if (extension == "html" || extension == "htm") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "application/javascript";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "png") return "image/png";
    if (extension == "gif") return "image/gif";
    if (extension == "json") return "application/json";
    if (extension == "txt") return "text/plain";
    if (extension == "pdf") return "application/pdf";
    if (extension == "xml") return "application/xml";
    
    return "application/octet-stream";
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