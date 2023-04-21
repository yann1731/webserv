
#include "config.hpp"
#include "socket.hpp"

#include "Parser.hpp"

/** Maximum pending connections in queue */
#define SO_MAX_QUEUE 10

/** Global config object */
HttpConfig httpConfig = HttpConfig();

/**
 * @brief Main function
 *
 * @param argc Number of arguments
 * @param argv config file name
 */
int main(int argc, char* argv[]) {
    /** Parse the config file x*/
	if (argc > 2) {
		std::cerr << "Usage: ./webserv [config_file]" << std::endl;
		return (EXIT_FAILURE);
	}
	httpConfig = HttpConfig();
	try {
		if (argc == 2) {
			parseConfig(argv[1]);
		} else {
			parseConfig(CONFIG_FILE);
		}
	}
	catch (std::exception &e) {
		std::cerr << e.what() << std::endl;
		return (EXIT_FAILURE);
	}

    vector<int>    ports;
    vector<Socket> sockets;

    /** @todo get ports from config */
    ports.push_back(3000);

    /** Create a socket for each port */
    for (vector<int>::iterator it = ports.begin(); it != ports.end(); ++it) {
        try {
            sockets.push_back(Socket(*it, INADDR_ANY));
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    while (true) {
        /** @todo accept connections */
        break;
    }

    /** Close sockets */
    for (vector<Socket>::iterator it = sockets.begin(); it != sockets.end();
         ++it) {
        try {
            it->close();
        } catch (std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    return (EXIT_SUCCESS);
}
