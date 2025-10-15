#include "../../include/Webserv.hpp"
#include "Response.hpp"


Response::Response() {}
Response::~Response() {}

const std::map<std::string, std::string>& Response::getHeaders() const {
		return _headers;
}

int Response::getStatusCode() const {
		return _statusCode;
}

std::string Response::getBody() const {
		return _body;
}


void	Response::setStatus(int code, const std::string &message)
{
	_statusCode = code;
	_statusLine = "HTTP/1.1 " + toString(code) + " " + message + "\r\n";
}


void	Response::setHeader(const std::string &name, const std::string &value)
{
	 _headers[name] = value;
	//_response += name + ": " + value + "\r\n";
}


void Response::setBody(const std::string& body) 
{
	_body = body;
	// dynamic content length
	if (_headers.find("Content-Length") == _headers.end())
        setHeader("Content-Length", toString(_body.length()));
	//_response += "\r\n" + _body;
}

size_t Response::getBodyLength() const {
    return _body.length();
}

std::string	Response::getResponse() const
{
	std::string response = _statusLine;
	// add all headers
		for (std::map<std::string, std::string>::const_iterator it = _headers.begin();
			 it != _headers.end(); ++it) {
			response += it->first + ": " + it->second + "\r\n";
		}
		
		response += "\r\n" + _body;
		return response;
}
