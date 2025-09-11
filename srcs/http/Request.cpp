#include "Request.hpp"

#define PARSE_ERROR() do { _isComplete = false; return; } while (0)

Request::Request() : _isComplete(false) {}

Request::Request(const std::string& rawRequest) : _rawRequest(rawRequest), _isComplete(false)
{
	parseRequest();
}

Request::~Request() {}


void    Request::parseError()
{
	_isComplete = false;
	return; 
}


void Request::parseRequest() 
{
	// Réinitialise l'état
	_isComplete = false;
	_method.clear();
	_uri.clear();
	_version.clear();
	_headers.clear();
	_body.clear();

	if (_rawRequest.empty()) parseError();

	// find headers / body separation
	size_t headerEnd = _rawRequest.find("\r\n\r\n");
	if (headerEnd == std::string::npos) parseError();

	// extract headers
	std::string headersPart = _rawRequest.substr(0, headerEnd);
	
	// extract body if existing
	if (headerEnd + 4 < _rawRequest.length())
		_body = _rawRequest.substr(headerEnd + 4);

	// parse first line
	size_t endOfFirstLine = headersPart.find("\r\n");
	if (endOfFirstLine == std::string::npos) parseError();

	std::string firstLine = headersPart.substr(0, endOfFirstLine);
	
	size_t firstSpace = firstLine.find(' ');
	if (firstSpace == std::string::npos || firstSpace == 0) parseError();
	
	size_t secondSpace = firstLine.find(' ', firstSpace + 1);
	if (secondSpace == std::string::npos) parseError();

	if (secondSpace + 1 >= firstLine.length()) parseError();

	_method = firstLine.substr(0, firstSpace);
	_uri = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
	_version = firstLine.substr(secondSpace + 1);

	// parse headers
	size_t lineStart = endOfFirstLine + 2; // after first \r\n
	while (lineStart < headersPart.length()) 
	{
		size_t lineEnd = headersPart.find("\r\n", lineStart);
		if (lineEnd == std::string::npos) break;

		std::string headerLine = headersPart.substr(lineStart, lineEnd - lineStart);
		
		// separates name & headers's value
		size_t colonPos = headerLine.find(':');
		if (colonPos != std::string::npos) 
		{
			std::string headerName = headerLine.substr(0, colonPos);
			std::string headerValue = headerLine.substr(colonPos + 1);
			
			// trim spaces
			headerName.erase(0, headerName.find_first_not_of(" \t"));
			headerName.erase(headerName.find_last_not_of(" \t") + 1);
			
			headerValue.erase(0, headerValue.find_first_not_of(" \t"));
			headerValue.erase(headerValue.find_last_not_of(" \t") + 1);
			
			// stores in the map (lowercse for case insensitivity)
			for (size_t i = 0; i < headerName.length(); ++i) 
				headerName[i] = std::tolower(headerName[i]);
			
			_headers[headerName] = headerValue;
		}
		
		lineStart = lineEnd + 2;
	}

	_isComplete = true;
}


// getters
std::string	Request::getRawRequest() const { return _rawRequest; }
std::string Request::getMethod() const { return _method; }
std::string Request::getUri() const { return _uri; }
std::string Request::getVersion() const { return _version; }
std::string Request::getBody() const { return _body; }
bool 		Request::isComplete() const { return _isComplete; }

std::string	Request::getHeader(const std::string &name) const
{
	std::string	lowerName = name;

	for (size_t i = 0; i < lowerName.length(); ++i)
		lowerName[i] = std::tolower(lowerName[i]);

	std::map<std::string, std::string>::const_iterator it = _headers.find(lowerName);
	if (it != _headers.end())
		return it->second;

	return "";
}

const std::map<std::string, std::string>& Request::getHeaders() const {
        return _headers;
    }

void Request::parseCookies(const std::string& cookieHeader) {
    std::vector<std::string> cookies = ParserUtils::split(cookieHeader, ';');
    
    for (size_t i = 0; i < cookies.size(); ++i) {
        std::string cookie = ParserUtils::trim(cookies[i]);
        size_t equalsPos = cookie.find('=');
        
        if (equalsPos != std::string::npos) {
            std::string name = cookie.substr(0, equalsPos);
            std::string value = cookie.substr(equalsPos + 1);
            _cookies[ParserUtils::trim(name)] = ParserUtils::trim(value);
        }
    }
}

std::string Request::getCookie(const std::string& name) const {
    std::map<std::string, std::string>::const_iterator it = _cookies.find(name);
    if (it != _cookies.end()) {
        return it->second;
    }
    return "";
}

void Request::print() const 
{
	std::cout << "=== HTTP REQUEST ===" << std::endl;
	std::cout << _rawRequest;

	if (!_rawRequest.empty() && _rawRequest[_rawRequest.length()-1] != '\n')
		std::cout << std::endl; // newline if absent

	std::cout << "====================" << std::endl;
	std::cout << "Method: " << (_method.empty() ? "N/A" : _method) << std::endl;
	std::cout << "URI: " << (_uri.empty() ? "N/A" : _uri) << std::endl;
	std::cout << "Version: " << (_version.empty() ? "N/A" : _version) << std::endl;
	std::cout << "Complete: " << (_isComplete ? "Yes" : "No") << std::endl;
	std::cout << "====================" << std::endl;
}
