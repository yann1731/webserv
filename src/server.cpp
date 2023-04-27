/**
 * @file server.cpp
 * @brief Defines classes for creating web servers that can handle HTTP
 * requests.
 *
 * This file contains the implementation of the Server and HttpServer classes.
 * The Server class is an abstract base class that provides an interface for
 * creating servers. The HttpServer class inherits from the Server class and
 * implements an HTTP server that can receive and send HTTP requests and
 * responses.
 *
 * @note This code is for educational purposes only and should not be used in
 * production environments without extensive testing and modification.
 *
 * @version 0.1
 * @date 2023-04-19
 * @authors
 *   - Francis L.
 *   - Marc-André L.
 *   - Cole H.
 */

#include "server.hpp"

using std::cerr;
using std::endl;

extern HttpConfig httpConfig;

Server::~Server() {
}

HttpServer::HttpServer(SocketGenerator socket_generator, HttpConfig httpConfig,
                       EventListener* listener) {
    socket_generator_ = socket_generator;
    config_           = httpConfig;
    listener_         = listener;
}

HttpServer::~HttpServer() throw() {
}

void HttpServer::start() {
    // Create a socket for each server in the config
    Socket* new_socket;
    for (vector<ServerConfig>::iterator it = config_.servers.begin();
         it != config_.servers.end(); ++it) {
        try {
            // Create a new socket
            new_socket = socket_generator_();

            // Bind the socket to the address/port
            int server_id =
                new_socket->bind(it->listen.first, it->listen.second);

            // Listen for connections
            new_socket->listen();

            // Add the socket to the map
            server_sockets_[server_id] = new_socket;

            // Add the socket to the listener
            listener_->registerEvent(server_id, 0); /** @todo event flags */
        } catch (runtime_error& e) {
            cerr << e.what() << endl;

            // Delete the socket, is this safe even if it wasn't constructed?
            delete new_socket;
        }
    }

    // Run the server
    run();
}

void HttpServer::stop() {
    // Close all sockets and delete them
    for (map<int, Socket*>::iterator it = server_sockets_.begin();
         it != server_sockets_.end(); ++it) {
        try {
            it->second->close();
        } catch (runtime_error& e) {
            cerr << e.what() << endl;
        }
        delete it->second;
    }

    // Clear the map of sockets
    server_sockets_.clear();
}

void HttpServer::run() {
    // Loop forever
    while (true) {
        // Wait for an event
        int event = listener_->listen();

        // Handle event
        switch (event) {
            case 0: /** @todo add event macros */
                /** @todo write error handler */
                break;
            case 1:
                /** @todo write handlers */
                break;
            default:
                /** @todo what should default case be */
                break;
        }
    }
}

Request HttpServer::receiveRequest() {
    Request request;

    /** @todo should be client_id not 0 */
    string buffer = sessions_[0]->recv(0);

    // start-line
    request.method = buffer.substr(0, buffer.find(' '));
    buffer.erase(0, buffer.find(' ') + 1);
    request.uri = buffer.substr(0, buffer.find(' '));
    buffer.erase(0, buffer.find(' ') + 1);
    request.version = buffer.substr(0, buffer.find(CRLF));
    buffer.erase(0, buffer.find(CRLF) + 2);

    // body
    request.body = buffer.substr(buffer.find("\r\n\r\n") + 4);
    buffer.erase(buffer.find("\r\n\r\n") + 2);

    // headers
    while (buffer.find(CRLF) != string::npos) {
        string key = buffer.substr(0, buffer.find(':'));
        buffer.erase(0, buffer.find(':') + 2);
        string value = buffer.substr(0, buffer.find(CRLF));
        buffer.erase(0, buffer.find(CRLF) + 2);
        request.headers[key] = value;
    }

    return request;
}

Response HttpServer::handleRequest(Request request) {
    /** @todo implement */
    Response response;

    if (request.method == "GET" && request.uri == "/") {
        response.version                 = "HTTP/1.1";
        response.status                  = "200 OK";
        response.server                  = "webserv/0.1";
        response.headers["Content-Type"] = "text/html";
        response.body = "<html><body><h1>Hello World!</h1></body></html>";
    } else {
        response.version                 = "HTTP/1.1";
        response.status                  = "404 Not Found";
        response.server                  = "webserv/0.1";
        response.headers["Content-Type"] = "text/html";
        response.body = "<html><body><h1>404 Not Found</h1></body></html>";
    }

    return response;
}

void HttpServer::sendResponse(Response response) {
    string buffer;

    // status-line
    buffer.append(response.version + " ");
    buffer.append(response.status + " ");
    buffer.append(response.server + CRLF);

    // headers
    for (map<string, string>::iterator it = response.headers.begin();
         it != response.headers.end(); ++it) {
        buffer.append(it->first + ": " + it->second + CRLF);
    }

    // body
    buffer.append(CRLF + response.body);

    /** @todo should be client_id not 0 */
    sessions_[0]->send(0, buffer);
}
