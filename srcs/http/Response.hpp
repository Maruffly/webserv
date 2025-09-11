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
		std::map<std::string, std::string> _cookies;

	public:
		const std::map<std::string, std::string>& getHeaders() const;
		Response();
		~Response();

		void	setStatus(int code, const std::string &message);
		void	setHeader(const std::string &name, const std::string &value);
		void	setBody(const std::string &body);

		std::string	getResponse() const;
};