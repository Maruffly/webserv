#pragma once

#include "../../include/Webserv.hpp"
#include "../utils/ParserUtils.hpp"

class	Request
{
	private:
		std::string	_rawRequest;
		std::string	_method;
		std::string	_uri;			// index.html..
		std::string	_version;		// HTTP/1.1..
		std::map<std::string, std::string>	_headers;
		std::string	_body;
		bool		_isComplete;	// if request fully received
		std::map<std::string, std::string> _cookies;


	public:
		Request();
		Request(const std::string &rawRequest);
		~Request();

		void		parseRequest();

		// getters
		const std::map<std::string, std::string>& getHeaders() const;
		std::string	getRawRequest() const;
		std::string	getMethod() const;
		std::string	getUri() const;
		std::string	getVersion() const;
		std::string getHeader(const std::string& name) const;
		std::string	getBody() const;
		std::string getCookie(const std::string& name) const;
    	const std::map<std::string, std::string>& getCookies() const;
		bool		isComplete() const;

		// debug
		void		print() const;

		// errors
		void		parseError();
		void parseCookies(const std::string& cookieHeader);
};