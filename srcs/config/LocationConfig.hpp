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
			std::string	_limit_except;
			size_t		_clientMax;
			bool		_autoindex;
			std::vector<std::string>	_allowedMethods;
			std::map<std::string, std::string> _cgiParams;
			std::string _cgiPass;
			std::vector<std::string> _IPallow;
			std::vector<std::string> _IPdeny;

	public:
			LocationConfig();
			~LocationConfig();
			void setPath(const std::string& path);
			void setRoot(const std::string& root);
			void setIndex(const std::string& index);
			void setLimitExcept(const std::string& limit_except);
			void setClientMax(const size_t client);
			void setAutoindex(const bool autoindex);
			void setAllowedMethods(std::vector<std::string>& allowedMethods);
			void setCgiParams(const std::map<std::string, std::string>& cgiParams);
			void setCgiPass(const std::string& cgiPass);
			void addCgiParam(const std::string& key, const std::string& value);
			void addAllowedMethod(const std::string& method);
			void addAllow(const std::string& ip);
			void addDeny(const std::string& ip);
			const std::string& getPath();
			const std::string& getRoot();
			const std::string& getIndex();
			const std::string& getLimitExcept();
			const size_t getClientMax();
			const bool getAutoindex();
			const std::vector<std::string>& getAllowedMethods();
			const std::map<std::string, std::string>& getCgiParams();
			const std::string& getCgiPass();
			std::vector<std::string>& getIPallow();
			std::vector<std::string>& getIPdeny();
			void printConfigLocation() const;
};