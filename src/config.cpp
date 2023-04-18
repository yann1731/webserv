#include "config.hpp"

void tokenizeConfig(std::vector<std::string> &tokens, std::string line) {
	while (line.size() > 0) {
		size_t pos = line.find_first_not_of(" \t");
		if (pos == line.npos || line[pos] == '#') {break;}
		line = line.substr(pos);
		pos = line.find_first_of("\\#{}; \t\n\0");
		pos == 0 ? pos = 1: pos;
		std::string tmp = line.substr(0, pos);
		tokens.push_back(tmp);;
		line = line.substr(tmp.size());
	}
}

/**
 * @brief Parse a config file. Read the file line by line and split into tokens.
 *
 * @param config_file Path to the config file
 *
 */
void parseConfig(std::string config_file) {
	std::string line;
	std::ifstream file(config_file);
	std::vector<std::string> tokens;

	if (!file.is_open() || file.peek() == std::ifstream::traits_type::eof()) {
 		throw FileError(config_file);}
	std::string filePath = std::string(realpath(config_file.c_str(), nullptr));
 	if (PRINT) {
		std::cout << "# configuration file " << filePath << ":" << std::endl;}
 	while(getline(file, line)) {
		if (PRINT) {
			std::cout << line.substr(0, line.find_first_of("\n")) << std::endl;}
		tokenizeConfig(tokens, line);
	}
	file.close();
}

//**************************************************************************//
//                              Constructors                                //
//**************************************************************************//

// Events::Events(void) {
	// settings.push_back("worker_connections");
	// settings.push_back("use");
	// settings.push_back("multi_accept");
	// settings.push_back("accept_mutex_delay");
	// settings.push_back("debug_connection");
	// settings.push_back("use_poll");
	// settings.push_back("deferred_accept");}
// 
// Events::Events(const Events &copy) {
	// *this = copy;}

//**************************************************************************//
//                                 Setters                                  //
//**************************************************************************//

//**************************************************************************//
//                                 Getters                                  //
//**************************************************************************//

//**************************************************************************//
//                             Member functions                             //
//**************************************************************************//

// bool Events::isSetting(std::string setting) {
	// size_t pos = 0;
	// std::cout << "setting: " << setting << ";" << std::endl;
	// for (; pos < settings.size(); ++pos) {
		// if (setting.compare(settings.at(pos)) == 0) {
			// return true;}}
	// return false;
// }

// Determine if the string following the setting is a valid one, which is an integer that ends with ";" 
// void Events::setWorkerConnections(std::vector<std::string>::iterator &it) {
	// std::string num = *it;
	// for (size_t i = 0; i < num.length(); ++i) {
		// if (!std::isdigit(num[i])) {
			// throw std::exception();}
	// }
	// worker_connections = std::stoi(num);
	// if (*(++it) != ";" || worker_connections < 1) {
		// throw std::exception();}
// }

// void Events::setUse(std::vector<std::string>::iterator &it) {
	// std::string options[5] = {"epoll", "kqueue", "devpoll", "poll", "select"};
	// std::string setting = *it;
	// for (size_t pos = 0; pos < 5; ++pos) {
		// if (setting.compare(options[pos]) == 0) {
			// if (*(++it) != ";") {
				// throw WebExcep::WrongSettingValue("use");}
			// use = setting;
			// return;}
	// }
	// throw std::exception();
// }

// void Events::setSetting(std::string &setting, std::vector<std::string>::iterator &it) {
	// size_t pos = 0;
	// for (; pos < settings.size(); ++pos) {
		// if (setting.compare(settings.at(pos)) == 0) {
			// break;}}
	// switch (pos) {
		// case WORKER_CONNECTIONS:
			// setWorkerConnections(it);
			// break;
		// case USE:
			// setUse(it);
			// break;
		// case MULTI_ACCEPT:
			// break;
		// case ACCEPT_MUTEX_DELAY:
			// break;
		// case DEBUG_CONNECTION:
			// break;
		// case USE_POLL:
			// break;
		// case DEFERRED_ACCEPT:
			// break;
		// default:
			// throw WebExcep::UnknownDirective(setting); // unknown directive
	// }
// }

//**************************************************************************//
//                           Operators overload                             //
//**************************************************************************//

// Events &Events::operator=(const Events &copy){
	// if (this != &copy){}
	// return (*this);}

//**************************************************************************//
//                               Destructors                                //
//**************************************************************************//

// Events::~Events(void){}
