/* #include "CgiHandler.hpp"

#include "../utils/Utils.hpp"
#include <signal.h>
#include <cctype>

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


void readParseCGI(int pipe_out[2], int pid, Response& response) 
{
    char buffer[4096];
    std::string cgiOutput;
    ssize_t bytesRead;
    
    close(pipe_out[1]);
    // Timeout pour éviter les blocages
    time_t startTime = time(NULL);
    const time_t MAX_IDLE = 5;
    
    while (true) 
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(pipe_out[0], &readfds);

        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        
        int sel;
        do {
            sel = select(pipe_out[0] + 1, &readfds, NULL, NULL, &timeout);
        } while (sel == -1 && errno == EINTR);

        if (sel == -1) {
            // erreur sérieuse
            break;
        } else if (sel == 0) {
            // select timeout -> check idle
            if (time(NULL) - startTime >= MAX_IDLE) {
                kill(pid, SIGKILL);
                break;
            } else {
                continue;
            }
        }
        
        bytesRead = read(pipe_out[0], buffer, sizeof(buffer));
        //usleep(1000);
        std::cerr << "[CGI] Read returned " << bytesRead << " bytes" << std::endl;

        if (bytesRead > 0) {
            cgiOutput.append(buffer, bytesRead);
            startTime = time(NULL); // refresh idle timer
            continue;
        } else if (bytesRead == 0) {
            // EOF
            break;
        } else { // bytesRead == -1
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // pas de données pour l'instant, continuer
                continue;
            }
            // erreur fatale de read
            break;
        }
    }
    close(pipe_out[0]);
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
        std::cerr << "[CGI] Child exited with status " << status << std::endl;
 // attendre proprement la fin du child
    } while (result == -1 && errno == EINTR);

    // --- ensuite parser les headers et body du cgiOutput ---
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        headerEnd = cgiOutput.find("\n\n");

    if (headerEnd != std::string::npos) {
        std::string headersPart = cgiOutput.substr(0, headerEnd);
        std::string body = cgiOutput.substr(headerEnd + (cgiOutput.find("\r\n\r\n") != std::string::npos ? 4 : 2));

        std::vector<std::string> headerLines = ParserUtils::split(headersPart, '\n');
        bool hasStatus = false;
        bool hasContentType = false;

        for (size_t i = 0; i < headerLines.size(); ++i) {
            std::string line = headerLines[i];
            if (!line.empty() && line[line.size()-1] == '\r')
                line.erase(line.size()-1);
            if (line.empty()) continue;

            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string name = ParserUtils::trim(line.substr(0, colonPos));
                std::string value = ParserUtils::trim(line.substr(colonPos + 1));
                std::string nameLower = toLowerCase(name);

                if (nameLower == "status") {
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
                    if (nameLower == "content-type") hasContentType = true;
                }
            }
        }

        if (!hasStatus) response.setStatus(200, "OK");
        if (!hasContentType) response.setHeader("Content-Type", "text/html");
        response.setBody(body);
    } else {
        // Pas d'entête séparée : contenu brut
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setBody(cgiOutput);
    }
}


std::string chooseInterpreter(const std::string& scriptPath, const std::string& defaultInterpreter) 
{
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
		close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        response.setStatus(500, "Internal Server Error");
		return response;
	}
	if (pid == 0) { // child process
		//usleep(1000); test

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
        // Dans le processus enfant
    if (chosenInterpreter.empty()) {
        // Exécuter directement le script
        if (execve(scriptPath.c_str(), &args[0], &envArray[0]) == -1) {
            perror("execve script");
            _exit(1);
        }
    }
    else{
        // Utiliser l'interpréteur
        if (execve(chosenInterpreter.c_str(), &args[0], &envArray[0]) == -1) {
            perror("execve interpreter");
            _exit(1);
            }
        }
		exit(1);
	}
	else { // parent process
        std::cerr << "[CGI] Closing pipe_in[1]" << std::endl;
		close(pipe_in[0]);
		close(pipe_out[1]);
		
		// write body request into CGI (handle partial writes)
		if (!request.getBody().empty()) {
        const std::string& body = request.getBody();
        size_t written = 0;

            while (written < body.size()) {
                ssize_t w = write(pipe_in[1], body.data() + written, body.size() - written);
                if (w > 0) {
                    written += static_cast<size_t>(w);
                    continue;
                }
                if (w == -1) {
                    if (errno == EINTR) {
                        continue; // retry
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // wait until writable
                        fd_set wfds;
                        FD_ZERO(&wfds);
                        FD_SET(pipe_in[1], &wfds);
                        // No timeout here: we want to wait until kernel buffer is ready,
                        // but you can add a timeval if you prefer an upper bound.
                        int s = select(pipe_in[1] + 1, NULL, &wfds, NULL, NULL);
                        if (s == -1) {
                            if (errno == EINTR) continue;
                            // fatal select error: cleanup and return 500
                            close(pipe_in[1]);
                            close(pipe_out[0]);
                            kill(pid, SIGKILL);
                            waitpid(pid, NULL, 0);
                            response.setStatus(500, "Internal Server Error");
                            return response;
                        }
                        continue; // now writable — retry write
                    }
                }
                // Other fatal error: cleanup and return 500
                close(pipe_in[1]);
                close(pipe_out[0]);
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
                response.setStatus(500, "Internal Server Error");
                return response;
            }
        }
		close(pipe_in[1]);
		readParseCGI(pipe_out, pid, response);
        
	}
	return response;
} */

#include "CgiHandler.hpp"
#include "../utils/Utils.hpp"
#include <signal.h>
#include <cctype>
#include <fcntl.h>

CgiHandler::CgiHandler() {}
CgiHandler::~CgiHandler() {}

// Fonction helper pour rendre un fd non-bloquant
static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) flags = 0;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

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
        args.push_back(strdup(interpreter.c_str()));
        args.push_back(strdup(scriptPath.c_str()));
    } else {
        args.push_back(strdup(scriptPath.c_str()));
    }
    args.push_back(NULL);
    return args;
}

void CgiHandler::setupEnvironment(const Request& request, const std::string& scriptPath,
                                  const LocationConfig* location, const ServerConfig& serverConfig) {
    _env.clear();
    
    _env["GATEWAY_INTERFACE"] = "CGI/1.1";
    _env["SERVER_PROTOCOL"] = "HTTP/1.1";
    _env["SERVER_SOFTWARE"] = "webserv/1.0";
    _env["REQUEST_METHOD"] = request.getMethod();
    _env["PYTHONUNBUFFERED"] = "1"; // Force Python à ne pas bufferiser
    
    std::string uri = request.getUri();
    size_t queryPos = uri.find('?');
    std::string pathOnly = (queryPos != std::string::npos) ? uri.substr(0, queryPos) : uri;
    
    _env["SCRIPT_NAME"] = pathOnly;
    _env["SCRIPT_FILENAME"] = scriptPath;
    _env["PATH_INFO"] = pathOnly;
    _env["QUERY_STRING"] = (queryPos != std::string::npos) ? uri.substr(queryPos + 1) : "";
    _env["REDIRECT_STATUS"] = "200";
    _env["SERVER_NAME"] = serverConfig.getServerName();
    _env["SERVER_PORT"] = toString(serverConfig.getPort());
    _env["PATH"] = "/usr/bin:/bin:/usr/local/bin";

    std::string documentRoot = serverConfig.getRoot();
    if (location && !location->getRoot().empty())
        documentRoot = location->getRoot();
    if (!documentRoot.empty())
        _env["DOCUMENT_ROOT"] = documentRoot;

    if (location) {
        const std::map<std::string, std::string>& cgiParams = location->getCgiParams();
        for (std::map<std::string, std::string>::const_iterator it = cgiParams.begin(); 
             it != cgiParams.end(); ++it) {
            _env[it->first] = it->second;
        }
    }
    
    const std::map<std::string, std::string>& headers = request.getHeaders();
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); 
         it != headers.end(); ++it) {
        std::string envName = "HTTP_" + toUpperCase(replaceChars(it->first, "-", "_"));
        _env[envName] = it->second;
    }
    
    if (request.getMethod() == "POST" || !request.getBody().empty()) {
        _env["CONTENT_LENGTH"] = toString(request.getBody().size());
        std::string contentType = request.getHeader("Content-Type");
        if (!contentType.empty()) {
            _env["CONTENT_TYPE"] = contentType;
        }
    }
}

// NOUVELLE VERSION : lecture et écriture simultanées avec select() + stderr séparé
void readWriteParseCGI(int pipe_in[2], int pipe_out[2], int pipe_err[2], int pid, 
                       const std::string& body, Response& response) 
{
    // Fermer les extrémités inutilisées
    close(pipe_in[0]);
    close(pipe_out[1]);
    close(pipe_err[1]);
    
    // Rendre les pipes non-bloquants
    setNonBlocking(pipe_in[1]);
    setNonBlocking(pipe_out[0]);
    setNonBlocking(pipe_err[0]);
    
    char buffer[4096];
    std::string cgiOutput;
    std::string cgiErrors;
    size_t bodyWritten = 0;
    bool writeComplete = body.empty();
    bool readComplete = false;
    bool errComplete = false;
    
    time_t startTime = time(NULL);
    const time_t MAX_TIMEOUT = 30;
    
    while (!readComplete || !errComplete) {
        if (time(NULL) - startTime >= MAX_TIMEOUT) {
            std::cerr << "[CGI] Timeout - killing process" << std::endl;
            close(pipe_in[1]);
            close(pipe_out[0]);
            close(pipe_err[0]);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            response.setStatus(504, "Gateway Timeout");
            return;
        }
        
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        
        int maxfd = 0;
        
        // Lire stdout
        if (!readComplete) {
            FD_SET(pipe_out[0], &readfds);
            maxfd = pipe_out[0];
        }
        
        // Lire stderr
        if (!errComplete) {
            FD_SET(pipe_err[0], &readfds);
            if (pipe_err[0] > maxfd) maxfd = pipe_err[0];
        }
        
        // Écrire stdin si nécessaire
        if (!writeComplete) {
            FD_SET(pipe_in[1], &writefds);
            if (pipe_in[1] > maxfd) maxfd = pipe_in[1];
        }
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int sel = select(maxfd + 1, &readfds, &writefds, NULL, &timeout);
        
        if (sel == -1) {
            if (errno == EINTR) continue;
            std::cerr << "[CGI] Select error: " << strerror(errno) << std::endl;
            break;
        }
        
        if (sel == 0) continue; // Timeout, vérifier timeout global
        
        // Écrire le body
        if (!writeComplete && FD_ISSET(pipe_in[1], &writefds)) {
            ssize_t w = write(pipe_in[1], body.data() + bodyWritten, 
                            body.size() - bodyWritten);
            if (w > 0) {
                bodyWritten += w;
                if (bodyWritten >= body.size()) {
                    writeComplete = true;
                    close(pipe_in[1]);
                    std::cerr << "[CGI] Body written completely" << std::endl;
                }
            } else if (w == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                std::cerr << "[CGI] Write error: " << strerror(errno) << std::endl;
                writeComplete = true;
                close(pipe_in[1]);
            }
        }
        
        // Lire stdout
        if (!readComplete && FD_ISSET(pipe_out[0], &readfds)) {
            ssize_t r = read(pipe_out[0], buffer, sizeof(buffer));
            if (r > 0) {
                cgiOutput.append(buffer, r);
            } else if (r == 0) {
                readComplete = true;
                std::cerr << "[CGI] EOF from stdout" << std::endl;
            } else if (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                readComplete = true;
            }
        }
        
        // Lire stderr
        if (!errComplete && FD_ISSET(pipe_err[0], &readfds)) {
            ssize_t r = read(pipe_err[0], buffer, sizeof(buffer));
            if (r > 0) {
                cgiErrors.append(buffer, r);
            } else if (r == 0) {
                errComplete = true;
                std::cerr << "[CGI] EOF from stderr" << std::endl;
            } else if (r == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                errComplete = true;
            }
        }
    }
    
    close(pipe_out[0]);
    close(pipe_err[0]);
    if (!writeComplete) close(pipe_in[1]);
    
    // Afficher stderr pour debug
    if (!cgiErrors.empty()) {
        std::cerr << "[CGI stderr]:\n" << cgiErrors << std::endl;
    }
    
    // Attendre la fin du processus
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);
    
    // Vérifier le code de sortie
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        std::cerr << "[CGI] Process exited with code " << exitCode << std::endl;
        if (exitCode == 127) {
            // Échec d'exec
            response.setStatus(500, "Internal Server Error");
            response.setBody("CGI execution failed");
            return;
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        std::cerr << "[CGI] Process killed by signal " << sig << std::endl;
        if (sig == SIGKILL) {
            // Timeout
            response.setStatus(504, "Gateway Timeout");
            response.setBody("CGI timeout");
            return;
        }
    }
    
    // Parser la sortie CGI
    size_t headerEnd = cgiOutput.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
        headerEnd = cgiOutput.find("\n\n");

    if (headerEnd != std::string::npos) {
        std::string headersPart = cgiOutput.substr(0, headerEnd);
        std::string bodyPart = cgiOutput.substr(headerEnd + 
            (cgiOutput.find("\r\n\r\n") != std::string::npos ? 4 : 2));

        std::vector<std::string> headerLines = ParserUtils::split(headersPart, '\n');
        bool hasStatus = false;
        bool hasContentType = false;

        for (size_t i = 0; i < headerLines.size(); ++i) {
            std::string line = headerLines[i];
            if (!line.empty() && line[line.size()-1] == '\r')
                line.erase(line.size()-1);
            if (line.empty()) continue;

            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string name = ParserUtils::trim(line.substr(0, colonPos));
                std::string value = ParserUtils::trim(line.substr(colonPos + 1));
                std::string nameLower = toLowerCase(name);

                if (nameLower == "status") {
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
                    if (nameLower == "content-type") hasContentType = true;
                }
            }
        }

        if (!hasStatus) response.setStatus(200, "OK");
        if (!hasContentType) response.setHeader("Content-Type", "text/html");
        response.setBody(bodyPart);
    } else {
        response.setStatus(200, "OK");
        response.setHeader("Content-Type", "text/html");
        response.setBody(cgiOutput);
    }
}

std::string chooseInterpreter(const std::string& scriptPath, const std::string& defaultInterpreter) 
{
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
    
    if (!interpreter.empty() && access(interpreter.c_str(), X_OK) != 0) {
        std::cerr << "Warning: Interpreter not found: " << interpreter << std::endl;
        if (ext == ".py") {
            if (access("/usr/bin/python", X_OK) == 0) return "/usr/bin/python";
            if (access("/bin/python3", X_OK) == 0) return "/bin/python3";
        }
        return "";
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

    int pipe_in[2], pipe_out[2], pipe_err[2];
    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1 || pipe(pipe_err) == -1) {
        response.setStatus(500, "Internal Server Error");
        return response;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        close(pipe_err[0]); close(pipe_err[1]);
        response.setStatus(500, "Internal Server Error");
        return response;
    }
    
    if (pid == 0) { // Processus enfant
        // Fermer les extrémités inutilisées
        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_err[0]);
        
        // Rediriger stdin/stdout/stderr
        if (dup2(pipe_in[0], STDIN_FILENO) == -1) {
            perror("dup2 stdin");
            _exit(1);
        }
        if (dup2(pipe_out[1], STDOUT_FILENO) == -1) {
            perror("dup2 stdout");
            _exit(1);
        }
        if (dup2(pipe_err[1], STDERR_FILENO) == -1) {
            perror("dup2 stderr");
            _exit(1);
        }

        // Fermer les descripteurs originaux après duplication
        close(pipe_in[0]);
        close(pipe_out[1]);
        close(pipe_err[1]);
        
        // Préparer l'environnement et les arguments
        std::vector<char*> envArray = prepareEnvArray();
        std::string chosenInterpreter = chooseInterpreter(scriptPath, interpreter);
        std::vector<char*> args = prepareArgs(scriptPath, chosenInterpreter);
        
        // Exécuter le CGI
        if (chosenInterpreter.empty()) {
            execve(scriptPath.c_str(), &args[0], &envArray[0]);
            // Si on arrive ici, execve a échoué
            std::cerr << "execve failed for " << scriptPath << ": " << strerror(errno) << std::endl;
        } else {
            execve(chosenInterpreter.c_str(), &args[0], &envArray[0]);
            std::cerr << "execve failed for " << chosenInterpreter << ": " << strerror(errno) << std::endl;
        }
        
        // Libérer la mémoire en cas d'échec
        for (size_t i = 0; i < envArray.size(); ++i) free(envArray[i]);
        for (size_t i = 0; i < args.size(); ++i) free(args[i]);
        _exit(127); // Code d'erreur standard pour échec d'exec
    }
    else { // Processus parent
        // Lecture/écriture avec stderr séparé
        readWriteParseCGI(pipe_in, pipe_out, pipe_err, pid, request.getBody(), response);
    }
    
    return response;
}
