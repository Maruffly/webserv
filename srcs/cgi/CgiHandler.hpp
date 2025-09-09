#pragma once

#include "../config/LocationConfig.hpp"

class CgiHandler {
	private:
			std::map<std::string, std::string> _env;
			std::string _output;
			int			_exitStatus;
	public:
			void setEnvironment();

};