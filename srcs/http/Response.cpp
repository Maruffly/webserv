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


void	Response::setBody(const std::string &body)
{
	_response += "\r\n" + body;
}


std::string	Response::getResponse() const
{
	return _response;
}