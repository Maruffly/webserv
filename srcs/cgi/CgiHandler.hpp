#pragma once

#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../../include/Webserv.hpp"
#include <sys/wait.h>
#include "../utils/ParserUtils.hpp"


class CgiHandler {
	private:
			std::map<std::string, std::string> _env;
			std::string _output;
			//int			_exitStatus;
			void setupEnvironment(const Request& request, const std::string& scriptPath);
			std::vector<char*> prepareEnvArray();
			std::vector<char*> prepareArgs(const std::string& scriptPath,  const std::string& interpreter="");
	public:
			CgiHandler();
			~CgiHandler();
			Response execute(const Request& request, const std::string& scriptPath, const std::string& interpreter);
};