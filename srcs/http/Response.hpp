#pragma once

#include "../../include/Webserv.hpp"
#include "../utils/Utils.hpp"

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

		bool	setFile(const std::string &filePath);

		std::string	getResponse() const;
};