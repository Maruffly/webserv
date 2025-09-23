#pragma once

#include "../../include/Webserv.hpp"

class	Response
{
	private:
		std::string	_response;
		std::string	_body;
		std::map<std::string, std::string> _headers;
		std::string _statusLine;
		int _statusCode;
	public:
		const std::map<std::string, std::string>& getHeaders() const;
		Response();
		~Response();

		void	setStatus(int code, const std::string &message);
		void	setHeader(const std::string &name, const std::string &value);
		void	setBody(const std::string &body);

		std::string	getResponse() const;
};

/* struct ResponseBuilder {
    static Response createError(int code, const std::string& message) {
        Response response;
        response.setStatus(code, getStatusText(code));
        response.setHeader("Content-Type", "text/html");
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        response.setBody(createHtmlResponse(toString(code) + " " + getStatusText(code), message));
        return response;
    }
    
    static Response createSuccess(const std::string& content, const std::string& contentType = "text/html") {
        Response response;
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", contentType);
        response.setHeader("Connection", "close");
        response.setHeader("Server", "webserv/1.0");
        response.setHeader("Date", getCurrentDate());
        response.setBody(content);
        return response;
    }
}; */