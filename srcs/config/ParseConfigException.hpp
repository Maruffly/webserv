#pragma once

#include <string>
#include <stdexcept>

class ParseConfigException : public std::runtime_error {
	private:
			std::string _directive;
			std::string _location;
			static std::string formatMessage(const std::string& message, const std::string& directive, const std::string& location);

	public:
    		ParseConfigException(const std::string& message, const std::string& directive = "", const std::string& location= "") :
			std::runtime_error(formatMessage(message, directive, location)), _directive(directive), _location(location){}
    		/* int getLineNumber() const {
				return _lineNbr; } */
			const std::string& getDirective() const {
				return _directive; }
};