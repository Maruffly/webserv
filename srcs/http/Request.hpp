#pragma once

#include "../../include/Webserv.hpp"

class	Request
{
	private:
		std::string	_rawRequest;
		std::string	_method;
		std::string	_uri;			// index.html..
		std::string	_version;		// HTTP/1.1..
		bool		_isComplete;	// if request fully received


	public:
		Request();
		Request(const std::string &rawRequest);
		~Request();

		void		parseRequest();

		// getters
		std::string	getRawRequest() const;
		std::string	getMethod() const;
		std::string	getUri() const;
		std::string	getVersion() const;
		bool		isComplete() const;

		// debug
		void		print() const;

		// errors
		void	parseError();
};