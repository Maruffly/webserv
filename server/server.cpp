#include "server.hpp"

Server::Server() {
	// TODO: Constructeur par défaut
}

Server::~Server() {
	// TODO: Destructeur
}

Server::Server(const Server& src) {
	// TODO: Constructeur de copie
}

Server& Server::operator=(const Server& src) {
	if (this != &src) {
		// TODO: Copie des membres
	}
	return *this;
}

int Server::setupServer(void){
	sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(8080);
	serverAddress.sin_addr.s_addr = INADDR_ANY;

	int	serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (serverSocket == -1) {
		std::cerr << "Error: Unable to create server socket endpoint" << std::endl;
		return -1;
	}
	if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == -1){
		std::cerr << "Error: Unable to bind address" << std::endl;
	}
	int clientSocket = accept(serverSocket, NULL, NULL);
	if (clientSocket == -1)
		std::cerr << "Error: Unable to create client socket" << std::endl;
	if (listen(serverSocket, 1000) == -1)
		std::cerr << "Error: Could not listen to client socket" << std::endl;
	return 0;
}


