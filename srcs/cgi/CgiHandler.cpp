#include "CgiHandler.hpp"

#include "../utils/Utils.hpp"

CgiHandler::CgiHandler() {}
CgiHandler::~CgiHandler() {}

std::vector<char*> CgiHandler::prepareEnvArray() {
	std::vector<char*> envArray;
	for (std::map<std::string, std::string>::const_iterator it = _env.begin(); 
		 it != _env.end(); ++it) {
		std::string envStr = it->first + "=" + it->second;
		char* envEntry = strdup(envStr.c_str());
		envArray.push_back(envEntry);
	}
	envArray.push_back(NULL);
	return envArray;
}

std::vector<char*> CgiHandler::prepareArgs(const std::string& scriptPath, const std::string& interpreter) {
	std::vector<char*> args;
	if (!interpreter.empty()) {
		args.push_back(strdup(interpreter.c_str()));  // argv[0] = "python3"
		args.push_back(strdup(scriptPath.c_str()));   // argv[1] = "script.py"
	} else {
		args.push_back(strdup(scriptPath.c_str()));   // argv[0] = "./script"
	}
	args.push_back(NULL);
	return args;
}

void CgiHandler::setupEnvironment(const Request& request, const std::string& scriptPath) {
	_env.clear();
	_env["GATEWAY_INTERFACE"] = "CGI/1.1";
	_env["SERVER_PROTOCOL"] = "HTTP/1.1";
	_env["SERVER_SOFTWARE"] = "webserv/1.0";
	_env["REQUEST_METHOD"] = request.getMethod();
	_env["PATH_INFO"] = request.getUri();
	_env["SCRIPT_FILENAME"] = scriptPath;
	_env["SCRIPT_NAME"] = request.getUri();
	_env["QUERY_STRING"] = request.getUri().find('?') != std::string::npos ? 
						  request.getUri().substr(request.getUri().find('?') + 1) : "";
	
	// set http header as env var
	const std::map<std::string, std::string>& headers = request.getHeaders();
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); 
		 it != headers.end(); ++it) {
		std::string envName = "HTTP_" + toUpperCase(replaceChars(it->first, "-", "_"));
		_env[envName] = it->second;
	}
	// content-Length / Content-Type
	if (request.getMethod() == "POST") {
		_env["CONTENT_LENGTH"] = toString(request.getBody().size());
		_env["CONTENT_TYPE"] = request.getHeader("Content-Type");
	}
}

Response CgiHandler::execute(const Request& request, 
						   const std::string& scriptPath, 
						   const std::string& interpreter) {
	Response response;

	if (!fileExists(scriptPath)) {
		response.setStatus(404, "Not Found");
		return response;
	}
	if (access(scriptPath.c_str(), X_OK) != 0) {
		response.setStatus(403, "Forbidden");
		return response;
	}

	setupEnvironment(request, scriptPath);

	int pipe_in[2], pipe_out[2];
	if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
		response.setStatus(500, "Internal Server Error");
		return response;
	}

	pid_t pid = fork();
	if (pid == -1) {
		response.setStatus(500, "Internal Server Error");
		return response;
	}
	if (pid == 0) { // child process
		close(pipe_in[1]);
		close(pipe_out[0]);
		
		dup2(pipe_in[0], STDIN_FILENO);
		dup2(pipe_out[1], STDOUT_FILENO);
		
		std::vector<char*> envArray = prepareEnvArray();
		std::vector<char*> args = prepareArgs(scriptPath, interpreter);
		
		if (interpreter.empty()) {
			execve(scriptPath.c_str(), args.data(), envArray.data());
		}
		else
			execve(interpreter.c_str(), args.data(), envArray.data());

		exit(1);
	} 
	else { // parent process
		close(pipe_in[0]);
		close(pipe_out[1]);
		
		// write body request into CGI
		if (request.getMethod() == "POST") {
			write(pipe_in[1], request.getBody().c_str(), request.getBody().size());
		}
		close(pipe_in[1]);
		
		// read cgi response
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
	}
	return response;
}