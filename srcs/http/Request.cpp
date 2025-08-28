#include "Request.hpp"

Request::Request() : _isComplete(false) {}

Request::Request(const std::string& rawRequest) : _rawRequest(rawRequest), _isComplete(false)
{
	parseRequest();
}

Request::~Request() {}


void Request::parseRequest() 
{
    _isComplete = false;
    _method.clear();
    _uri.clear();
    _version.clear();

    if (_rawRequest.empty()) 
	{
        parseError();
        return;
    }

    // finds end of first line
    size_t endOfFirstLine = _rawRequest.find("\r\n");
    if (endOfFirstLine == std::string::npos) 
	{
        parseError();
        return;
    }

    // extract and parse first line
    std::string firstLine = _rawRequest.substr(0, endOfFirstLine);
    
    // Parse METHOD URI VERSION
    size_t firstSpace = firstLine.find(' ');
    if (firstSpace == std::string::npos || firstSpace == 0) 
	{
        parseError();
        return;
    }
    
    size_t secondSpace = firstLine.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) 
	{
        parseError();
        return;
    }

    // check string's end
    if (secondSpace + 1 >= firstLine.length()) 
	{
        parseError();
        return;
    }

    _method = firstLine.substr(0, firstSpace);
    _uri = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    _version = firstLine.substr(secondSpace + 1);

    // Validation basique des valeurs
    if (_method.empty() || _uri.empty() || _version.empty()) 
	{
        parseError();
        return;
    }

    _isComplete = true;
}


// getters
std::string	Request::getRawRequest() const { return _rawRequest; }
std::string Request::getMethod() const { return _method; }
std::string Request::getUri() const { return _uri; }
std::string Request::getVersion() const { return _version; }
bool 		Request::isComplete() const { return _isComplete; }




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


void Request::parseError() 
{
    _isComplete = false;
    _method.clear();
    _uri.clear();
    _version.clear();
}