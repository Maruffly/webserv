#include "Session.hpp"

Session::Session() : sessionId(NULL), expirationTime(0), lastActivity(0) {}

Session::Session(const std::string& id) : sessionId(id){
	time_t now = time(NULL);
	expirationTime = now + 1800;
	lastActivity = now;
}

bool Session::isValid() const {
	return time(NULL) < expirationTime;
}

void Session::updateActivity() {
	lastActivity = time(NULL);
	expirationTime = lastActivity + 1800;
}

void Session::setData(const std::string& key, const std::string& value) {
	updateActivity();
	data[key] = value;
}

std::string Session::getData(const std::string& key) const {
	std::map<std::string, std::string>::const_iterator it = data.find(key);
    if (it != data.end()) {
        return it->second;
    }
    return "";
}