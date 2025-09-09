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