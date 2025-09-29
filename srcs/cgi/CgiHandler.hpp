#pragma once

#include "../http/Request.hpp"
#include "../http/Response.hpp"
#include "../config/LocationConfig.hpp"
#include "../config/ServerConfig.hpp"
#include "../../include/Webserv.hpp"
#include <sys/wait.h>
#include "../utils/ParserUtils.hpp"


class CgiHandler {
	private:
			std::map<std::string, std::string> _env;
			std::string _output;
			//int			_exitStatus;
			void setupEnvironment(const Request& request, const std::string& scriptPath,
						     const LocationConfig* location, const ServerConfig& serverConfig);
			std::vector<char*> prepareEnvArray();
			std::vector<char*> prepareArgs(const std::string& scriptPath,  const std::string& interpreter="");

	public:
			CgiHandler();
			~CgiHandler();
			Response execute(const Request& request, const std::string& scriptPath, const std::string& interpreter,
						   const LocationConfig* location, const ServerConfig& serverConfig);
};
/* 
void readParseCGI(int pipe_out[2], int pid, Response& response){
	char buffer[4096];
		std::string cgiOutput;
		ssize_t bytesRead;
		
		while ((bytesRead = read(pipe_out[0], buffer, sizeof(buffer))) > 0) {
			cgiOutput.append(buffer, bytesRead);
		}
		close(pipe_out[0]);
		waitpid(pid, NULL, 0);

		// parse cgi response
		size_t headerEnd = cgiOutput.find("\r\n\r\n");
		if (headerEnd != std::string::npos) {
			std::string headersPart = cgiOutput.substr(0, headerEnd);
			std::string body = cgiOutput.substr(headerEnd + 4);
			
			std::vector<std::string> headerLines = ParserUtils::split(headersPart, '\n');
			for (size_t i = 0; i < headerLines.size(); ++i) {
				 if (!headerLines[i].empty() && headerLines[i][headerLines[i].size()-1] == '\r')
					headerLines[i].erase(headerLines[i].size()-1);				

				size_t colonPos = headerLines[i].find(':');
				if (colonPos != std::string::npos) {
					std::string name = headerLines[i].substr(0, colonPos);
					std::string value = headerLines[i].substr(colonPos + 1);
					response.setHeader(ParserUtils::trim(name), ParserUtils::trim(value));
				}
			}
			response.setBody(body);
		} 
		else
			response.setBody(cgiOutput);
		response.setStatus(200, "OK");
		if (response.getHeaders().find("Content-Type") == response.getHeaders().end())
			response.setHeader("Content-Type", "text/html");
} */
