#include "CgiHandler.hpp"

#include "../utils/Utils.hpp"
#include <signal.h>

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

void CgiHandler::setupEnvironment(const Request& request, const std::string& scriptPath,
                                  const LocationConfig* location, const ServerConfig& serverConfig) {
    _env.clear();
    
    // Variables CGI standard
    _env["GATEWAY_INTERFACE"] = "CGI/1.1";
    _env["SERVER_PROTOCOL"] = "HTTP/1.1";
    _env["SERVER_SOFTWARE"] = "webserv/1.0";
    _env["REQUEST_METHOD"] = request.getMethod();
    
    // CORRECTION: Séparer PATH_INFO et SCRIPT_NAME
    std::string uri = request.getUri();
    size_t queryPos = uri.find('?');
    std::string pathOnly = (queryPos != std::string::npos) ? uri.substr(0, queryPos) : uri;
    
    _env["SCRIPT_NAME"] = pathOnly;
    _env["SCRIPT_FILENAME"] = scriptPath;
    _env["PATH_INFO"] = pathOnly;
    
    // Query string
    _env["QUERY_STRING"] = (queryPos != std::string::npos) ? uri.substr(queryPos + 1) : "";
    
    _env["REDIRECT_STATUS"] = "200";
    _env["SERVER_NAME"] = serverConfig.getServerName();
    _env["SERVER_PORT"] = toString(serverConfig.getPort());
    
    // Variables d'environnement système importantes
    _env["PATH"] = "/usr/bin:/bin:/usr/local/bin";

    std::string documentRoot = serverConfig.getRoot();
    if (location && !location->getRoot().empty())
        documentRoot = location->getRoot();
    if (!documentRoot.empty())
        _env["DOCUMENT_ROOT"] = documentRoot;

    if (location) {
        const std::map<std::string, std::string>& cgiParams = location->getCgiParams();
        for (std::map<std::string, std::string>::const_iterator it = cgiParams.begin(); it != cgiParams.end(); ++it) {
            _env[it->first] = it->second;
        }
    }
    
    // Headers HTTP
    const std::map<std::string, std::string>& headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); 
         it != headers.end(); ++it) {
        std::string envName = "HTTP_" + toUpperCase(replaceChars(it->first, "-", "_"));
        _env[envName] = it->second;
    }
    
    // Content-Length et Content-Type pour POST
    if (request.getMethod() == "POST" || !request.getBody().empty()) {
        _env["CONTENT_LENGTH"] = toString(request.getBody().size());
        std::string contentType = request.getHeader("Content-Type");
        if (!contentType.empty()) {
            _env["CONTENT_TYPE"] = contentType;
        }
    }
}

/* void CgiHandler::setupEnvironment(const Request& request, const std::string& scriptPath) {
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
	_env["REDIRECT_STATUS"] = "200";
	
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
} */

/* void readParseCGI(int pipe_out[2], int pid, Response& response){
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
		if (response.getHeaders().find("Content-Type") == response.getHeaders().end()
    	&& response.getHeaders().find("content-type") == response.getHeaders().end())
    		response.setHeader("Content-Type", "text/html");
} */

void readParseCGI(int pipe_out[2], int pid, Response& response) {
    char buffer[4096];
    std::string cgiOutput;
    ssize_t bytesRead;
    
    // AJOUT: Timeout pour éviter les blocages
    struct timeval timeout;
    timeout.tv_sec = 30; // 30 secondes
    timeout.tv_usec = 0;
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipe_out[0], &readfds);
    
    while (true) {
        int selectResult = select(pipe_out[0] + 1, &readfds, NULL, NULL, &timeout);
        if (selectResult <= 0) {
            break; // Timeout ou erreur
        }
        
        bytesRead = read(pipe_out[0], buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) break;
        
        buffer[bytesRead] = '\0'; // Sécurité
        cgiOutput.append(buffer, bytesRead);
    }
    
    close(pipe_out[0]);
    waitpid(pid, NULL, 0);

    // CORRECTION: Meilleure gestion des en-têtes CGI
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        headerEnd = cgiOutput.find("\n\n");
    }
    
    if (headerEnd != std::string::npos) {
        std::string headersPart = cgiOutput.substr(0, headerEnd);
        std::string body = cgiOutput.substr(headerEnd + (headersPart.find("\r\n\r\n") != std::string::npos ? 4 : 2));
        
        // Parse headers
        std::vector<std::string> headerLines = ParserUtils::split(headersPart, '\n');
        bool hasStatus = false;
        bool hasContentType = false;
        
        for (size_t i = 0; i < headerLines.size(); ++i) {
            std::string line = headerLines[i];
            // Nettoyer les \r
            if (!line.empty() && line[line.size()-1] == '\r')
                line.erase(line.size()-1);
            
            if (line.empty()) continue;
            
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string name = ParserUtils::trim(line.substr(0, colonPos));
                std::string value = ParserUtils::trim(line.substr(colonPos + 1));
                
                // Gérer les en-têtes spéciaux
                std::string nameLower = name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                
                if (nameLower == "status") {
                    // Extraire le code de statut
                    size_t spacePos = value.find(' ');
                    if (spacePos != std::string::npos) {
                        int statusCode = std::atoi(value.substr(0, spacePos).c_str());
                        std::string statusText = value.substr(spacePos + 1);
                        response.setStatus(statusCode, statusText);
                    } else {
                        int statusCode = std::atoi(value.c_str());
                        response.setStatus(statusCode, "");
                    }
                    hasStatus = true;
                } else {
                    response.setHeader(name, value);
                    if (nameLower == "content-type") {
                        hasContentType = true;
                    }
                }
            }
        }
        
        // Valeurs par défaut si non définies
        if (!hasStatus) {
            response.setStatus(200, "OK");
        }
        if (!hasContentType) {
            response.setHeader("Content-Type", "text/html");
        }
        
        response.setBody(body);
    } else {
        // Pas d'en-têtes séparés, traiter comme du contenu brut
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setBody(cgiOutput);
    }
}

/* std::string chooseInterpreter(const std::string& scriptPath, const std::string& defaultInterpreter) {
	std::string ext;
	size_t dotPos = scriptPath.find_last_of('.');
	if (dotPos != std::string::npos)
		ext = scriptPath.substr(dotPos);

	if (ext == ".pl")
		return "/usr/bin/perl";
	else if (ext == ".php")
		return "/usr/bin/php-cgi";
	else if (ext == ".py")
		return "/usr/bin/python3";
	else if (ext == ".sh")
		return "/bin/bash";
	return defaultInterpreter;
} */

std::string chooseInterpreter(const std::string& scriptPath, const std::string& defaultInterpreter) {
    std::string ext;
    size_t dotPos = scriptPath.find_last_of('.');
    if (dotPos != std::string::npos)
        ext = scriptPath.substr(dotPos);

    std::string interpreter;
    if (ext == ".pl")
        interpreter = "/usr/bin/perl";
    else if (ext == ".php")
        interpreter = "/usr/bin/php-cgi";
    else if (ext == ".py")
        interpreter = "/usr/bin/python3";
    else if (ext == ".sh")
        interpreter = "/bin/bash";
    else
        interpreter = defaultInterpreter;
    
    // AJOUT: Vérifier si l'interpréteur existe
    if (!interpreter.empty() && access(interpreter.c_str(), X_OK) != 0) {
        std::cerr << "Warning: Interpreter not found or not executable: " << interpreter << std::endl;
        // Essayer des alternatives
        if (ext == ".py") {
            if (access("/usr/bin/python", X_OK) == 0) return "/usr/bin/python";
            if (access("/bin/python3", X_OK) == 0) return "/bin/python3";
        }
        return ""; // Pas d'interpréteur trouvé
    }
    
    return interpreter;
}

Response CgiHandler::execute(const Request& request, 
						   const std::string& scriptPath, 
						   const std::string& interpreter,
						   const LocationConfig* location,
						   const ServerConfig& serverConfig) {
	Response response;

	if (!fileExists(scriptPath)) {
		response.setStatus(404, "Not Found");
		return response;
	}
	if (access(scriptPath.c_str(), X_OK) != 0) {
		response.setStatus(403, "Forbidden");
		return response;
	}

	setupEnvironment(request, scriptPath, location, serverConfig);

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
		dup2(pipe_out[1], STDERR_FILENO); // important pour voir erreurs Python/Perl/PHP

		close(pipe_in[0]);
		close(pipe_in[1]);
		close(pipe_out[0]);
		close(pipe_out[1]);
		
		std::vector<char*> envArray = prepareEnvArray();
		std::vector<char*> args = prepareArgs(scriptPath, interpreter);
		std::string chosenInterpreter;
		chosenInterpreter = chooseInterpreter(scriptPath, interpreter);
		if (interpreter.empty()) {
        if (execve(scriptPath.c_str(), args.data(), envArray.data()) == -1) {
            std::cerr << "CGI execution failed: " << strerror(errno) << std::endl;
        }
    } else {
        if (execve(chosenInterpreter.c_str(), args.data(), envArray.data()) == -1) {
            std::cerr << "CGI execution failed: " << strerror(errno) << std::endl;
        }
	}
		exit(1);
	}
	else { // parent process
		close(pipe_in[0]);
		close(pipe_out[1]);
		
		// write body request into CGI (handle partial writes)
		if (!request.getBody().empty()) {
			const std::string& body = request.getBody();
			size_t written = 0;
			while (written < body.size()) {
				size_t chunk = body.size() - written;
				ssize_t w = write(pipe_in[1], body.c_str() + written, chunk);
				if (w <= 0) {
					close(pipe_in[1]);
					close(pipe_out[0]);
					kill(pid, SIGKILL);
					waitpid(pid, NULL, 0);
					response.setStatus(500, "Internal Server Error");
					return response;
				}
				written += static_cast<size_t>(w);
			}
		}
		close(pipe_in[1]);
		readParseCGI(pipe_out, pid, response);
	}
	return response;
}
