#pragma once

#include "../../include/Webserv.hpp"

class	Response
{
	private:
		std::string	_response;
		std::string	_body;
	
	public:
		Response();
		~Response();

		void	setStatus(int code, const std::string &message);
		void	setHeader(const std::string &name, const std::string &value);
		void	setBody(const std::string &body);

		std::string	getResponse() const;
};