#pragma once

#include <string>
#include <vector>
#include <map>
#include <iostream>

class LocationConfig {
	private:
			std::string _path;
			std::string _root;
			std::string	_index;

			size_t		_clientMax;
			bool		_autoindex;

			std::string					_limit_except;
			std::vector<std::string>	_allowedMethods;
			std::vector<std::string> 	_IPallow;
			std::vector<std::string> 	_IPdeny;

			std::map<std::string, std::string>	_cgiParams;
			std::map<std::string, std::string>	_cgiPass;

			// Redirection (return <code> <url>)
			bool _hasReturn;
			int  _returnCode;
			std::string _returnUrl;

	public:
			int lineOffset;
			LocationConfig();
			~LocationConfig();
			void setPath(const std::string& path);
			void setRoot(const std::string& root);
			void setIndex(const std::string& index);
			void setLimitExcept(const std::string& limit_except);
			void setClientMax(const size_t client);
			void setAutoindex(const std::string& autoindex);
			void setAllowedMethods(std::vector<std::string>& allowedMethods);
			void setCgiParams(const std::map<std::string, std::string>& cgiParams);
			void addCgiPass(const std::string& extension, const std::string& interpreter);
			void addCgiParam(const std::string& key, const std::string& value);
			void addAllowedMethod(const std::string& method);
			void addAllow(const std::string& ip);
			void addDeny(const std::string& ip);
			const std::string& getPath()const;
			const std::string& getRoot()const;
			const std::string& getIndex()const;
			const std::string& getLimitExcept()const;
			size_t getClientMax()const;
			bool getAutoindex()const;
			const std::vector<std::string>& getAllowedMethods()const;
			const std::map<std::string, std::string>& getCgiParams()const;
			const std::map<std::string, std::string>& getCgiPass()const;
			const std::vector<std::string>& getIPallow()const;
			const std::vector<std::string>& getIPdeny()const;
			std::string getCgiInterpreter(const std::string& extension) const;
   			bool isCgiRequest(const std::string& uri) const;
			void printConfigLocation() const;

			// Redirection API
			void setReturn(int code, const std::string& url);
			bool hasReturn() const;
			int  getReturnCode() const;
			const std::string& getReturnUrl() const;
};
